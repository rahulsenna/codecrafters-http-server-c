#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

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

	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0)
	{
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}

	char BUFFER[1024];
	char response[1024];
	int client_socket;
	while(1)
	{
		printf("Waiting for a client to connect...\n");
		client_addr_len = sizeof(client_addr);

		client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);

		if (client_socket == -1)
		{
			perror("Accept failed");
			continue;
		}
		printf("Client connected\n");
		size_t bytes_read = read(client_socket, BUFFER, 1024);

		if (bytes_read < 0)
		{
			perror("Read failed");
			close(client_socket);
			continue;
		}
		BUFFER[bytes_read] = 0;
		// printf("BUFFER: %s\n", BUFFER);

        char *method       = strtok(BUFFER, " ");
        char *path         = strtok(0, " ");
        char *http_version = strtok(0, "\r\n");
        char *host         = strtok(0, "\r\n");
        char *user_agent   = strtok(0, "\r\n");
		if (user_agent)
			user_agent += strlen("User-Agent: ");

        printf("method: %s\n", method);
		printf("path: %s\n", path);
		printf("http_version: %s\n", http_version);
		printf("host: %s\n", host);
		printf("user_agent: %s\n", user_agent);
        if (strlen(path) == 1)
        {
            write(client_socket, "HTTP/1.1 200 OK\r\n\r\n", strlen("HTTP/1.1 200 OK\r\n\r\n"));
        } else if (strncmp(path, "/user-agent", 11) == 0)
        {
			snprintf(response, sizeof(response),
			"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %lu\r\n\r\n%s",
			strlen(user_agent),user_agent);

            write(client_socket, response, strlen(response));		
		}
		else if (strncmp(path, "/echo/", 6) == 0)
        {
			
			char *arg = path+6;
			snprintf(response, sizeof(response),
			"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %lu\r\n\r\n%s",
			strlen(arg),arg);

            write(client_socket, response, strlen(response));
		}

		else
		{
            write(client_socket, "HTTP/1.1 404 Not Found\r\n\r\n", strlen("HTTP/1.1 404 Not Found\r\n\r\n"));
        }

		close(client_socket);
	}
	

	close(server_fd);

	return 0;
}
