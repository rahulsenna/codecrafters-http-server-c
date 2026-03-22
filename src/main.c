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


typedef struct {
    const char *data;
    size_t len;
} string;

#ifndef __cplusplus
    static inline string _str_from_cstr(const char *s) {
        return (string){s, strlen(s)};
    }
    // STR() still useful for char* -> string conversion
    #define STR(x) _Generic((x),           \
        string:      (x),                  \
        char*:       _str_from_cstr(x),    \
        const char*: _str_from_cstr(x)     \
    )
#else
    static inline string STR(string s)      { return s; }
    static inline string STR(char *s)       { return {s, strlen(s)}; }
    static inline string STR(const char *s) { return {s, strlen(s)}; }
#endif

// STR_ARG never needs _Generic — always receives a string
#define STR_ARG(s)       (int)(s).len, (s).data

#ifdef DEBUG_BUILD
#define DBGSTR(x)        printf(#x ": %.*s\n", STR_ARG(x))
#define DBGSTRL(lbl, x)  printf(lbl ": %.*s\n", STR_ARG(x))
#else
#define DBGSTR(x)        do {} while (0)
#define DBGSTRL(lbl, x)  do {} while (0)
#endif

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
  CONNECTION,
  HEADER_COUNT
} HeaderType;

size_t gzip(string source, unsigned char* dest)
{

  z_stream strm = { 0 };
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = source.len;
  strm.next_in = (Bytef*) source.data;
  strm.avail_out = source.len + 128;
  strm.next_out = dest;

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

string directory = {};

void* handle_client(void* arg)
{
  int client_socket = *(int *)arg;

  char BUFFER[1024];
  char response[1024];

connection_begin:
  // Timeout ?
  // struct timeval tv;
  // tv.tv_sec = 5;
  // tv.tv_usec = 0;
  // setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  ssize_t bytes_read = read(client_socket, BUFFER, 1024);
  if (bytes_read <= 0)
  {
    perror("Read failed");
    close(client_socket);
    return NULL;
  }
  BUFFER[bytes_read] = 0;

  string header_info[HEADER_COUNT] = { 0 };
  
  char *cursor = BUFFER;
  char *end;
  // Method
  end = strchr(cursor, ' ');
  string method = { cursor, (size_t) (end - cursor) };
  cursor = end + 1;

  // Path
  end = strchr(cursor, ' ');
  string path = { cursor, (size_t) (end - cursor) };
  cursor = end + 1;

  // HTTP version
  end = strstr(cursor, "\r\n");
  string http_version = { cursor, (size_t) (end - cursor) };
  cursor = end + 2;  // skip \r\n

  // HOST
  end = strstr(cursor, "\r\n");
  string host = { cursor, (size_t) (end - cursor) };
  cursor = end + 2;  // skip \r\n

  DBGSTR(path);               // path: /index.html
  DBGSTR(host);               // cstr: hello
  DBGSTRL("method", method);  // method: GET

  char *headers_end = strstr(cursor, "\r\n\r\n");
  while (cursor < headers_end)
  {
    end = strstr(cursor, "\r\n");
    string header_line = { cursor, (size_t) (end - cursor) };
    cursor = end + 2;

    const char *sep = (const char *)memchr(header_line.data, ':', header_line.len);
    if (!sep) continue;

    string name  = {header_line.data, (size_t)(sep - header_line.data)};
    string value = {sep + 2, header_line.len - (size_t)(sep + 2 - header_line.data)};

    DBGSTR(name);
    DBGSTR(value);

    if (strncmp(name.data, "User-Agent", name.len) == 0)
      header_info[USER_AGENT] = value;
    else if (strncmp(name.data, "Accept", name.len) == 0)
      header_info[ACCEPT] = value;
    else if (strncmp(name.data, "Content-Type", name.len) == 0)
      header_info[CONTENT_TYPE] = value;
    else if (strncmp(name.data, "Content-Length", name.len) == 0)
      header_info[CONTENT_LENGTH] = value;
    else if (strncmp(name.data, "Accept-Encoding", name.len) == 0)
      header_info[ACCEPT_ENCODING] = value;
    else if (strncmp(name.data, "Connection", name.len) == 0)
      header_info[CONNECTION] = value;
  }

  char connection_header[30] = { 0 };
  if (header_info[CONNECTION].len && strncmp(header_info[CONNECTION].data, "close", strlen("close")) == 0)
  {
    snprintf(connection_header, sizeof(connection_header), "\r\nConnection: close");
  }

  if (path.len == 1)
  {
    snprintf(response, sizeof(response), "HTTP/1.1 200 OK%s\r\n\r\n", connection_header);
    write(client_socket, response, strlen(response));
  } else if (strncmp(path.data, "/user-agent", path.len) == 0)
  {
    snprintf(response, sizeof(response),
      "HTTP/1.1 200 OK%s\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%.*s",
      connection_header, header_info[USER_AGENT].len, STR_ARG(header_info[USER_AGENT]));

    write(client_socket, response, strlen(response));
  } else if (strncmp(path.data, "/echo/", strlen("/echo/")) == 0)
  {
    string content = {path.data+6, path.len-6};
    const char* encoding = header_info[ACCEPT_ENCODING].data &&
      strstr(header_info[ACCEPT_ENCODING].data, "gzip")
      ? (const char *)"Content-Encoding: gzip\r\n"
      : (const char *)"";
    if (strcmp(encoding, "Content-Encoding: gzip\r\n") == 0)
    {
      unsigned char compressed[1024];
      size_t compressd_len = gzip(content, compressed);

      snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK%s\r\n%sContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n",
        connection_header, encoding, compressd_len);
      write(client_socket, response, strlen(response));
      write(client_socket, compressed, compressd_len);

    } else
    {
      snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK%s\r\n%sContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%.*s",
        connection_header, encoding, content.len, STR_ARG(content));
      write(client_socket, response, strlen(response));
    }
  } else if (strncmp(path.data, "/files/", strlen("/files/")) == 0)
  {
    char filepath[256] = {};
    strncpy(filepath, directory.data, directory.len);
    strncpy(filepath + directory.len, path.data + strlen("/files/"), path.len - strlen("/files/"));    

    if (strncmp(method.data, "GET", method.len) == 0)
    {
      int file_ptr = open(filepath, O_RDONLY);
      if (file_ptr == -1)
      {
        snprintf(response, sizeof(response), "HTTP/1.1 404 Not Found%s\r\n\r\n", connection_header);
        write(client_socket, response, strlen(response));
        close(client_socket);
        return NULL;
      }

      char file_buffer[256];

      size_t file_size = read(file_ptr, file_buffer, 256);
      close(file_ptr);
      file_buffer[file_size] = 0;

      snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK%s\r\nContent-Type: application/octet-stream\r\nContent-Length: %lu\r\n\r\n%s",
        connection_header, (file_size), file_buffer);

      write(client_socket, response, strlen(response));
    } else if (strncmp(method.data, "POST", strlen("POST")) == 0)
    {
      int file_ptr = open(filepath, O_WRONLY | O_CREAT, 0644);
      if (file_ptr == -1)
      {
        close(file_ptr);
        perror("Failed to open file");
        write(client_socket, "500 Internal Server Error\r\n\r\n", strlen("500 Internal Server Error\r\n\r\n"));
        return NULL;
      }
      ssize_t bytes_written = write(file_ptr, headers_end+4, strlen(headers_end+4));
      if (bytes_written == -1)
      {
        perror("Write failed");
        close(file_ptr);
        write(client_socket, "500 Internal Server Error\r\n\r\n", strlen("500 Internal Server Error\r\n\r\n"));
        return NULL;
      }
      close(file_ptr);
      snprintf(response, sizeof(response), "HTTP/1.1 201 Created%s\r\n\r\n", connection_header);
      write(client_socket, response, strlen(response));
    }

  } else
  {
    snprintf(response, sizeof(response), "HTTP/1.1 404 Not Found%s\r\n\r\n", connection_header);
    write(client_socket, response, strlen(response));
  }


  if (header_info[CONNECTION].data && strncmp(header_info[CONNECTION].data, "close", strlen("close")) == 0)
  {
    close(client_socket);
    return NULL;
  }    
  goto connection_begin;
}

#define MAX_CONNECTIONS 10

int main(int argc, char* argv[])
{
  if (argc == 3)
  {
    if (strcmp(argv[1], "--directory") == 0) 
    	directory = STR(argv[2]);
  }
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

  if (bind(server_fd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) != 0)
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

  while (1)
  {
    printf("Waiting for a client to connect...\n");
    client_addr_len = sizeof(client_addr);

    client_socket[thread_count] = accept(server_fd, (struct sockaddr*) &client_addr, &client_addr_len);

    if (client_socket[thread_count] == -1)
    {
      printf("Accept failed");
      continue;
    }
    // Create thread for client
    if (pthread_create(&threads[thread_count], NULL, handle_client, &client_socket[thread_count]) != 0)
    {
      printf("Thread creation failed");
      close(client_socket[thread_count]);
    }
    thread_count = (thread_count + 1) % MAX_CONNECTIONS;
  }


  close(server_fd);

  return 0;
}
