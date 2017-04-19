//
// Created by Trevor Lyon on 4/18/17.
//

#ifndef CSRC_CLIENT_H
#define CSRC_CLIENT_H
typedef struct client {
    int fd; /* socket descriptor for connection */
    socklen_t addr_len;
    struct sockaddr_storage storage;
    /*... many other members omitted for brevity */
    /* you can, and probably will, add to this */
    time_t timestamp;
} client_t;

extern client client;
#endif //CSRC_CLIENT_H
