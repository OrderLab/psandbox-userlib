//
// The Psandbox project
//
// Created by yigonghu on 2/18/21.
//
// Copyright (c) 2021, Johns Hopkins University - Order Lab
//
//      All rights reserved.
//      Licensed under the Apache License, Version 2.0 (the "License");
#include "../include/psandbox.h"
#include "../include/hashmap.h"
#include "../include/linked_list.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#define SYS_PSANDBOX_CREATE	436
#define SYS_PSANDBOX_RELEASE 437
#define SYS_PSANDBOX_WAKEUP 438

const unsigned initial_size = 8;
HashMap psandbox_groups;
HashMap sandbox_list;
Condition c;

PSandbox *pbox_create(float rule) {
  struct pSandbox *pBox;
  long sandbox_id = 1;
  const char *arg = NULL;

  pBox = (struct pSandbox*)malloc(sizeof(struct pSandbox));
  if (pBox == NULL) return NULL;
  sandbox_id = syscall(SYS_PSANDBOX_CREATE, &arg);

  if (sandbox_id == -1) {
    free(pBox);
    printf("syscall failed with errno: %s\n", strerror(errno));
    return NULL;
  }

  pBox->box_id = sandbox_id;
  pBox->state = START;
  if(sandbox_list.table_size == 0) {
    if (0 != hashmap_create(initial_size, &sandbox_list)) {
      free(pBox);
      return NULL;
    }
  }
  pid_t tid = syscall(SYS_gettid);
  pBox->tid = tid;
  pBox->delay_ratio = rule;
//  printf("set sandbox for the thread %d\n",tid);
  hashmap_put(&sandbox_list,tid,pBox);
  return pBox;
}

int pbox_release(struct pSandbox* pSandbox){
  if (!pSandbox)
    return 0;

  int arg = pSandbox->box_id;
  long success = syscall(SYS_PSANDBOX_RELEASE, arg);

  if(success == -1) {
    free(pSandbox);
    printf("failed to release sandbox in the kernel: %s\n", strerror(errno));
    return -1;
  }
  free(pSandbox);
  return 1;
}


int pbox_update(struct sandboxEvent event,PSandbox *sandbox) {
  int event_type = event.event_type;
  int key_type = event.key_type;
  void* element;
  if(!event.key)
    return -1;

  if(psandbox_groups.table_size == 0) {
    if (0 != hashmap_create(initial_size, &psandbox_groups)) {
      return -1;
    }
  }

  if(sandbox->state != ACTIVE) {
    return 0;
  }

  switch (event_type) {
    case UPDATE_KEY:
      element = hashmap_get(&psandbox_groups, &event.key);
      if (NULL == element) {
        //:TODO make link list reuseable
        list_insertFirst(sandbox);
        hashmap_put(&psandbox_groups,&event.key, head);
      } else {
        if(!list_find(sandbox)) {
          list_insertFirst(sandbox);
        }
      }

      if (*(int*)event.key <= c.value) {
          for(struct node* node = head; node->next != NULL; node = node->next) {
            if (sandbox == node->sandbox)
              continue;
            clock_t executing_time = clock() - node->sandbox->execution_start;
            clock_t delayed_time = clock() - node->sandbox->delaying_time + node->sandbox->delayed_time;
            int num = sandbox_list.size;
            if(delayed_time > executing_time*sandbox->delay_ratio*num) {
              int arg = node->sandbox->tid;
              syscall(SYS_PSANDBOX_WAKEUP, arg);
              break;
            }
          }
      }
      break;
    case SLEEP_BEGIN:
      element = hashmap_get(&psandbox_groups, &event.key);
      if (NULL == element) {
        //:TODO make link list reuseable
        list_insertFirst(sandbox);
        hashmap_put(&psandbox_groups,&event.key,head);
      } else {
        if(!list_find(sandbox)) {
          list_insertFirst(sandbox);
        }
      }
      sandbox->delaying_time=clock();
//      int arg = 0;
//      syscall(SYS_PSANDBOX_WAKEUP, arg);
      break;
    case SLEEP_END:
      element = hashmap_get(&psandbox_groups, &event.key);
      if (NULL == element) {
        //:TODO make link list reuseable
        list_insertFirst(sandbox);
        hashmap_put(&psandbox_groups,&event.key,head);
      } else {
        if(!list_find(sandbox)) {
          list_insertFirst(sandbox);
        }
      }
      sandbox->delayed_time += clock() - sandbox->delaying_time;
      int arg = 0;
      syscall(SYS_PSANDBOX_WAKEUP, arg);
      break;
    default:break;
  }

  return 1;
}

int pbox_condition(int value, bool isBig){
  c.value = value;
  c.isBig = false;
}

int pbox_active(PSandbox* pSandbox) {
  pSandbox->state = ACTIVE;
  pSandbox->execution_start = clock();
  return 1;
}

int pbox_freeze(PSandbox* pSandbox) {
  pSandbox->state = FREEZE;
  list_delete(pSandbox);
  return 1;
}

PSandbox *pbox_get() {
  pid_t tid = syscall(SYS_gettid);
  void* element = hashmap_get(&sandbox_list, tid);
  PSandbox *sandbox =  (PSandbox *)element;
  if (NULL == element) {
    printf("Can't get sandbox for the thread %d\n",tid);
    return NULL;
  }
  return sandbox;
}
