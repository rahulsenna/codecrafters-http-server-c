#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <zlib.h>


size_t gzip(const char *source, unsigned char *dest)
{
    uint64_t src_len = strlen(source);
    z_stream strm    = {0};
    strm.zalloc      = Z_NULL;
    strm.zfree       = Z_NULL;
    strm.opaque      = Z_NULL;
    strm.avail_in    = src_len;
    strm.next_in     = (Bytef *) source;
    strm.avail_out   = src_len + 128;
    strm.next_out    = dest;

    // Initialize for gzip compression (window bits 15 + 16 for gzip header)
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     15 | 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    {
        perror("Failed to compress (defalateInit2)\n");
		return -1;
    }

    int ret = deflate(&strm, Z_FINISH);
    deflateEnd(&strm);

    if (ret != Z_STREAM_END)
    {
        perror("Failed to compress (deflateEnd)\n");
		return -1;
    }
    return strm.total_out;
}


int main ()
{
    struct sockaddr_in server_addr =
        {
            .sin_family = AF_INET,
            .sin_port = htons(4221),
            .sin_addr.s_addr = INADDR_ANY,
        };

    int server_sock = socket(PF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_sock, 10);

    struct sockaddr client_addr;
    socklen_t client_addr_len;

    while(1)
    {
        int client_sock = accept(server_sock, &client_addr, &client_addr_len);
        if (client_sock == -1)
        { 
            perror("Accept Failed \n");
        	continue;
        }
if (fork() == 0)
{
        char request_buf[1024];
        size_t bytes_read = read(client_sock, request_buf, sizeof(request_buf));
        request_buf[bytes_read] = 0;

        printf("request: %s\n", request_buf);
        char *method = strtok(request_buf, " ");
        char *path = strtok(0, " ");
        char *http = strtok(0, "\r\n");
        char *host = strtok(0, "\r\n");

        char resp[1024];

        if (strncmp(path, "/", 2 ) == 0)
        {
            snprintf(resp, sizeof(resp),
                     "HTTP/1.1 200 OK\r\n\r\n");
        } else if (strncmp(path, "/echo/", strlen("/echo/") ) == 0)
        {
            char *out = path + strlen("/echo/");
            size_t outlen = strlen(out);
            int is_gzip = 0;
            unsigned char compressed[1024];
            char *curr_line;
            while ((curr_line = strtok(0, "\r\n")))
            {
                if (strstr(curr_line, "gzip"))
                {
                    is_gzip = 1;
                    outlen = gzip(out, compressed);
                    out = (char*)compressed;
                    break;
                }
            }
            

            if (is_gzip)
            {
                snprintf(resp, sizeof(resp),
                         "HTTP/1.1 200 OK\r\n"
                         "Content-Encoding: gzip\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: %lu\r\n"
                         "\r\n",
                         outlen);
                write(client_sock, resp, strlen(resp));
                write(client_sock, out, outlen);

                close(client_sock);
                continue;
                
            } else
            {
            snprintf(resp, sizeof(resp),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: %lu\r\n"
                     "\r\n"
                     "%s",
                     outlen, out
                     );    
            }

            
        } else if (strncmp(path, "/user-agent", strlen("/user-agent") ) == 0)
        {
            char *out = strtok(0, "\r\n");
            out += strlen("User-Agent: ");
            snprintf(resp, sizeof(resp),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: %lu\r\n"
                     "\r\n"
                     "%s",
                     strlen(out), out
                     );
        }else if (strncmp(path, "/files/", strlen("/files/") ) == 0)
        {
            char *filename = path + strlen("/files/");
            if (strncmp(method, "POST", strlen("POST")) == 0)
            {
                int fd = open(filename, O_WRONLY|O_CREAT, 0777);
                char *curr_line, *last_line;
                while((curr_line = strtok(0, "\r\n")))
                    last_line = curr_line;
                
                write(fd, last_line, strlen(last_line));
                close(fd);
                snprintf(resp, sizeof(resp),
                     "HTTP/1.1 201 Created\r\n\r\n");
                goto END_REQ;
            }

            int fd = open(filename, O_RDONLY);
            if (fd == -1)
            {
                snprintf(resp, sizeof(resp),
                         "HTTP/1.1 404 Not Found\r\n\r\n");
                goto END_REQ;
            }

            char out[1024];
            read(fd, out, sizeof(out));
            
            snprintf(resp, sizeof(resp),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: application/octet-stream\r\n"
                     "Content-Length: %lu\r\n"
                     "\r\n"
                     "%s",
                     strlen(out), out
                     );
        }
        else
        {
            snprintf(resp, sizeof(resp),
                     "HTTP/1.1 404 Not Found\r\n\r\n");
        }
        
END_REQ:
        write(client_sock, resp, strlen(resp));
        close(client_sock);
}   
    }
    return 0;
}