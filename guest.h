#ifndef GUEST_H
#define GUEST_H

#define NAME_LEN 32

typedef struct guest {
    char name[NAME_LEN];
    int cur_days;
} guest;

typedef struct queue_node {
    guest guest;
    int sock;
    struct queue_node *next;
} queue_node;

#endif 

