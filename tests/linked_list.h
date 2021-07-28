#include <stdio.h>
#include <stdlib.h>

typedef struct Node {
    size_t data;
    struct Node *next;
} node_t;

node_t *queue_push(node_t *head, size_t data) {
    node_t *i, *new_node;
    new_node = (node_t *)malloc(sizeof(node_t));
    new_node->data = data;
    new_node->next = NULL;
    i = head;

    if (!i)
        return new_node;
    
    while (i->next) {
        i = i->next;
    }
    i->next = new_node;
    return head;
}

node_t *queue_pop(node_t *head) {
    node_t *new_head;
    if (!head)
        return head;
    new_head = head->next;
    free(head);
    return new_head;
}

int queue_is_empty(node_t *head) {
    return head ? 0 : 1;
}