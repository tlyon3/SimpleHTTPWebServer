//
// Created by Trevor Lyon on 4/10/17.
// C queue using a Linked-List implementation
//
#include <stddef.h>
#include <stdlib.h>
#include <printf.h>
#include "queue.h"

struct queue newQueue(int size) {
    struct queue queue;
    queue.size = size;
    queue.head = NULL;
    return queue;

}

// Adds a new node with clientFD to the end of the lined list
// If the list is already full, no new node is added
// Return Value: 1 if a node was added. 0 if not.
int enqueue(struct queue *queue, int clientFD, struct sockaddr_storage storage, socklen_t socklen) {
    if (queue->head) {
        struct node *current = queue->head;
        //insert clientFD
        int i = 1;
        //get to end of list
        while (current->nextNode && i <= queue->size) {
            i++;
            current = current->nextNode;
        }
        //only add if list is not full
        if (i != queue->size) {
            struct node *newNode = malloc(sizeof(struct node));
            newNode->clientFD = clientFD;
            newNode->nextNode = NULL;
            newNode->client_addr = storage;
            newNode->addr_len = socklen;
            current->nextNode = newNode;
            return 1;
        } else {
            return 0;
        }
    } else {
        struct node *newNode = malloc(sizeof(struct node));
        newNode->clientFD = clientFD;
        newNode->nextNode = NULL;
        newNode->client_addr = storage;
        newNode->addr_len = socklen;
        queue->head = newNode;
        return 1;
    }
}

// Moves the head to the next node in the linked list
// Return Value: the original head
struct node *dequeue(struct queue *queue) {
    if (queue->head) {
        struct node *head = queue->head;
        queue->head = queue->head->nextNode;
        free(head);
        return head;
    } else {
        printf("Queue is empty!\n");
        return NULL;
    }
}

// Frees up all memory used by queue
void deconstructQueue(struct queue *queue) {
    struct node *current = queue->head;
    if (current) {
        while (current->nextNode) {
            struct node *temp = current->nextNode;
            free(current);
            current = temp;
        }
        //in case there is only one in the queue
        if (current) {
            free(current);
        }
    }
    free(queue);
}

