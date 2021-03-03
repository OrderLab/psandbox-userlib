//
// The Psandbox project
//
// Created by yigonghu on 2/25/21.
//
// Copyright (c) 2021, Johns Hopkins University - Order Lab
//
//      All rights reserved.
//      Licensed under the Apache License, Version 2.0 (the "License");

#ifndef PSANDBOX_USERLIB_LINKED_LIST_H
#define PSANDBOX_USERLIB_LINKED_LIST_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include "psandbox.h"


/* We need to keep values. */
struct linkedlist_element_s {
  void *data;
  struct linkedlist_element_s *next;
};

typedef struct linkedlist_s {
  int size;
  struct linkedlist_element_s *head;
}LinkedList;


/// @brief Create a linkedlist.
/// @return the head of the linked list.
///
/// Note that the function will malloc memory for the linked list header and the
/// creation of the linked list will fail if the malloc fails.
static LinkedList *linkedlist_create();

/// @brief Insert an node into the head of linked list.
/// @param linkedlist The linkedlist to insert into.
/// @param value The value to insert.
/// @return On success 0 is returned.
///
/// The key string slice is not copied when creating the linked list entry, and thus
/// must remain a valid pointer until the linkedlist entry is removed or the
/// linkedlist is destroyed.
static int list_push_front(LinkedList *const linkedlist, void *const value) HASHMAP_USED;

/// @brief Remove an element from the linkedlist and pop it out.
/// @param linkedlist The linkedlist to remove from.
/// @return The element that we removed.
static struct linkedlist_element_s *list_pop_front(LinkedList *const linkedlist);

/// @brief Remove an element from the linkedlist and pop it out.
/// @param linkedlist The linkedlist to remove from.
static void list_remove_front(LinkedList *const linkedlist);

/// @brief Check the size of the linked list.
/// @param linkedlist The linkedlist to check.
/// @return The size of the list.
static int list_size(LinkedList *const linkedlist);

/// @brief Find the node based on the values.
/// @param linkedlist The linkedlist to find the element.
/// @param value The value to find.
/// @return The node that we find.
static struct linkedlist_element_s *list_find(LinkedList *const linkedlist, void *value);

/// @brief Remove an element from the linkedlist and pop it out.
/// @param linkedlist The linkedlist to remove from.
/// @param value The value to remove.
/// @return The removed element.
static struct linkedlist_element_s *list_remove_and_return_key(LinkedList *const linkedlist, void *value);

/// @brief Remove an element from the linkedlist
/// @param linkedlist The linkedlist to remove from.
/// @param value The value to remove.
/// @return On success 0 is returned.
static int list_remove(LinkedList *const linkedlist, void *value);

LinkedList *linkedlist_create() {
  LinkedList *linkedList = (LinkedList *)malloc(sizeof(LinkedList));
  if(!linkedList) {
    printf("Error: fail to create linked list\n");
    return NULL;
  }

  return linkedList;
}


//insert link at the first location
int list_push_front(LinkedList *const linkedlist, void *const value) {
  //create a node
  struct linkedlist_element_s* node = (struct linkedlist_element_s *) malloc(sizeof(struct linkedlist_element_s));
  if(!node) {
    printf("Error: fail to create linked list\n");
    return -1;
  }

  //point it to old first node
  node->next = linkedlist->head;
  node->data = value;

  //point first to new first node
  linkedlist->head = node;
  linkedlist->size++;
  return 0;
}

//pop the first item
struct linkedlist_element_s *list_pop_front(LinkedList *const linkedlist) {

  //save reference to first link
  struct linkedlist_element_s *tempLink = linkedlist->head;

  //mark next to first link as first
  linkedlist->head = linkedlist->head->next;
  linkedlist->size--;

  //return the deleted link
  return tempLink;
}

//pop the first item
void list_remove_front(LinkedList *const linkedlist) {

  //save reference to first link
  struct linkedlist_element_s *tempLink = linkedlist->head;

  //mark next to first link as first
  linkedlist->head = linkedlist->head->next;
  free(tempLink);
  linkedlist->size--;

}


//is list empty
int list_size(LinkedList *const linkedlist) {
  assert(linkedlist->head != NULL);
  return linkedlist->size;
}


//find a link with given key
struct linkedlist_element_s *list_find(LinkedList *const linkedlist, void *value) {

  //start from the first link
  struct linkedlist_element_s *current = linkedlist->head;

  //if list is empty
  if (linkedlist->head == NULL) {
    return NULL;
  }

  //navigate through list
  while (current->data != value) {

    //if it is last node
    if (current->next == NULL) {
      return NULL;
    } else {
      //go to next link
      current = current->next;
    }
  }

  //if data found, return the current Link
  return current;
}

//delete a link with given key
struct linkedlist_element_s *list_remove_and_return_key(LinkedList *const linkedlist, void *value) {

  //start from the first link
  struct linkedlist_element_s *current = linkedlist->head;
  struct linkedlist_element_s *previous = NULL;

  //if list is empty
  if (linkedlist->head == NULL) {
    return NULL;
  }

  //navigate through list
  while (current->data != value) {

    //if it is last node
    if (current->next == NULL) {
      return NULL;
    } else {
      //store reference to current link
      previous = current;
      //move to next link
      current = current->next;
    }
  }

  //found a match, update the link
  if (current == linkedlist->head) {
    //change first to point to next link
    linkedlist->head = linkedlist->head->next;
  } else {
    //bypass the current link
    previous->next = current->next;
  }
  linkedlist->size--;
  return current;
}

//delete a link with given key
int list_remove(LinkedList *const linkedlist, void *value) {

  //start from the first link
  struct linkedlist_element_s *current = linkedlist->head;
  struct linkedlist_element_s *previous = NULL;

  //if list is empty
  if (linkedlist->head == NULL) {
    return -1;
  }

  //navigate through list
  while (current->data != value) {

    //if it is last node
    if (current->next == NULL) {
      return -1;
    } else {
      //store reference to current link
      previous = current;
      //move to next link
      current = current->next;
    }
  }

  //found a match, update the link
  if (current == linkedlist->head) {
    //change first to point to next link
    linkedlist->head = linkedlist->head->next;
  } else {
    //bypass the current link
    previous->next = current->next;
  }
  linkedlist->size--;
  free(current);
  return 0;
}

//display the list
void printList(LinkedList *const linkedlist) {
  struct linkedlist_element_s *ptr = linkedlist->head;
  printf("\n[ ");

  //start from the beginning
  while (ptr != NULL) {
    printf("(%d) ", ptr->data);
    ptr = ptr->next;
  }

  printf(" ]");
}

//void list_reverse(struct node **head_ref) {
//  struct node *prev = NULL;
//  struct node *current = *head_ref;
//  struct node *next;
//
//  while (current != NULL) {
//    next = current->next;
//    current->next = prev;
//    prev = current;
//    current = next;
//  }
//
//  *head_ref = prev;
//}

#endif //PSANDBOX_USERLIB_LINKED_LIST_H
