#include <stdio.h>

#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>

#include "create_server_socket.h"

#define BUFFER_MAX 1024

void serve_client(int client, struct sockaddr_storage client_addr, socklen_t addr_len);
void handle_request(unsigned char *request, size_t request_len, unsigned char *response, int sock);

int main(int argc, char *argv[]) {

    printf("Starting up server...\n");

    int port = 8080;
    char sport[10];
    sprintf(sport, "%d", port);

    int sock = create_server_socket(sport, SOCK_STREAM);
    printf("Created socket: %d\n", sock);
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t  client_addr_len = sizeof(client_addr);
        int client = accept(sock, (struct sockaddr*)&client_addr, &client_addr_len);
        if(client == -1){
           printf("ERROR: error in accept\n");
        }
        else {
            if(fork() == 0) {
                serve_client(client, client_addr, client_addr_len);
            }
            //todo: reap child on return
            continue;
        }
        return 0;
    }
}

void serve_client(int sock, struct sockaddr_storage client_addr, socklen_t addr_len){
    unsigned char buffer[BUFFER_MAX];
    unsigned char response[BUFFER_MAX];
    char client_hostname[NI_MAXHOST];
    char client_port[NI_MAXSERV];
    int ret = getnameinfo((struct sockaddr*)&client_addr, addr_len, client_hostname, NI_MAXHOST, client_port, NI_MAXSERV, 0);
    if(ret != 0){
        printf("ERROR: error getting name info\n");
    }
    printf("Connected to: %s:%s\n", client_hostname, client_port);
    //serve client
    while(1){
        size_t  bytes_read = recv(sock, buffer, BUFFER_MAX-1, 0);
        if(bytes_read == 0){
            printf("Disconnected from %s:%s\n", client_hostname, client_port);
            close(sock);
            return;
        }
        else if(bytes_read < 0) {
            //error in recv
            printf("ERROR: error in receiving\n");
            //todo: send back error code 500
        }
        else {
            buffer[bytes_read] = '\0';
            handle_request(buffer, bytes_read, response, sock);
        }
    }
}

void handle_request(unsigned char *request, size_t request_len, unsigned char* response, int sock){
    //todo: parse request
    printf("Received request: %s", request);
    send(sock, request, request_len+1, 0);
    //todo: get current date
    //todo: get content type
    //todo: get content length
    //todo: get last-modified
}
