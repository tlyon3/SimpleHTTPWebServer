#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include "create_server_socket.h"

int isOnMac;
#define BUFFER_MAX 1024
#define DEFAULT_PORT "8080"
#define DEFAULT_CONFIG "http.conf"

void serve_client(int client, struct sockaddr_storage client_addr, socklen_t addr_len);

void handle_request(char *request, size_t request_len, int sock);

int isPost(char *);

int isGet(char *);

int isHead(char *);

int verbose = 1;
char *dir = "../resources/www";

char debugSting[BUFFER_MAX];

char *notFound404 = "<html>\n<head>\n<title>404 Page Not Found</title>\n<body>\n\n<H2>404: Page not found</H2>\n\n</body></html>";
char *notFound400 = "<html>\n<head>\n<title>400 Bad Request</title>\n<body>\n\n<H2>400: Received Bad Request</H2>\n\n</body></html>";
char *notFound501 = "<html>\n<head>\n<title>501 Not Implemented</title>\n<body>\n\n<H2>501: Method not implemented</H2>\n\n</body></html>";
char *notFound403 = "<html>\n<head>\n<title>403 Forbidden</title>\n<body>\n\n<H2>403: File is forbidden</H2>\n\n</body></html>";


void prepend(char *s, const char *t) {
    size_t len = strlen(t);
    size_t i;

    memmove(s + len, s, strlen(s) + 1);

    for (i = 0; i < len; ++i) {
        s[i] = t[i];
    }
}

void handle_sigchld(int sig) {
    int saved_errno = errno;
    while (waitpid((pid_t) (-1), 0, WNOHANG) > 0) {}
    errno = saved_errno;
}

void verbosePrintf(char *s) {
    if (verbose) {
        printf("%s", s);
    }
}

void usage(char* name) {
    printf("Usage: %s [-v] [-p port] [-c config-file]\n", name);
    printf("Example:\n");
    printf("\t%s -v -p 8080 -c http.conf \n", name);
    return;
}

int main(int argc, char *argv[]) {
    char *port = NULL;
    char *config_path = NULL;
    port = DEFAULT_PORT;
    config_path = DEFAULT_CONFIG;
    int c;
    while ((c = getopt(argc, argv, "vp:c:")) != -1) {
        switch (c) {
            case 'v':
                verbose = 1;
                break;
            case 'p':
                port = optarg;
                break;
            case 'c':
                config_path = optarg;
                break;
            case '?':
                if (optopt == 'p' || optopt == 'c') {
                    fprintf(stderr, "Option -%c requires an argument\n", optopt);
                    usage(argv[0]);
                    exit(EXIT_FAILURE);
                }
            default:
                fprintf(stderr, "Unknown option encountered\n");
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    //different behavior based on operating system
    struct utsname unameData;
    uname(&unameData);
    printf("OS: %s\n", unameData.sysname);
    if (strcmp(unameData.sysname, "Darwin") == 0) {
        printf("Is running on macOS\n");
        isOnMac = 1;
    }
    struct sigaction sa;
    sa.sa_handler = &handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, 0) == -1) {
        perror(0);
        exit(1);
    }

    printf("Starting up server...\n");

    printf("Running on port: %s\n", port);
    int sock = create_server_socket(port, SOCK_STREAM);
    printf("Created socket: %d\n", sock);
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client = accept(sock, (struct sockaddr *) &client_addr, &client_addr_len);
        if (client == -1) {
            printf("ERROR: error in accept\n");
        } else {
            if (fork() == 0) {
                serve_client(client, client_addr, client_addr_len);
            }
            continue;
        }
        return 0;
    }
}

void serve_client(int sock, struct sockaddr_storage client_addr, socklen_t addr_len) {
    char buffer[BUFFER_MAX];
    char client_hostname[NI_MAXHOST];
    char client_port[NI_MAXSERV];
    int ret = getnameinfo((struct sockaddr *) &client_addr, addr_len, client_hostname, NI_MAXHOST, client_port,
                          NI_MAXSERV, 0);
    if (ret != 0) {
        printf("ERROR: error getting name info\n");
    }
    printf("Connected to: %s:%s\n", client_hostname, client_port);
    //serve client
    size_t bytes_read = 0;
    size_t total_bytes_read = 0;
    char newRequest[BUFFER_MAX];
    while (1) {
        bytes_read = recv(sock, buffer, BUFFER_MAX, 0);
        total_bytes_read += bytes_read;
        if (bytes_read == 0) {
            printf("Disconnected from %s:%s\n", client_hostname, client_port);
            close(sock);
            exit(0);
        } else if (bytes_read < 0) {
            //error in recv
            printf("ERROR: error in receiving\n");
            //todo: send back error code 500
        } else if (strstr(buffer, "\r\n\r\n")) {
            //todo: get first request
            buffer[total_bytes_read] = '\0';
            handle_request(buffer, bytes_read, sock);
            total_bytes_read = 0;
        } else {
            //todo: save what we have so far, wait for more
        }
    }
}

//handle single request
void handle_request(char *request, size_t request_len, int sock) {
    //parse request
    printf("Received request: %s", request);

    char *type;
    char *path;
    type = strtok(request, " ");
    path = strtok(NULL, " ");

    //check if valid request
    if (isGet(type)) {
        //get file in ./resources/path
        struct stat sb;
        char location[1024];
        //check if path is '/'
        if (strcmp(path, "/") == 0) {
            strcpy(location, "/index.html");
            prepend(location, dir);
        } else {
            strcpy(location, path);
            prepend(location, dir);
        }

        if (stat(location, &sb) != -1) {
            //found file
            //Check file permissions
            FILE *fp;
            if ((fp = fopen(location, "r")) == NULL) {
                printf("Error opening file\n");
            }
            if ((sb.st_mode & S_IRUSR) <= 0) {
                //incorrect permissions
                //send back 403
                char header[BUFFER_MAX];
                sprintf(header, "HTTP/1.1 403 Forbidden\r\nContent-Length: %d\r\n\r\n", sizeof(notFound403));
                send(sock, header, strlen(header), 0);
                send(sock, notFound403, strlen(notFound403), 0);
                return;
            }
            //get MIME file type
            char contentType[16];
            int beginCopying = 0;
            int j = 0;
            for (int i = 1; i < strlen(location); i++) {
                if (beginCopying) {
                    contentType[j] = location[i];
                    j++;
                    continue;
                }
                if (location[i] == '.') {
                    beginCopying = 1;
                }
            }

            char *mimeType;
            if (strstr(contentType, "pdf")) {
                mimeType = "application/pdf";
            } else if (strstr(contentType, "jpg")) {
                mimeType = "image/jpg";
            } else if (strstr(contentType, "gif")) {
                mimeType = "image/gif";
            } else if (strstr(contentType, "png")) {
                mimeType = "image/png";
            } else if (strstr(contentType, "html")) {
                mimeType = "text/html";
            } else {
                mimeType = "text/plain";
            }

            //get file size
            off_t size = sb.st_size;

            //get last modified time
            char lastMTime[80];
            struct tm *lmTimeInfo;
            time_t lastModified = sb.st_mtime;
            lmTimeInfo = localtime(&lastModified);
            strftime(lastMTime, 80, "%a, %d %b %Y %H:%M:%S %Z", lmTimeInfo);

            //get current time
            char currentTime[80];
            struct tm *currentTimeInfo;
            time_t rawTime;
            currentTimeInfo = localtime(&rawTime);
            strftime(currentTime, 80, "%a, %d %b %Y %H:%M:%S %Z", currentTimeInfo);

            //build header
            char header[BUFFER_MAX];
            sprintf(header,
                    "HTTP/1.1 200 OK\r\n"
                            "Date: %s\r\n"
                            "Server: CS360-Server\r\n"
                            "Content-Type: %s\r\n"
                            "Content-Length: %d\r\n"
                            "Last-Modified: %s\r\n\r\n",
                    currentTime, mimeType, size, lastMTime);

            verbosePrintf(header);

            //send header
            send(sock, header, strlen(header), NULL);

            //macOS. Works!
            if (isOnMac) {
                if (sendfile(fileno(fp), sock, 0, &size, NULL, 0) == -1) {
                    printf("Error sending file: %s\n", strerror(errno));
                }
            } else {
                //linux. Works!
                if (sendfile(sock, fileno(fp), 0, &size, NULL, 0) == -1) {
                    printf("Error sending file: %s\n", strerror(errno));
                }
            }
            close(fileno(fp));
            return;
        } else {
            char currentTime[80];
            struct tm *currentTimeInfo;
            time_t rawTime;
            currentTimeInfo = localtime(&rawTime);
            strftime(currentTime, 80, "%a, %d %b %Y %H:%M:%S %Z", currentTimeInfo);
            //file not found
            //send back 404
            char header[1024];
            sprintf(header, "HTTP/1.1 404 Not Found\r\n"
                    "Date: %s\r\n"
                    "Content-Length: %d\r\n"
                    "Server: CS360-Server\r\n\r\n", currentTime, strlen(notFound404));
            printf("sent header: %s\n", header);
            send(sock, header, strlen(header), 0);
            //send back html for 404
            send(sock, notFound404, strlen(notFound404), 0);
        }

    } else if (isPost(type)) {
        char currentTime[80];
        struct tm *currentTimeInfo;
        time_t rawTime;
        currentTimeInfo = localtime(&rawTime);
        strftime(currentTime, 80, "%a, %d %b %Y %H:%M:%S %Z", currentTimeInfo);
        //Not implemented
        //send back error code 501
        char header[1024];
        sprintf(header, "HTTP/1.1 501 Not Implemented\r\n"
                "Date: %s\r\n"
                "Content-Length: %d\r\n"
                "Server: CS360-Server\r\n\r\n", currentTime, strlen(notFound501));
        printf("sent header: %s\n", header);
        send(sock, header, strlen(header), 0);
        //send back html for 404
        send(sock, notFound501, strlen(notFound501), 0);
        return;
    } else if (isHead(type)) {
        char currentTime[80];
        struct tm *currentTimeInfo;
        time_t rawTime;
        currentTimeInfo = localtime(&rawTime);
        strftime(currentTime, 80, "%a, %d %b %Y %H:%M:%S %Z", currentTimeInfo);
        //only need to send the header back
        //send back error code 501
        char header[1024];
        sprintf(header, "HTTP/1.1 501 Not Implemented\r\n"
                "Date: %s\r\n"
                "Content-Length: %d\r\n"
                "Server: CS360-Server\r\n\r\n", currentTime, strlen(notFound501));
        printf("sent header: %s\n", header);
        send(sock, header, strlen(header), 0);
        //send back html for 404
        send(sock, notFound501, strlen(notFound501), 0);
    } else {
        char currentTime[80];
        struct tm *currentTimeInfo;
        time_t rawTime;
        currentTimeInfo = localtime(&rawTime);
        strftime(currentTime, 80, "%a, %d %b %Y %H:%M:%S %Z", currentTimeInfo);
        char header[1024];
        sprintf(header, "HTTP/1.1 400 Bad Request\r\n"
                "Date: %s\r\n"
                "Content-Length: %d\r\n"
                "Server: CS360-Server\r\n\r\n", currentTime, strlen(notFound400));
        printf("sent header: %s\n", header);
        send(sock, header, strlen(header), 0);
        //send back html for 404
        send(sock, notFound400, strlen(notFound400), 0);
        close(sock);
        exit(400);
    }
}

int isPost(char *request) {
    return request[0] == 'P' && request[1] == 'O' && request[2] == 'S' && request[3] == 'T';
}

int isHead(char *request) {
    return request[0] == 'H' && request[1] == 'E' && request[2] == 'A' && request[3] == 'D';
}

int isGet(char *request) {
    return request[0] == 'G' && request[1] == 'E' && request[2] == 'T';
}
