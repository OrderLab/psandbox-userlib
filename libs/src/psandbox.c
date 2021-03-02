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
HashMap competed_sandbox_set;
HashMap psandbox_thread_map;
HashMap key_condition_map;
Condition c;

PSandbox *pbox_create(float rule) {
  struct pSandbox *pBox;
  long sandbox_id;
  const char *arg = NULL;

  pBox = (struct pSandbox*)malloc(sizeof(struct pSandbox));
  if (pBox == NULL) return NULL;
//  sandbox_id = syscall(SYS_PSANDBOX_CREATE, &arg);

  if (sandbox_id == -1) {
    free(pBox);
    printf("syscall failed with errno: %s\n", strerror(errno));
    return NULL;
  }

  pBox->box_id = sandbox_id;
  pBox->state = START;
  if(psandbox_thread_map.table_size == 0) {
    if (0 != hashmap_create(initial_size, &psandbox_thread_map)) {
      free(pBox);
      return NULL;
    }
  }
  pid_t tid = syscall(SYS_gettid);
  pBox->tid = tid;
  pBox->delay_ratio = rule;
//  printf("set sandbox for the thread %d\n",tid);
  hashmap_put(&psandbox_thread_map,tid,pBox);
  return pBox;
}

int pbox_release(struct pSandbox* pSandbox){
  long success = 0;
  if (!pSandbox)
    return 0;

  int arg = pSandbox->box_id;
//  success = syscall(SYS_PSANDBOX_RELEASE, arg);

  if(success == -1) {
    free(pSandbox);
    printf("failed to release sandbox in the kernel: %s\n", strerror(errno));
    return -1;
  }
  free(pSandbox);
  return 1;
}


int push_or_create_competitors(LinkedList* competitors, struct sandboxEvent event, PSandbox* new_competitor) {
  int success = 0;
  if (NULL == competitors) {
    competitors = linkedlist_create();
    success = list_push_front(competitors,new_competitor);
    hashmap_put(&competed_sandbox_set,event.key,competitors);
  } else {
    if(!list_find(competitors,new_competitor)) {
      success = list_push_front(competitors,new_competitor);
    }
  }
  return success;
}

int wakeup_competitor(LinkedList *competitors, PSandbox* sandbox) {
  for(struct linkedlist_element_s* node = competitors->head; node != NULL; node = node->next) {

    // Don't wakeup itself
    if (sandbox == node->data)
      continue;

    PSandbox* competitor_sandbox = (PSandbox *)(node->data) ;
    clock_t executing_time = clock() - competitor_sandbox->execution_start;
    clock_t delayed_time = clock() - competitor_sandbox->delaying_time + competitor_sandbox->delayed_time;
    int num = psandbox_thread_map.size;
    if(delayed_time > executing_time*sandbox->delay_ratio*num) {
      int arg = competitor_sandbox->tid;
      printf("wakeup sandbox %d\n", competitor_sandbox->tid);
      syscall(SYS_PSANDBOX_WAKEUP, arg);
      break;
    }
  }
  return 0;
}

int pbox_update(struct sandboxEvent event,PSandbox *sandbox) {
  int event_type = event.event_type;
  int key_type = event.key_type;
  LinkedList * competitors;
  if(!event.key)
    return -1;

  if(competed_sandbox_set.table_size == 0) {
    if (0 != hashmap_create(initial_size, &competed_sandbox_set)) {
      return -1;
    }
  }

  if(sandbox->state != ACTIVE) {
    return 0;
  }

  switch (event_type) {
    case UPDATE_KEY:
      competitors = hashmap_get(&competed_sandbox_set, event.key);
      if(push_or_create_competitors(competitors,event,sandbox)) {
        printf("Error: fail to create competitor list\n");
        return -1;
      }
      competitors = hashmap_get(&competed_sandbox_set, event.key);
      Condition* cond = hashmap_get(&key_condition_map, event.key);
      if(!cond) {
        printf("Error: fail to find condition\n");
        return -1;
      }

      switch (cond->compare) {
        case LARGE:
          if (*(int*)event.key <= cond->value) {
            wakeup_competitor(competitors,sandbox);
          }
          break;
        case SMALL:
          if (*(int*)event.key >= *(int *)cond->value) {
            wakeup_competitor(competitors,sandbox);
          }
          break;
        case LARGE_OR_EQUAL:
          if (*(int*)event.key < *(int *)cond->value) {
            wakeup_competitor(competitors,sandbox);
          }
          break;
        case SMALL_OR_EQUAL:
          if (*(int*)event.key > *(int *)cond->value) {
            wakeup_competitor(competitors,sandbox);
          }
          break;
      }

      break;
    case SLEEP_BEGIN:
      competitors = hashmap_get(&competed_sandbox_set, event.key);
      if(push_or_create_competitors(competitors,event,sandbox)) {
        printf("Error: fail to create competitor list\n");
        return -1;
      }
      sandbox->delaying_time=clock();
      break;
    case SLEEP_END:
      competitors = hashmap_get(&competed_sandbox_set, event.key);
      if(push_or_create_competitors(competitors,event,sandbox)) {
        printf("Error: fail to create competitor list\n");
        return -1;
      }
      sandbox->delayed_time += clock() - sandbox->delaying_time;
      break;
    default:break;
  }

  return 1;
}

int pbox_update_condition(int* key, Condition cond){
  Condition *condition;

  if (key_condition_map.table_size == 0) {
    if (0 != hashmap_create(initial_size, &key_condition_map)) {
      return -1;
    }
  }
  condition = hashmap_get(&key_condition_map, key);
  if(!condition) {
    condition  = malloc(sizeof(Condition));
    condition->compare = cond.compare;
    condition->value = cond.value;
    if(hashmap_put(&key_condition_map, key, condition)) {
      printf("Error: fail to add condition list\n");
      return -1;
    }
  } else {
    condition->compare = cond.compare;
    condition->value = cond.value;
  }

  return 0;
}

int pbox_active(PSandbox* pSandbox) {
  pSandbox->state = ACTIVE;
  pSandbox->execution_start = clock();
  return 1;
}

int pbox_freeze(PSandbox* pSandbox) {
  pSandbox->state = FREEZE;
  pSandbox->delaying_time = 0;
  pSandbox->delayed_time = 0;
  pSandbox->execution_start = 0;
  return 1;
}

PSandbox *pbox_get() {
  pid_t tid = syscall(SYS_gettid);
  void* element = hashmap_get(&psandbox_thread_map, tid);
  PSandbox *sandbox =  (PSandbox *)element;
  if (NULL == element) {
    printf("Can't get sandbox for the thread %d\n",tid);
    return NULL;
  }
  return sandbox;
}
