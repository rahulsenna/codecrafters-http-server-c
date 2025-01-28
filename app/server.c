#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <zlib.h>

typedef enum
{
    METHOD = 0x0,
    PATH,
    HTTP_VER,
    HOST,
    USER_AGENT,
    ACCEPT,
    CONTENT_TYPE,
    CONTENT_LENGTH,
    ACCEPT_ENCODING,
    HEADER_COUNT
} HeaderType;

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

void *handle_client(int *client_socket)
{
    char BUFFER[1024];
    char response[1024];

    printf("Client connected\n");
    size_t bytes_read = read(*client_socket, BUFFER, 1024);

    if (bytes_read <= 0)
    {
        perror("Read failed");
        close(*client_socket);
        return NULL;
    }
    BUFFER[bytes_read] = 0;
    printf("BUFFER: %s\n", BUFFER);

	char *header_info[HEADER_COUNT] = {NULL};
	char *saveptr = NULL;
    char *line;

    char *method       = strtok_r(BUFFER, " ", &saveptr);
    char *path         = strtok_r(0, " ", &saveptr);
    char *http_version = strtok_r(0, "\r\n", &saveptr);
    char *host         = strtok_r(0, "\r\n", &saveptr);

    char *last_line;
    size_t header_len;
    // Safely tokenize the headers
    while ((line = strtok_r(0, "\r\n", &saveptr)) != NULL)
    {
        // User-Agent header
        if (strncmp(line, "User-Agent: ", header_len = strlen("User-Agent: ")) == 0)        
            header_info[USER_AGENT] = line + header_len;
        // Accept header
        else if (strncmp(line, "Accept: ", header_len = strlen("Accept: ")) == 0)
            header_info[ACCEPT] = line + header_len;
        // Content-Type header
        else if (strncmp(line, "Content-Type: ", header_len = strlen("Content-Type: ")) == 0)
            header_info[CONTENT_TYPE] = line + header_len;
        // Content-Length header
        else if (strncmp(line, "Content-Length: ", header_len = strlen("Content-Length: ")) == 0)
            header_info[CONTENT_LENGTH] = line + header_len;
		else if (strncmp(line, "Accept-Encoding: ", header_len = strlen("Accept-Encoding: ")) == 0)
            header_info[ACCEPT_ENCODING] = line + header_len;

        // printf("Parsed line: %s\n", line);
		last_line = line;
    }
	printf("last_line: %s\n", last_line);

    // printf("client_socket: %d\nmethod: %s\npath: %s\nhttp_version: %s\nhost: %s\nuser_agent: %s\n",
        //    *client_socket, method, path, http_version, host, user_agent);

    if (strlen(path) == 1)
    {
        write(*client_socket, "HTTP/1.1 200 OK\r\n\r\n", strlen("HTTP/1.1 200 OK\r\n\r\n"));
    } else if (strncmp(path, "/user-agent", 11) == 0)
    {
        snprintf(response, sizeof(response),
                 "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %lu\r\n\r\n%s",
                 strlen(header_info[USER_AGENT]), header_info[USER_AGENT]);

        write(*client_socket, response, strlen(response));
    } else if (strncmp(path, "/echo/", 6) == 0)
    {
        char *encoding = header_info[ACCEPT_ENCODING] &&
                         strstr(header_info[ACCEPT_ENCODING], "gzip")
                         ? "Content-Encoding: gzip\r\n"
                         : "";
        char *arg = path + 6;
		if (strcmp(encoding, "Content-Encoding: gzip\r\n") == 0)
		{
			unsigned char compressed[1024];
			size_t compressd_len = gzip(arg, compressed);
			
            snprintf(response, sizeof(response),
                 "HTTP/1.1 200 OK\r\n%sContent-Type: text/plain\r\nContent-Length: %lu\r\n\r\n",
                 encoding,compressd_len);
            write(*client_socket, response, strlen(response));
            write(*client_socket, compressed, compressd_len);

        } else
        {
            snprintf(response, sizeof(response),
                 "HTTP/1.1 200 OK\r\n%sContent-Type: text/plain\r\nContent-Length: %lu\r\n\r\n%s",
                 encoding,strlen(arg), arg);
            write(*client_socket, response, strlen(response));
        }
    } else if (strncmp(path, "/files/", 7) == 0)
    {
        char *filename = path + 7;
		char filepath[256] = "/tmp/data/codecrafters.io/http-server-tester/";
		strcat(filepath, filename);

        if (strcmp(method, "GET") == 0)
        {
            int file_ptr = open(filepath, O_RDONLY);
            if (file_ptr == -1)
            {
                write(*client_socket, "HTTP/1.1 404 Not Found\r\n\r\n", strlen("HTTP/1.1 404 Not Found\r\n\r\n"));
                close(*client_socket);
                return NULL;
            }

            char file_buffer[1024 * 4];

            size_t file_size = read(file_ptr, file_buffer, 1024 * 4);
            close(file_ptr);
            file_buffer[file_size] = 0;

            snprintf(response, sizeof(response),
                     "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %lu\r\n\r\n%s",
                     (file_size), file_buffer);

            write(*client_socket, response, strlen(response));
        } else if (strcmp(method, "POST") == 0)
        {
			printf("POST METHOD: %s\n", method);
			printf("filepath: %s\n", filepath);
            int file_ptr = open(filepath, O_WRONLY | O_CREAT, 0644);
            if (file_ptr == -1)
            {
				close(file_ptr);
                perror("Failed to open file");
    	        write(*client_socket, "500 Internal Server Error\r\n\r\n", strlen("500 Internal Server Error\r\n\r\n"));
                return NULL;
            }
            ssize_t bytes_written =write(file_ptr, last_line, strlen(last_line));
            if (bytes_written == -1)
            {
                perror("Write failed");
                close(file_ptr);
    	        write(*client_socket, "500 Internal Server Error\r\n\r\n", strlen("500 Internal Server Error\r\n\r\n"));
                return NULL;
            }
            close(file_ptr);
            write(*client_socket, "HTTP/1.1 201 Created\r\n\r\n", strlen("HTTP/1.1 201 Created\r\n\r\n"));
		}

    } else
    {
        write(*client_socket, "HTTP/1.1 404 Not Found\r\n\r\n", strlen("HTTP/1.1 404 Not Found\r\n\r\n"));
    }

    close(*client_socket);
	return NULL;
}

#define MAX_CONNECTIONS 10

int main()
{
	// Disable output buffering
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	// You can use print statements as follows for debugging, they'll be visible when running tests.
	printf("Logs from your program will appear here!\n");

	// Uncomment this block to pass the first stage

	int server_fd;
	socklen_t client_addr_len;
	struct sockaddr_in client_addr;

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1)
	{
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}

	// Since the tester restarts your program quite often, setting SO_REUSEADDR
	// ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
	{
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	}

	struct sockaddr_in serv_addr =
	{
		.sin_family = AF_INET,
		.sin_port = htons(4221),
		.sin_addr = {htonl(INADDR_ANY)},
	};

	if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0)
	{
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}

	if (listen(server_fd, MAX_CONNECTIONS) != 0)
	{
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}

	int client_socket[MAX_CONNECTIONS];
	pthread_t threads[MAX_CONNECTIONS];
    int thread_count = 0;

	while(1)
	{
		printf("Waiting for a client to connect...\n");
		client_addr_len = sizeof(client_addr);

		client_socket[thread_count] = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

		if (client_socket[thread_count] == -1)
		{
			printf("Accept failed");
			continue;
		}
		 // Create thread for client
        if (pthread_create(&threads[thread_count], NULL, (void *) handle_client, &client_socket[thread_count]) != 0)
        {
            printf("Thread creation failed");
            close(client_socket[thread_count]);
        }
        thread_count = (thread_count + 1) % MAX_CONNECTIONS;		
	}
	

	close(server_fd);

	return 0;
}
