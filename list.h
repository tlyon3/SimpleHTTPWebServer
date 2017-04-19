//
// Created by Trevor Lyon on 4/18/17.
//

#ifndef CSRC_LIST_H
#define CSRC_LIST_H

#include <sys/socket.h>
enum state {
    SENDING,
    RECEIVING,
    DISCONNECTED
};

struct buffer{
    unsigned char* data;
    int length;
    int max_length;
    int position;
};

typedef struct client {
    int fd; /* socket descriptor for connection */
    socklen_t addr_len;
    struct sockaddr_storage storage;
    /*... many other members omitted for brevity */
    /* you can, and probably will, add to this */
    time_t timestamp;
    enum state state;
    struct buffer send_buf;
    struct buffer recv_buf;
} client_t;

struct listNode {
    struct client* client;
    struct listNode* nextNode;
};

struct list{
    int size;
    struct listNode* head;
};

struct list newList();
void add(struct list*, struct client*);
void sweep(struct list*);
void removeFD(struct list*, int);
#endif //CSRC_LIST_H
