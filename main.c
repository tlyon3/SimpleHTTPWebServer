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
#include <semaphore.h>
#include <pthread.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/epoll.h>

#include "create_server_socket.h"
#include "queue.h"

#define BUFFER_MAX 1024
#define DEFAULT_PORT "8080"
#define DEFAULT_CONFIG "http.conf"
#define THREAD_COUNT_DEFAULT 8
#define MAX_QUEUE_SIZE_DEFAULT 10
#define MAX_EVENTS 100

typedef struct client {
    int fd; /* socket descriptor for connection */
    socklen_t addr_len;
    struct sockaddr_storage storage;
    /*... many other members omitted for brevity */
    /* you can, and probably will, add to this */
} client_t;

void serve_client(int sock, struct client* client_info, int epoll_fd);

void handle_request(char *request, ssize_t request_len, int sock);

int isPost(char *);

int isGet(char *);

int isHead(char *);

void consume(void *args);

void checkOS();

int isOnMac;
int cont = 1;
int verbose = 0;
char *dir = "./www";
//char debugSting[BUFFER_MAX];
char *notFound404 = "<html>\n<head>\n<title>404 Page Not Found</title>\n<body>\n\n<H2>404: Page not found</H2>\n\n</body></html>";
char *badRequest400 = "<html>\n<head>\n<title>400 Bad Request</title>\n<body>\n\n<H2>400: Received Bad Request</H2>\n\n</body></html>";
char *notImplemented500 = "<html>\n<head>\n<title>501 Not Implemented</title>\n<body>\n\n<H2>501: Method not implemented</H2>\n\n</body></html>";
char *forbidden403 = "<html>\n<head>\n<title>403 Forbidden</title>\n<body>\n\n<H2>403: File is forbidden</H2>\n\n</body></html>";
char *internalError500 = "<html>\n<head>\n<title>500 Internal Server Error</title>\n<body>\n\n<H2>500: There was an error while serving the client</H2>\n\n</body></html>";

struct thread_info *threadPool;
pthread_mutex_t mutex;
//todo: rename
sem_t openQueueSpot;
sem_t clientsInQueue;
struct queue queue1;
int thread_count = THREAD_COUNT_DEFAULT;
int epollfd;
struct server {
    int fd;
};

struct thread_info {
    pthread_t *thread;
    int epoll_fd;
};

int set_blocking(int sock, int blocking) {
    int flags;
    /* Get flags for socket */
    if ((flags = fcntl(sock, F_GETFL)) == -1) {
        perror("fcntl get");
        exit(EXIT_FAILURE);
    }
    /* Only change flags if they're not what we want */
    if (blocking && (flags & O_NONBLOCK)) {
        if (fcntl(sock, F_SETFL, flags & ~O_NONBLOCK) == -1) {
            perror("fcntl set block");
            exit(EXIT_FAILURE);
        }
        return 0;
    }
    /* Only change flags if they're not what we want */
    if (!blocking && !(flags & O_NONBLOCK)) {
        if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
            perror("fcntl set nonblock");
            exit(EXIT_FAILURE);
        }
        return 0;
    }
    return 0;
}

struct client *get_new_client(int sock) {
    struct sockaddr_storage addr;
    socklen_t add_len = sizeof(struct sockaddr_storage);
    int new_fd = accept(sock, (struct sockaddr *) &addr, &add_len);
    if (new_fd == -1) {
        perror("accept");
        return NULL;
    }
    set_blocking(new_fd, 0);
    struct client *client = (struct client *) calloc(1, sizeof(struct client));
    client->fd = new_fd;
    client->storage = addr;
    client->addr_len = add_len;
    if (verbose) printf("got connection\n");
    return client;
}

void prepend(char *s, const char *t) {
    size_t len = strlen(t);
    size_t i;

    memmove(s + len, s, strlen(s) + 1);

    for (i = 0; i < len; ++i) {
        s[i] = t[i];
    }
}

void clean_up_memory() {
    if (verbose) printf("Cleaning up memory\n");
    struct node *toFree = NULL;
    //free the queue
    while ((toFree = dequeue(&queue1)) != NULL) {
        close(toFree->clientFD);
    }
    deconstructQueue(&queue1);
    if (verbose) printf("Freeing thread pool: count = %d\n", thread_count);
    for (int i = 0; i < thread_count; i++) {
        pthread_kill(threadPool[i].thread, SIGINT);
    }
    for (int i = 0; i < thread_count; i++) {
        pthread_join(threadPool[i].thread, NULL);
    }
    free(threadPool);
    exit(0);
}

void sig_int(int sig) {
    if (verbose) printf("Received signal: %d\n", sig);
    cont = 0;
}

void verbosePrintf(char *s) {
    if (verbose) {
        printf("%s", s);
    }
}

void usage(char *name) {
    printf("Usage: %s [-v] [-p port] [-c config-file]\n", name);
    printf("Example:\n");
    printf("\t%s -v -p 8080 -c http.conf \n", name);
    return;
}

int main(int argc, char *argv[]) {

    char *port = NULL;
    char *config_path = NULL;
    thread_count = THREAD_COUNT_DEFAULT;
    int max_queue_size = MAX_QUEUE_SIZE_DEFAULT;

    port = DEFAULT_PORT;
    config_path = DEFAULT_CONFIG;

    int c;
    while ((c = getopt(argc, argv, "vp:c:t:q:")) != -1) {
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
            case 't':
                thread_count = atoi(optarg);
                break;
            case 'q':
                max_queue_size = atoi(optarg);
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

    queue1 = newQueue(max_queue_size);

    //setup sighandler for safe clean up
    struct sigaction sa;
    sa.sa_handler = &sig_int;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, 0) == -1) {
        perror(0);
        exit(1);
    }

    //create thread pool
    pthread_mutex_init(&mutex, NULL);
    sem_init(&openQueueSpot, 0, max_queue_size);
    sem_init(&clientsInQueue, 0, 0);
    threadPool = malloc(thread_count * sizeof(pthread_t));
    for (int i = 0; i < thread_count; i++) {
        threadPool[i].epoll_fd = epoll_create1(0);
        if (threadPool[i].epoll_fd == -1) {
            perror("epoll_create1");
            exit(EXIT_FAILURE);
        } else {
            threadPool[i].thread = (pthread_t *) malloc(sizeof(pthread_t));
            if ((pthread_create(threadPool[i].thread, NULL, consume, (void *) &threadPool[i])) == 0) {
                perror("pthread_create");
            }
        }
    }

    //different behavior based on operating system
    checkOS();

    printf("Starting up server...\n");
    printf("Running on port: %s\n", port);

    int sock = create_server_socket(port, SOCK_STREAM);
    if (verbose) printf("Created socket: %d\n", sock);
    cont = 1;

    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        perror("epoll_create");
    }

    set_blocking(sock, 0);

    int nfds;
    struct epoll_event ev;
    struct epoll_event events[MAX_EVENTS];

    ev.events = EPOLLIN;
    struct server server = {.fd = sock};
    ev.data.ptr = (void *) &server;

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sock, &ev) == -1) {
        perror("epoll_ctl");
        exit(EXIT_FAILURE);
    }

    int currentThread = 0;
    //producer
    while (cont) {
       nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if(nfds == -1){
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }
        struct sockaddr_storage client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(sock, (struct sockaddr*) &client_addr, &addr_len);
        if(client_sock == -1){
            if(errno == EINTR){
                if(cont == 0){
                    break;
                }
            }
            perror("accept");
            continue;
        }
        set_blocking(client_sock, 0);
        int thread_id = currentThread;
        currentThread++;
        if(currentThread >= thread_count){
            currentThread = 0;
        }
        ev.events = EPOLLIN;
        ev.data.fd = client_sock;

        struct client* client_info1 = malloc(sizeof(struct client));
        client_info1->fd = client_sock;
        client_info1->storage = client_addr;
        client_info1->addr_len = addr_len;
        ev.data.ptr = (void*) client_info1;

        if(epoll_ctl(threadPool[thread_id].epoll_fd, EPOLL_CTL_ADD, client_sock, &ev) == -1){
            perror("epoll_ctl");
            exit(EXIT_FAILURE);
        }
    }
    verbosePrintf("Shutting down server\n");
    clean_up_memory();
}

//consumer
void consume(void *args) {
    int epoll_fd = -1;
    struct epoll_event events[MAX_EVENTS];
    int nfds = -1;
    struct thread_info *thread = (struct thread_info *) args;
    while (cont) {
        nfds = epoll_wait(thread->epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < nfds; i++) {
            struct client* client_info = (struct client*)events[i].data.ptr;
            serve_client(client_info->fd, (struct client*)events[i].data.ptr, thread->epoll_fd);
        }
    }
}

void serve_client(int sock, struct client* client_info, int epoll_fd) {
    char buffer[BUFFER_MAX];
    char client_hostname[NI_MAXHOST];
    char client_port[NI_MAXSERV];
    struct sockaddr_storage client_addr = client_info->storage;
    socklen_t addr_len = client_info->addr_len;
    int ret = getnameinfo((struct sockaddr *) &client_addr, addr_len, client_hostname, NI_MAXHOST, client_port,
                          NI_MAXSERV, 0);
    if (ret != 0) {
        printf("ERROR: error getting name info\n");
    }
    if (verbose) printf("Connected to: %s:%s\n", client_hostname, client_port);
    //serve client
    ssize_t bytes_read = 0;
    ssize_t total_bytes_read = 0;
    while (cont) {
        bytes_read = recv(sock, buffer + total_bytes_read, BUFFER_MAX, 0);
        total_bytes_read += bytes_read;
        if (bytes_read == 0) {
            if (verbose) printf("Disconnected from %s:%s\n", client_hostname, client_port);
            close(sock);
            return;
        } else if (bytes_read < 0) {
            //error in recv
            if (errno == EINTR) {
                if (cont == 0) {
                    if (verbose) printf("Exiting serve_client\n");
                    break;
                }
            } else if (errno == EPIPE) {
                if (verbose) printf("ERROR: error in receiving\n");
                char currentTime[80];
                struct tm *currentTimeInfo;
                time_t rawTime = 0;
                currentTimeInfo = localtime(&rawTime);
                strftime(currentTime, 80, "%a, %d %b %Y %H:%M:%S %Z", currentTimeInfo);
                char header[BUFFER_MAX];
                sprintf(header,
                        "HTTP/1.1 500 Internal Server Error\r\n"
                                "Server: CS360 Server\r\n"
                                "Date: %s\r\n"
                                "Content-Type: text/html\r\n"
                                "Content-Length: %lu\r\n\r\n", currentTime, strlen(internalError500));
                send(sock, header, strlen(header), 0);
                send(sock, internalError500, strlen(internalError500), 0);
                total_bytes_read = 0;
                memset(buffer, 0, BUFFER_MAX);
            }
        } else if (strstr(buffer, "\r\n\r\n")) {
            //get first request
//            buffer[total_bytes_read] = '\0';
            handle_request(buffer, bytes_read, sock);
            total_bytes_read = 0;
            memset(buffer, 0, BUFFER_MAX);
            continue;
        }
        close(sock);
        return;
    }
    close(sock);
}

void checkOS() {
    struct utsname unameData;
    uname(&unameData);
    printf("OS: %s\n", unameData.sysname);
    if (strcmp(unameData.sysname, "Darwin") == 0) {
        printf("Is running on macOS\n");
        isOnMac = 1;
    }
}

//handle single request
void handle_request(char *request, ssize_t request_len, int sock) {
    //parse request
    verbosePrintf("REQUEST: \n");
    verbosePrintf(request);

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
            fp = fopen(location, "r");
            if (fp == NULL) {
                printf("Error opening file\n");
            }
            if ((sb.st_mode & S_IRUSR) <= 0) {
                //incorrect permissions
                //send back 403
                char currentTime[80];
                struct tm *currentTimeInfo;
                time_t rawTime = 0;
                currentTimeInfo = localtime(&rawTime);
                strftime(currentTime, 80, "%a, %d %b %Y %H:%M:%S %Z", currentTimeInfo);
                char header[BUFFER_MAX];
                printf("FORBIDDEN\n");
                sprintf(header,
                        "HTTP/1.1 403 Forbidden\r\n"
                                "Server: CS360 Server\r\n"
                                "Date: %s\r\n"
                                "Content-Type: text/html\r\n"
                                "Content-Length: %lu\r\n\r\n", currentTime, strlen(forbidden403));
                send(sock, header, strlen(header), 0);
                send(sock, forbidden403, strlen(forbidden403), 0);
                return;
            }
            //todo: there is a memory leak somewhere in here vv
            //get MIME file type
            char contentType[16] = "n";
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
            time_t rawTime = 0;
            currentTimeInfo = localtime(&rawTime);
            strftime(currentTime, 80, "%a, %d %b %Y %H:%M:%S %Z", currentTimeInfo);

            //build header
            char header[BUFFER_MAX];
            sprintf(header,
                    "HTTP/1.1 200 OK\r\n"
                            "Date: %s\r\n"
                            "Server: CS360-Server\r\n"
                            "Content-Type: %s\r\n"
                            "Content-Length: %lu\r\n"
                            "Last-Modified: %s\r\n\r\n",
                    currentTime, mimeType, size, lastMTime);

            verbosePrintf("RESPONSE HEADER: \n");
            verbosePrintf(header);

            //send header
            send(sock, header, strlen(header), 0);
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
            if (verbose) printf("Closing file pointer\n");
            if (fclose(fp) == -1) {
                printf("Error closing file: %s\n", strerror(errno));
            }
            //todo: there is a memory leak somewhere in here ^^
            return;
        } else {
            char currentTime[80];
            struct tm *currentTimeInfo;
            time_t rawTime = 0;
            currentTimeInfo = localtime(&rawTime);
            strftime(currentTime, 80, "%a, %d %b %Y %H:%M:%S %Z", currentTimeInfo);
            //file not found
            //send back 404
            char header[1024];
            sprintf(header, "HTTP/1.1 404 Not Found\r\n"
                    "Date: %s\r\n"
                    "Content-Length: %lu\r\n"
                    "Content-Type: text/html\r\n"
                    "Server: CS360-Server\r\n\r\n", currentTime, strlen(notFound404));
            verbosePrintf(header);
            send(sock, header, strlen(header), 0);
            //send back html for 404
            send(sock, notFound404, strlen(notFound404), 0);
        }

    } else if (isPost(type)) {
        char currentTime[80];
        struct tm *currentTimeInfo;
        time_t rawTime = 0;
        currentTimeInfo = localtime(&rawTime);
        strftime(currentTime, 80, "%a, %d %b %Y %H:%M:%S %Z", currentTimeInfo);
        //Not implemented
        //send back error code 501
        char header[1024];
        sprintf(header, "HTTP/1.1 501 Not Implemented\r\n"
                "Date: %s\r\n"
                "Content-Length: %lu\r\n"
                "Content-Type: text/html\r\n"
                "Server: CS360-Server\r\n\r\n", currentTime, strlen(notImplemented500));
        verbosePrintf(header);
        send(sock, header, strlen(header), 0);
        //send back html for 404
        send(sock, notImplemented500, strlen(notImplemented500), 0);
        return;
    } else if (isHead(type)) {
        char currentTime[80];
        struct tm *currentTimeInfo;
        time_t rawTime = 0;
        currentTimeInfo = localtime(&rawTime);
        strftime(currentTime, 80, "%a, %d %b %Y %H:%M:%S %Z", currentTimeInfo);
        //only need to send the header back
        //send back error code 501
        char header[1024];
        sprintf(header, "HTTP/1.1 501 Not Implemented\r\n"
                "Date: %s\r\n"
                "Content-Length: %lu\r\n"
                "Content-Type: text/html\r\n"
                "Server: CS360-Server\r\n\r\n", currentTime, strlen(notImplemented500));
        verbosePrintf(header);
        send(sock, header, strlen(header), 0);
        //send back html for 404
        send(sock, notImplemented500, strlen(notImplemented500), 0);
    } else {
        char currentTime[80];
        struct tm *currentTimeInfo;
        time_t rawTime = 0;
        currentTimeInfo = localtime(&rawTime);
        strftime(currentTime, 80, "%a, %d %b %Y %H:%M:%S %Z", currentTimeInfo);
        char header[1024];
        sprintf(header, "HTTP/1.1 400 Bad Request\r\n"
                "Date: %s\r\n"
                "Content-Type: text/html\r\n"
                "Content-Length: %lu\r\n"
                "Server: CS360-Server\r\n\r\n", currentTime, strlen(badRequest400));
        verbosePrintf(header);
        send(sock, header, strlen(header), 0);
        //send back html for 404
        send(sock, badRequest400, strlen(badRequest400), 0);
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
