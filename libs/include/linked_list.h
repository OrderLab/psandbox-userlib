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
#include "psandbox.h"

struct node {
  PSandbox *sandbox;
  struct node *next;
};

struct node *head = NULL;

//display the list
void printList() {
  struct node *ptr = head;
  printf("\n[ ");

  //start from the beginning
  while (ptr != NULL) {
    printf("(%d,%d) ", ptr->sandbox->box_id, ptr->sandbox->box_id);
    ptr = ptr->next;
  }

  printf(" ]");
}

//insert link at the first location
void list_insertFirst(PSandbox *sandbox) {
  //create a link
  struct node *link = (struct node *) malloc(sizeof(struct node));

  link->sandbox = sandbox;

  //point it to old first node
  link->next = head;

  //point first to new first node
  head = link;
}

//delete first item
struct node *list_deleteFirst() {

  //save reference to first link
  struct node *tempLink = head;

  //mark next to first link as first
  head = head->next;

  //return the deleted link
  return tempLink;
}

//is list empty
bool list_isEmpty() {
  return head == NULL;
}

int length() {
  int length = 0;
  struct node *current;

  for (current = head; current != NULL; current = current->next) {
    length++;
  }

  return length;
}

//find a link with given key
struct node *list_find(PSandbox *sandbox) {

  //start from the first link
  struct node *current = head;

  //if list is empty
  if (head == NULL) {
    return NULL;
  }

  //navigate through list
  while (current->sandbox != sandbox) {

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
struct node *list_delete(PSandbox *sandbox) {

  //start from the first link
  struct node *current = head;
  struct node *previous = NULL;

  //if list is empty
  if (head == NULL) {
    return NULL;
  }

  //navigate through list
  while (current->sandbox != sandbox) {

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
  if (current == head) {
    //change first to point to next link
    head = head->next;
  } else {
    //bypass the current link
    previous->next = current->next;
  }

  return current;
}

void list_reverse(struct node **head_ref) {
  struct node *prev = NULL;
  struct node *current = *head_ref;
  struct node *next;

  while (current != NULL) {
    next = current->next;
    current->next = prev;
    prev = current;
    current = next;
  }

  *head_ref = prev;
}

#endif //PSANDBOX_USERLIB_LINKED_LIST_H
