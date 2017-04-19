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
#include "list.h"

#define BUFFER_MAX 1024
#define DEFAULT_PORT "8080"
#define DEFAULT_CONFIG "http.conf"
#define THREAD_COUNT_DEFAULT 8
#define MAX_QUEUE_SIZE_DEFAULT 10
#define MAX_EVENTS 100

struct client *clients;

void serve_client(int sock, struct client *client_info, int epoll_fd);

void handle_request(struct client *);

int isPost(char *);

int isGet(char *);

int isHead(char *);

int isDelete(char *);

void consume(void *args);

void checkOS();

int send_responses(struct client*);

int recv_requests(struct client*);

int send_data(struct client*);

int recv_data(struct client*);

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
struct list list1;

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
    client->timestamp = time(NULL);
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
    printf("EXIT IN CLEAN UP MEMORY\n");
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
    list1 = newList();

    while (cont) {
        sweep(&list1);
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, 2500);
        if (nfds == 0) {
            continue;
        }
        if (nfds == -1) {
            perror("epoll_wait");
            exit(EXIT_FAILURE);
        }
        for (int i = 0; i < nfds; i++) {
            struct client *client1 = (struct client *) events[i].data.ptr;
            //new connection
            if (client1 == &server) {
                struct client *new_client = get_new_client(server.fd);
                if (new_client == NULL) {
                    continue;
                }
                add(&list1, new_client);
                ev.events = EPOLLIN;
                ev.data.ptr = (void *) new_client;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, new_client->fd, &ev) == -1) {
                    perror("epoll_ctl: new_client");
                    exit(EXIT_FAILURE);
                }
                continue;
            }
            //event from client
            if (events[i].events & EPOLLIN) {
                if (recv_requests(client1) == 0) {
                    ev.events = EPOLLOUT | EPOLLRDHUP | EPOLLET;
                    ev.data.ptr = (void *) client1;
                    if (epoll_ctl(epollfd, EPOLL_CTL_MOD, client1->fd, &ev) == -1) {
                        perror("epoll_ctl: switching to output events");
                        exit(EXIT_FAILURE);
                    }
                }
            }
            if (events[i].events & EPOLLOUT) {
                if (send_responses(client1) == 0) {
                    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
                    ev.data.ptr = (void *) client1;
                    if (epoll_ctl(epollfd, EPOLL_CTL_MOD, client1->fd, &ev) == -1) {
                        perror("epoll_ctl: switching to input events");
                        exit(EXIT_FAILURE);
                    }
                }
            }
            if(events[i].events & EPOLLRDHUP) {
                if(client1->state == RECEIVING){
                    client1->state = DISCONNECTED;
                }
            }
            if(client1->state == DISCONNECTED){
                removeFD(&list1, client1->fd);
            }
        }
    }
    verbosePrintf("Shutting down server\n");
    clean_up_memory();
    printf("Cleaned memory...Exiting\n");
}

int recv_requests(struct client *c) {
    if (recv_data(c) != 0) {
        c->state = DISCONNECTED;
        return 0;
    }
    c->timestamp = time(NULL);
    c->state = SENDING;
    handle_request(c);
    return 0;
}

int send_responses(struct client *c) {
    if (send_data(c) == 1) {
        return 1;
    }
    c->send_buf.length = 0;
    c->send_buf.position = 0;
    c->state = RECEIVING;
    c->recv_buf.length = 0;
    c->recv_buf.position = 0;
    return 0;
}

int send_data(struct client *c) {
    char *buffer = (char *) c->send_buf.data;
    int bytes_sent;
    int bytes_left = c->send_buf.length - c->send_buf.position;
    while (bytes_left > 0) {
        bytes_sent = send(c->fd, &buffer[c->send_buf.position], bytes_left, 0);
        if (bytes_sent == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                if(verbose)printf("Didn't finish!\n");
                return 1;
            } else if (errno == EINTR) {
                if(verbose)printf("EINTR\n");
                continue;
            } else if (errno == EPIPE || errno == ECONNRESET) {
                if(verbose)printf("DISCONNECTED\n");
                c->state = DISCONNECTED;
                return 1;
            } else {
                perror("send");
                c->state = DISCONNECTED;
                return 1;
            }
        }
        bytes_left -= bytes_sent;
        c->send_buf.position += bytes_sent;
    }
    return 0;
}

int recv_data(struct client *c) {
    char temp_buffer[BUFFER_MAX];
    int bytes_read;
    while (1) {
        bytes_read = recv(c->fd, temp_buffer, BUFFER_MAX, 0);
        if (bytes_read == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                break;
            } else if (errno = EINTR) {
                continue;
            } else {
                perror("recv");
                return 1;
            }
        } else if (bytes_read == 0) {
            break;
        }
        int new_length = c->recv_buf.length + bytes_read;
        if (c->recv_buf.max_length < new_length) {
            c->recv_buf.data = realloc(c->recv_buf.data, new_length * 2);
            c->recv_buf.max_length = new_length * 2;
        }
        memcpy(&(c->recv_buf.data)[c->recv_buf.length], temp_buffer, bytes_read);
        c->recv_buf.length += bytes_read;
    }
    return 0;
}

//consumer
//void consume(void *t) {
//    int epoll_fd = -1;
//    struct epoll_event events[MAX_EVENTS];
//    int nfds = -1;
//    struct thread_info *thread = (struct thread_info *) t;
//    while (cont) {
//        nfds = epoll_wait(thread->epoll_fd, events, MAX_EVENTS, -1);
//        for (int i = 0; i < nfds; i++) {
//            if (events[i].events & EPOLLIN) {
//                struct client *client_info = (struct client *) events[i].data.ptr;
//                client_info->timestamp = time(NULL);
//                serve_client(client_info->fd, (struct client *) events[i].data.ptr, thread->epoll_fd);
//            }
//        }
//    }
//}

//void serve_client(int sock, struct client *client_info, int epoll_fd) {
//    char buffer[BUFFER_MAX];
//    char client_hostname[NI_MAXHOST];
//    char client_port[NI_MAXSERV];
//    struct sockaddr_storage client_addr = client_info->storage;
//    socklen_t addr_len = client_info->addr_len;
//    int ret = getnameinfo((struct sockaddr *) &client_addr, addr_len, client_hostname, NI_MAXHOST, client_port,
//                          NI_MAXSERV, 0);
//    if (ret != 0) {
//        printf("ERROR: error getting name info\n");
//    }
//    if (verbose) printf("Connected to: %s:%s\n", client_hostname, client_port);
//    //serve client
//    ssize_t bytes_read = 0;
//    ssize_t total_bytes_read = 0;
//    while (cont) {
//        bytes_read = recv(sock, buffer, BUFFER_MAX, 0);
////        if(sem_wait(&clientsInQueue) == -1){
////            if(errno == EINTR){
////                if(cont == 0){
////                    break;
////                }
////            }
////        }
////        pthread_mutex_lock(&mutex);
//
//        client_info->timestamp = time(NULL);
////
////        pthread_mutex_unlock(&mutex);
////        sem_post(&openQueueSpot);
//
//        total_bytes_read += bytes_read;
//        if (bytes_read == 0) {
//            if (verbose) printf("Disconnected from %s:%s\n", client_hostname, client_port);
//
////            if(sem_wait(&clientsInQueue) == -1){
////                if(errno == EINTR){
////                    if(cont == 0){
////                        break;
////                    }
////                }
////            }
////            pthread_mutex_lock(&mutex);
//
//            removeFD(&list1, sock);
//
////            pthread_mutex_unlock(&mutex);
////            sem_post(&openQueueSpot);
//
//            return;
//        } else if (bytes_read < 0) {
//            //error in recv
//            if (errno == EINTR) {
//                if (cont == 0) {
//                    if (verbose) printf("Exiting serve_client\n");
//                    break;
//                }
//            } else if (errno == EPIPE) {
//                if (verbose) printf("ERROR: error in receiving\n");
//                char currentTime[80];
//                struct tm *currentTimeInfo;
//                time_t rawTime = 0;
//                currentTimeInfo = localtime(&rawTime);
//                strftime(currentTime, 80, "%a, %d %b %Y %H:%M:%S %Z", currentTimeInfo);
//                char header[BUFFER_MAX];
//                sprintf(header,
//                        "HTTP/1.1 500 Internal Server Error\r\n"
//                                "Server: CS360 Server\r\n"
//                                "Date: %s\r\n"
//                                "Content-Type: text/html\r\n"
//                                "Content-Length: %lu\r\n\r\n", currentTime, strlen(internalError500));
//                send(sock, header, strlen(header), 0);
//                send(sock, internalError500, strlen(internalError500), 0);
//                total_bytes_read = 0;
//                memset(buffer, 0, BUFFER_MAX);
//            }
//            continue;
//        } else if (strstr(buffer, "\r\n\r\n")) {
//            //get first request
////            buffer[total_bytes_read] = '\0';
//            handle_request(buffer, bytes_read, sock);
//            total_bytes_read = 0;
//            memset(buffer, 0, BUFFER_MAX);
//            continue;
//        }
//        if (verbose) printf("Closing socket: %d\n", sock);
//        removeFD(&list1, sock);
//        return;
//    }
//    removeFD(&list1, sock);
//}

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
void handle_request(struct client *c) {
    if(c->recv_buf.length <= 0){
        return;
    }
    char *request = c->recv_buf.data;
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

                int response_length = strlen(header) + strlen(forbidden403);
                if (c->send_buf.max_length < response_length) {
                    c->send_buf.data = realloc(c->send_buf.data, response_length);
                    c->send_buf.max_length = response_length;
                }
                c->send_buf.length = response_length;
                c->send_buf.position = 0;

                //send(sock, header, strlen(header), 0);
                memcpy(c->send_buf.data, header, strlen(header));
                //send(sock, forbidden403, strlen(forbidden403), 0);
                printf("forbidden copying\n");
                memcpy(&c->send_buf.data[strlen(header)], forbidden403, strlen(forbidden403));

                return;
            }
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

            int response_length = strlen(header) + size;

            if (c->send_buf.max_length < response_length) {
                c->send_buf.data = realloc(c->send_buf.data, response_length);
                c->send_buf.max_length = response_length;
            }
            c->send_buf.length = response_length;
            c->send_buf.position = 0;
            //send header
            //send(sock, header, strlen(header), 0);
            memcpy(c->send_buf.data, header, strlen(header));
            char* body;
            body = (char*)malloc(size * sizeof(char));
            fread(body, size, 1, fp);
            memcpy(&c->send_buf.data[strlen(header)], body, size);
            free(body);
            //macOS. Works!
//            if (isOnMac) {
//                if (sendfile(fileno(fp), sock, 0, &size, NULL, 0) == -1) {
//                    printf("Error sending file: %s\n", strerror(errno));
//                }
//            } else {
//                //linux. Works!
//                if (sendfile(sock, fileno(fp), 0, &size, NULL, 0) == -1) {
//                    printf("Error sending file: %s\n", strerror(errno));
//                }
//            }

            if (verbose) printf("Closing file pointer\n");
            if (fclose(fp) == -1) {
                printf("Error closing file: %s\n", strerror(errno));
            }
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
            int response_length = strlen(header) + strlen(notFound404);

            if (c->send_buf.max_length < response_length) {
                c->send_buf.data = realloc(c->send_buf.data, response_length);
                c->send_buf.max_length = response_length;
            }
            c->send_buf.length = response_length;
            c->send_buf.position = 0;

            //send(sock, header, strlen(header), 0);
            memcpy(c->send_buf.data, header, strlen(header));
            //send(sock, forbidden403, strlen(forbidden403), 0);
            printf("not found copying\n");
            memcpy(&c->send_buf.data[strlen(header)], notFound404, strlen(notFound404));
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
        int response_length = strlen(header) + strlen(notImplemented500);

        if (c->send_buf.max_length < response_length) {
            c->send_buf.data = realloc(c->send_buf.data, response_length);
            c->send_buf.max_length = response_length;
        }
        c->send_buf.length = response_length;
        c->send_buf.position = 0;

        //send(sock, header, strlen(header), 0);
        memcpy(c->send_buf.data, header, strlen(header));
        //send(sock, forbidden403, strlen(forbidden403), 0);
        printf("not implemented copying\n");
        memcpy(&c->send_buf.data[strlen(header)], notImplemented500, strlen(notImplemented500));

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
        int response_length = strlen(header) + strlen(notImplemented500);

        if (c->send_buf.max_length < response_length) {
            c->send_buf.data = realloc(c->send_buf.data, response_length);
            c->send_buf.max_length = response_length;
        }
        c->send_buf.length = response_length;
        c->send_buf.position = 0;

        //send(sock, header, strlen(header), 0);
        memcpy(c->send_buf.data, header, strlen(header));
        //send(sock, forbidden403, strlen(forbidden403), 0);
        printf("not implemented copying\n");
        memcpy(&c->send_buf.data[strlen(header)], notImplemented500, strlen(notImplemented500));

    } else if (isDelete(type)) {
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
        int response_length = strlen(header) + strlen(notImplemented500);

        if (c->send_buf.max_length < response_length) {
            c->send_buf.data = realloc(c->send_buf.data, response_length);
            c->send_buf.max_length = response_length;
        }
        c->send_buf.length = response_length;
        c->send_buf.position = 0;

        //send(sock, header, strlen(header), 0);
        memcpy(c->send_buf.data, header, strlen(header));
        //send(sock, forbidden403, strlen(forbidden403), 0);
        printf("not implemented copying\n");

        memcpy(&c->send_buf.data[strlen(header)], notImplemented500, strlen(notImplemented500));
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
        int response_length = strlen(header) + strlen(badRequest400);

        if (c->send_buf.max_length < response_length) {
            c->send_buf.data = realloc(c->send_buf.data, response_length);
            c->send_buf.max_length = response_length;
        }
        c->send_buf.length = response_length;
        c->send_buf.position = 0;

        //send(sock, header, strlen(header), 0);
        memcpy(c->send_buf.data, header, strlen(header));
        //send(sock, forbidden403, strlen(forbidden403), 0);
        printf("bad request copying\n");
        memcpy(&c->send_buf.data[strlen(header)], badRequest400, strlen(badRequest400));
        removeFD(&list1, c->fd);
        printf("EXIT IN 400\n");
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

int isDelete(char *request) {
    if (strstr(request, "DELETE")) {
        return 1;
    } else {
        return 0;
    }
}
