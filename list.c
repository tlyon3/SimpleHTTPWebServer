//
// Created by Trevor Lyon on 4/18/17.
//

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "list.h"

int debug = 0;

struct list newList() {
    struct list l;
    l.size = 0;
    l.head = NULL;
    return l;
}

void add(struct list *l, struct client *c) {
    if (l->head) {
        struct listNode *current = l->head;
        while (current->nextNode) {
            if(current->client->fd == c->fd){
                current->client->timestamp = time(NULL);
                return;
            }
            current = current->nextNode;
        }
        struct listNode *newNode = malloc(sizeof(struct listNode));
        newNode->client = c;
        newNode->nextNode = NULL;
        current->nextNode = newNode;
        l->size += 1;
        if(debug)printf("Added %d->%d to list \n", current->client->fd, c->fd);
        return;
    } else {
        struct listNode *newNode = malloc(sizeof(struct listNode));
        newNode->client = c;
        newNode->nextNode = NULL;
        l->head = newNode;
        l->size += 1;
    }
    if(debug)printf("Added %d to list\n", c->fd);
}

void sweep(struct list *l) {
    struct listNode *current = l->head;
    if (debug)printf("-----------------\n");
    while (current) {
        if (debug) printf("NEXT NODE\n");
        time_t current_time = time(NULL);
        if (debug)
            printf("Node: %d\n\tTimestamp: %lu\n\tCurrent-time: %lu\n", current->client->fd, current->client->timestamp,
                   current_time);
        struct listNode* temp = current->nextNode;
        if (current_time - current->client->timestamp > 5) {
            removeFD(l, current->client->fd);
        }
        current = temp;
    }
    if (debug)printf("-----------------\n");
}

void removeFD(struct list* l, int fd){
    struct listNode* current = l->head;
    while(current){
        if(current->client->fd == fd){
            if (current->nextNode) {
                if (debug)printf("Current->next: %d\n", current->nextNode->client->fd);
                if(debug)printf("Deleting node: %d\n", current->client->fd);
                struct listNode *temp = current->nextNode;
                int currentfd = current->client->fd;
                current->client = temp->client;
                current->nextNode = temp->nextNode;
                if (current == l->head) {
                    if (debug)printf("Setting %d to head\n", current->client->fd);
                    l->head = current;
                }
                close(currentfd);
//                free(temp);
                return;
            } else {
                if(debug)printf("Deleting node (%d) with no next node\n", current->client->fd);
                if (current == l->head) {
                    l->head = NULL;
                }
                close(current->client->fd);
//                free(current);
                return;
            }
        }
    }
}