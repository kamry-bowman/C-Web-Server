/**
 * webserver.c -- A webserver written in C
 * 
 * Test with curl (if you don't have it, install it):
 * 
 *    curl -D - http://localhost:3490/
 *    curl -D - http://localhost:3490/d20
 *    curl -D - http://localhost:3490/date
 * 
 * You can also test the above URLs in your browser! They should work!
 * 
 * Posting Data:
 * 
 *    curl -D - -X POST -H 'Content-Type: text/plain' -d 'Hello, sample data!' http://localhost:3490/save
 * 
 * (Posting data is harder to test from a browser.)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/file.h>
#include <fcntl.h>
#include "net.h"
#include "file.h"
#include "mime.h"
#include "cache.h"
#include <sys/stat.h>

#define PORT "3490"  // the port users will be connecting to

#define SERVER_FILES "./serverfiles"
#define SERVER_ROOT "./serverroot"

/**
 * Send an HTTP response
 *
 * header:       "HTTP/1.1 404 NOT FOUND" or "HTTP/1.1 200 OK", etc.
 * content_type: "text/plain", etc.
 * body:         the data to send.
 * 
 * Return the value from the send() function.
 */
int send_response(int fd, char *header, char *content_type, void *body, int content_length)
{
    const int max_response_size = 262144;
    char response[max_response_size];

    // get current time
    time_t rawtime;
    time(&rawtime);

    // Build HTTP response and store it in response
    int header_length = sprintf(response,
      "%s\r\n"
      "Connection: close\r\n"
      "Content-Length: %d\r\n"
      "Content-Type: %s\r\n"
      "Date: %s\r\n",
      header, content_length, content_type, asctime(localtime(&rawtime)));
    
    memcpy(response + header_length, body, content_length);

    int rv = send(fd, response, header_length + content_length, 0);

    if (rv < 0) {
        perror("send");
    }

    return rv;
}


/**
 * Send a /d20 endpoint response
 */
void get_d20(int fd)
{
    // Generate a random number between 1 and 20 inclusive
    srand(time(NULL)); 
    int r = rand() % 21;
    char num_str[3];
    sprintf(num_str, "%d", r);
    send_response(fd, "HTTP/1.1 200 OK", "text/plain", num_str, (int) strlen(num_str));
}

/**
 * Send a 404 response
 */
void resp_404(int fd)
{
    char filepath[4096];
    struct file_data *filedata; 
    char *mime_type;

    // Fetch the 404.html file
    snprintf(filepath, sizeof filepath, "%s/404.html", SERVER_FILES);
    filedata = file_load(filepath);

    if (filedata == NULL) {
        // TODO: make this non-fatal
        fprintf(stderr, "cannot find system 404 file\n");
        exit(3);
    }

    mime_type = mime_type_get(filepath);

    send_response(fd, "HTTP/1.1 404 NOT FOUND", mime_type, filedata->data, filedata->size);

    file_free(filedata);
}

int is_regular_file(const char *path)
{
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}

/**
 * Read and return a file from disk or cache
 */
void get_file(int fd, struct cache *cache, char *request_path)
{   
    // first, check cache
    struct cache_entry * entry = cache_get(cache, request_path);
    if (entry) {
        send_response(fd, "HTTP/1.1 200 OK", entry->content_type, entry->content, entry->content_length);
    } else {
        char * root = "serverroot";
        int full_path_len = strlen(root) + strlen(request_path);
        char full_path[full_path_len + 1];
        strcpy(full_path, root);
        strcat(full_path, request_path);
        printf("full_path: %s\n", full_path);
        FILE *fp;
        char * content_type;
        if (!is_regular_file(full_path)) {
            char * index;
            if (full_path[full_path_len] == '/') {
                index = "index.html";
            } else {
                index = "/index.html";
            }
            char alt_path[full_path_len + strlen(index) + 1];
            strcpy(alt_path, full_path);
            strcat(alt_path, index);
            fp = fopen(alt_path, "r");
            content_type = mime_type_get(alt_path);
        } else {
            fp = fopen(full_path, "r");
            content_type = mime_type_get(full_path);
            }
        if (fp) {
            fseek(fp, 0, SEEK_END);
            int fsize = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            char * buff = malloc(fsize);
            fread(buff, 1, fsize, fp);
            fclose(fp);
            printf("size: %d, buffer: %s\n", fsize, buff);
            send_response(fd, "HTTP/1.1 200 OK", content_type, buff, fsize);
            cache_put(cache, request_path, content_type, buff, fsize);
        } else {
            resp_404(fd);
        }
    }
}

/**
 * Read and return a file from disk or cache
 */
int save_file(char *request_path, char* body, int content_length)
{   
    char * root = "serverroot";
    int full_path_len = strlen(root) + strlen(request_path);
    char full_path[full_path_len + 1];
    strcpy(full_path, root);
    strcat(full_path, request_path);

    int f = open(full_path, O_WRONLY | O_CREAT);
    if (f < 0) {
        return 1;
    } else {
        int res = write(f, body, content_length);
        close(f);
        if (res < 0) {
            return 1;
        } else {
            return 0;
        }
    }
}

/**
 * Search for the end of the HTTP header
 * 
 * "Newlines" in HTTP can be \r\n (carriage return followed by newline) or \n
 * (newline) or \r (carriage return).
 */
char *find_start_of_body(char *header)
{
    char *ptr = header;
    int found = 0;
    while (*ptr != 0) {
        int lf2 = (ptr[0] == '\n' && ptr[1] == '\n');
        int crlf2 = (ptr[0] == '\r' && ptr[1] == '\n' && ptr[2] == '\r' && ptr[3] == '\n');

        if (lf2) {
            found = 1;
            ptr+=2;
            break;
        }
        if (crlf2) {
            found = 1;
            ptr+=4;
            break;
        }
        ptr++;
    }
    if (found) {
        return ptr;
    }
    return NULL;
}

int find_content_length(char * req) {
    int i;
    char * header = strstr(req, "Content-Length: ");
    sscanf(header, "Content-Length: %d", &i);
    return i;
}


/**
 * Handle HTTP request and send response
 */
void handle_http_request(int fd, struct cache *cache)
{
    const int request_buffer_size = 65536; // 64K
    char request[request_buffer_size];

    // Read request
    int bytes_recvd = recv(fd, request, request_buffer_size - 1, 0);

    if (bytes_recvd < 0) {
        perror("recv");
        return;
    }

    // GET /example HTTP/1.1
    // Host: lambdaschool.com
    char method[5];
    char path[1024];
    char host[1024];
    // printf("received: %s", request);
    sscanf(request, "%s %s HTTP/1.1\r\n Host: %s", method, path, host);
    // printf("method: %s\n", method);
    // printf("path: %s\n", path);
    // printf("host: %s\n", host);

    // Read the three components of the first request line

    // If GET, handle the get endpoints
    if (strcmp(method, "GET") == 0) {
      if (strcmp(path, "/d20") == 0) {
        get_d20(fd);
      } else {
        get_file(fd, cache, path);
      }
    } else if (strcmp(method, "POST") == 0) {
        if (save_file(path, find_start_of_body(request), find_content_length(request)) == 0) {
            // success
            char * message = "Created file";
            send_response(fd, "201 Created", "text/plain", message, strlen(message));
        } else {
            // error
            char * message = "Creation failed.";
            send_response(fd, "500 Internal Server Error", "text/plain", message, strlen(message));
        }
    }
}

/**
 * Main
 */
int main(void)
{
    int newfd;  // listen on sock_fd, new connection on newfd
    struct sockaddr_storage their_addr; // connector's address information
    char s[INET6_ADDRSTRLEN];

    struct cache *cache = cache_create(10, 0);

    // Get a listening socket
    int listenfd = get_listener_socket(PORT);

    if (listenfd < 0) {
        fprintf(stderr, "webserver: fatal error getting listening socket\n");
        exit(1);
    }

    printf("webserver: waiting for connections on port %s...\n", PORT);

    // This is the main loop that accepts incoming connections and
    // forks a handler process to take care of it. The main parent
    // process then goes back to waiting for new connections.
    
    while(1) {
        socklen_t sin_size = sizeof their_addr;

        // Parent process will block on the accept() call until someone
        // makes a new connection:
        newfd = accept(listenfd, (struct sockaddr *)&their_addr, &sin_size);
        if (newfd == -1) {
            perror("accept");
            continue;
        }

        // Print out a message that we got the connection
        inet_ntop(their_addr.ss_family,
            get_in_addr((struct sockaddr *)&their_addr),
            s, sizeof s);
        printf("server: got connection from %s\n", s);
        
        // newfd is a new socket descriptor for the new connection.
        // listenfd is still listening for new connections.

        handle_http_request(newfd, cache);

        close(newfd);
    }

    // Unreachable code

    return 0;
}

