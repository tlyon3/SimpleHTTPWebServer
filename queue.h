//
// Created by Trevor Lyon on 4/10/17.
// C queue using a Linked-List implementation
//

#ifndef CSRC_QUEUE_H
#define CSRC_QUEUE_H

#include <sys/socket.h>

struct node{
    int clientFD;
    struct node* nextNode;
    struct sockaddr_storage client_addr;
    socklen_t addr_len;
};

struct queue{
    int size;
    struct node* head;
};

struct queue* newQueue(int);
int enqueue(struct queue*, int clientFD, struct sockaddr_storage, socklen_t);
struct node* dequeue(struct queue* );

void deconstructQueue(struct queue*);

#endif //CSRC_QUEUE_H
