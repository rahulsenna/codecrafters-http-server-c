#include <arpa/inet.h> // internet
#include <unistd.h> // read, write, close
#include <fcntl.h>  // open
#include <zlib.h>

#include <string_view>
#include <unordered_map>
#include <iostream>

std::unordered_map<std::string_view, std::string_view>
parse_header(std::string_view buffer)
{
    std::unordered_map<std::string_view, std::string_view> headers;
    int pos_beg = 0, pos_end = buffer.find(' ');
    headers["method"] = buffer.substr(pos_beg, pos_end - pos_beg);

    pos_beg = pos_end + 1, pos_end = buffer.find(' ', pos_beg);
    headers["url"] = buffer.substr(pos_beg, pos_end - pos_beg);

    pos_beg = pos_end + 1, pos_end = buffer.find("\r\n", pos_beg);
    headers["version"] = buffer.substr(pos_beg, pos_end - pos_beg);

    while(1)
    {
        pos_beg = pos_end+2;
        pos_end = buffer.find("\r\n", pos_beg);
        if (pos_end == -1)
            break;
        auto line = buffer.substr(pos_beg, pos_end - pos_beg);
        if (line.empty())
            break;

        int colon_pos = line.find(':');
        auto key = line.substr(0, colon_pos);
        auto value = line.substr(colon_pos+2, INT_MAX);
        headers[key] = value;
    }
    return headers;
}

int gzip_compress(std::string_view source, unsigned char *dest)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    zs.next_in = (Bytef *)source.data();
    zs.avail_in = source.length();
    zs.next_out = dest;
    zs.avail_out = source.length() + 128;

    if (deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    {
        perror("Failed deflateInit2\n");
        return -1;
    }
        
    int ret = deflate(&zs, Z_FINISH);
    deflateEnd(&zs);

    if (ret != Z_STREAM_END)
    {
        return -1;
        perror("Failed deflateEnd\n");
    }

    return zs.total_out;
}


std::string_view OK("HTTP/1.1 200 OK\r\n\r\n");
std::string_view CREATED("HTTP/1.1 201 Created\r\n\r\n");
std::string_view NOT_FOUND("HTTP/1.1 404 Not Found\r\n\r\n");

int main(int argc, char *argv[])
{
    // Disable output buffering
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
    
    std::unordered_map<std::string_view, std::string_view> options;
    for (int i = 1; i < argc; i+=2)
        options[argv[i]] = argv[i+1];
    
    struct sockaddr_in server_addr =
    {
        .sin_family = AF_INET,
        .sin_port = htons(4221),
        .sin_addr.s_addr = INADDR_ANY,
    };
    int server_sock = socket(PF_INET, SOCK_STREAM, 0);

    int reuse = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    bind(server_sock, (struct sockaddr*) &server_addr, sizeof(server_addr));
    listen(server_sock, 10);

    struct sockaddr client_addr;
    socklen_t len_client_addr;



    while(1)
    {
        int client_sock = accept(server_sock, &client_addr, &len_client_addr);
        if (client_sock == -1)
        { 
            perror("Accept Failed\n");
            continue;
        }
        if (fork() != 0)
            continue;

        char req_buf[1024];
        size_t bytes_read = read(client_sock, req_buf, sizeof(req_buf));
        req_buf[bytes_read] = 0;

        auto headers = parse_header(req_buf);

        std::string_view resp;
        char resp_str[1024];
        if (headers["url"] == "/")
            resp = OK;
        else if (headers["url"].starts_with("/echo/") )
        {
            auto output = headers["url"].substr(strlen("/echo/"), INT_MAX);
            if (headers["Accept-Encoding"].contains("gzip"))
            {
                unsigned char compressed[1024];
                size_t compressed_size = gzip_compress(output, compressed);
                snprintf(resp_str, 1024,
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Encoding: gzip\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: %lu\r\n"
                         "\r\n",
                         compressed_size);

                write(client_sock, resp_str, strlen(resp_str));
                write(client_sock, compressed, compressed_size);
                close(client_sock);
                continue;
            }
            snprintf(resp_str, 1024,
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: %lu\r\n"
                     "\r\n"
                     "%s",
                     output.length(), output.data());

            resp = resp_str;
        }
        else if (headers["url"] == "/user-agent")
        {
            snprintf(resp_str, 1024,
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: %lu\r\n"
                     "\r\n"
                     "%s",
                     headers["User-Agent"].length(), headers["User-Agent"].data());

            resp = resp_str;
        }
        else if (headers["url"].starts_with("/files/"))
        {
            std::string_view filename = headers["url"].substr(headers["url"].find_last_of('/') + 1, INT_MAX);
            std::string full_path = std::string(options["--directory"]) + std::string(filename);

            if (headers["method"] == "POST")
            {
                int fd = open(full_path.c_str(), O_CREAT | O_WRONLY, 0644);
                std::string_view buffer(req_buf);
                auto content = buffer.substr(buffer.find("\r\n\r\n")+4, INT_MAX);
                write(fd, content.data(), content.length());
                resp = CREATED;
                goto END_PROC;
            }
            int fd = open(full_path.c_str(), O_RDONLY, 0644);
            if(fd == -1)
            {
                resp = NOT_FOUND;
                goto END_PROC;
            }
            char file_contents[1024];
            size_t bytes_read = read(fd, file_contents, 1024);

            snprintf(resp_str, 1024,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Content-Length: %lu\r\n"
                "\r\n",
                bytes_read);
            write(client_sock, resp_str, strlen(resp_str));
            write(client_sock, file_contents, bytes_read);
            close(client_sock);
            continue;
        }
        else
            resp = NOT_FOUND;

END_PROC:
        write(client_sock, resp.data(), resp.length());
        close(client_sock);
    }

}