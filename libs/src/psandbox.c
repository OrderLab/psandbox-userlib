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
#include <sys/time.h>
#include <signal.h>

#define SYS_PSANDBOX_CREATE	436
#define SYS_PSANDBOX_RELEASE 437
#define SYS_PSANDBOX_WAKEUP 438
#define SYS_PSANDBOX_PENALTY 439

#define USEC_PER_SEC	1000000L

const unsigned initial_size = 8;
HashMap competed_sandbox_set;
HashMap psandbox_thread_map;
HashMap key_condition_map;

PSandbox *pbox_create(float rule) {
  struct pSandbox *pSandbox;
  long sandbox_id;
  const char *arg = NULL;

  pSandbox = (struct pSandbox*)malloc(sizeof(struct pSandbox));

  if (pSandbox == NULL) return NULL;
  sandbox_id = syscall(SYS_PSANDBOX_CREATE, &arg);

  if (sandbox_id == -1) {
    free(pSandbox);
    printf("syscall failed with errno: %s\n", strerror(errno));
    return NULL;
  }
  Activity *act = (Activity *) calloc(1,sizeof(Activity));
  pSandbox->activity = act;
  pSandbox->box_id = sandbox_id;
  pSandbox->state = BOX_START;


  if(psandbox_thread_map.table_size == 0) {
    if (0 != hashmap_create(initial_size, &psandbox_thread_map)) {
      free(pSandbox);
      return NULL;
    }
  }
  pid_t tid = syscall(SYS_gettid);
  pSandbox->tid = tid;
  pSandbox->delay_ratio = rule;
//  printf("set sandbox for the thread %d\n",tid);
  hashmap_put(&psandbox_thread_map,tid,pSandbox);
  return pSandbox;
}

int pbox_release(struct pSandbox* pSandbox){
  long success = 0;
  if (!pSandbox)
    return 0;

  int arg = pSandbox->box_id;
  success = syscall(SYS_PSANDBOX_RELEASE, arg);

  if(success == -1) {
    free(pSandbox);
    printf("failed to release sandbox in the kernel: %s\n", strerror(errno));
    return -1;
  }
  hashmap_remove(&psandbox_thread_map,pSandbox->tid);
  free(pSandbox->activity);
  free(pSandbox);
  return 1;
}

long int timeval_to_time(struct timeval tm) {
  return tm.tv_sec * USEC_PER_SEC + tm.tv_usec;
}

struct timeval time_to_timeval(long int tm) {
  struct timeval t;
  t.tv_sec = tm / 1000000;
  t.tv_usec = tm % 1000000;
  return t;
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
  for (struct linkedlist_element_s* node = competitors->head; node != NULL; node = node->next) {
    PSandbox* competitor_sandbox = (PSandbox *)(node->data);

    // Don't wakeup itself
    if (sandbox == node->data || competitor_sandbox->activity->queue_state != QUEUE_SLEEP)
      continue;


    struct timeval current;
    gettimeofday(&current, NULL);
    long int current_tm = timeval_to_time(current);
    long int delaying_start_tm = timeval_to_time(competitor_sandbox->activity->delaying_start);
    long int execution_start_tm = timeval_to_time(competitor_sandbox->activity->execution_start);
    long int executing_time = current_tm - execution_start_tm;
    long int delayed_time = current_tm - delaying_start_tm;


    int num = psandbox_thread_map.size;
    if(delayed_time > (executing_time - delayed_time) * sandbox->delay_ratio * num) {
      int arg = competitor_sandbox->tid;
//      printf("wakeup sandbox %d\n", competitor_sandbox->tid);
      syscall(SYS_PSANDBOX_WAKEUP, arg);
      break;
    }
  }
  return 0;
}

int pbox_update(struct sandboxEvent event,PSandbox *sandbox) {
  int event_type = event.event_type;
  LinkedList *competitors;
  int delayed_competitors = 0;

  if(!event.key)
    return -1;

  if(competed_sandbox_set.table_size == 0) {
    if (0 != hashmap_create(initial_size, &competed_sandbox_set)) {
      return -1;
    }
  }

  if(sandbox->state != BOX_ACTIVE) {
    return 0;
  }

  switch (event_type) {
    case UPDATE_QUEUE_CONDITION:
      competitors = hashmap_get(&competed_sandbox_set, event.key);
      if(!competitors) {
        printf("Error: fail to find the competitor sandbox\n");
        return -1;
      }
      Condition* cond = hashmap_get(&key_condition_map, event.key);
      if(!cond) {
        printf("Error: fail to find condition\n");
        return -1;
      }

      switch (cond->compare) {
        case COND_LARGE:
          if (*(int*)event.key <= cond->value) {
            wakeup_competitor(competitors,sandbox);
          }
          break;
        case COND_SMALL:
          if (*(int*)event.key >= *(int *)cond->value) {
            wakeup_competitor(competitors,sandbox);
          }
          break;
        case COND_LARGE_OR_EQUAL:
          if (*(int*)event.key < *(int *)cond->value) {
            wakeup_competitor(competitors,sandbox);
          }
          break;
        case COND_SMALL_OR_EQUAL:
          if (*(int*)event.key > *(int *)cond->value) {
            wakeup_competitor(competitors,sandbox);
          }
          break;
      }
      break;
    case SLEEP_BEGIN:
      sandbox->activity->queue_state=QUEUE_SLEEP;
      break;
    case SLEEP_END:
      sandbox->activity->queue_state=QUEUE_AWAKE;
      break;
    case TRY_QUEUE:
      competitors = hashmap_get(&competed_sandbox_set, event.key);
      if(push_or_create_competitors(competitors,event,sandbox)) {
        printf("Error: fail to create competitor list\n");
        return -1;
      }

      gettimeofday(&sandbox->activity->delaying_start , NULL);
      break;
    case ENTER_QUEUE:
      sandbox->activity->queue_state = QUEUE_ENTER;

      struct timeval current;
      gettimeofday(&current, NULL);
      long int cur = timeval_to_time(current);
      long int del = timeval_to_time(sandbox->activity->delaying_start);
      long int delayed = timeval_to_time(sandbox->activity->delayed_time);
      delayed += cur-del;
      sandbox->activity->delayed_time = time_to_timeval(delayed);
      break;
    case EXIT_QUEUE:
      competitors = hashmap_get(&competed_sandbox_set, event.key);
      if(!competitors) {
        printf("Error: fail to create competitor list\n");
        return -1;
      }

      if(list_remove(competitors,sandbox)) {
        printf("Error: fail to remove competitor sandbox\n");
        return -1;
      }

      if(competitors->size == 0) {
        if(hashmap_remove(&competed_sandbox_set, event.key) || hashmap_remove(&key_condition_map,event.key)) {
          printf("Error: fail to remove empty list\n");
          return -1;
        }

      }
      sandbox->activity->queue_state=QUEUE_NULL;
      break;
    case MUTEX_REQUIRE:
      competitors = hashmap_get (&competed_sandbox_set, event.key);
      if (push_or_create_competitors(competitors,event,sandbox)) {
        printf("Error: fail to create competitor list\n");
        return -1;
      }
      gettimeofday(&sandbox->activity->delaying_start , NULL);
      break;
    case MUTEX_GET: {
      struct timeval current;
      gettimeofday(&current, NULL);
      long int cur = timeval_to_time(current);
      long int del = timeval_to_time(sandbox->activity->delaying_start);
      long int delayed = timeval_to_time(sandbox->activity->delayed_time);
      delayed += cur-del;
      sandbox->activity->delayed_time = time_to_timeval(delayed);
      break;
    }
    case MUTEX_RELEASE:
      delayed_competitors = 0;
      competitors = hashmap_get(&competed_sandbox_set, event.key);
      if(!competitors) {
        printf("Error: fail to create competitor list\n");
        return -1;
      }

      if(list_remove(competitors,sandbox)) {
        printList(competitors);
        printf("Error: fail to remove competitor sandbox\n");
        return -1;
      }

      if(competitors->size == 0) {
        if(hashmap_remove(&competed_sandbox_set, event.key)) {
          printf("Error: fail to remove empty list\n");
          return -1;
        }

      }
      int tid,delayed_in_s = 0;
      for (struct linkedlist_element_s* node = competitors->head; node != NULL; node = node->next) {
        PSandbox* competitor_sandbox = (PSandbox *)(node->data);

        if (sandbox == node->data)
          continue;

        struct timeval current;
        gettimeofday(&current, NULL);
        long int current_tm = timeval_to_time(current);
        long int delaying_start_tm = timeval_to_time(competitor_sandbox->activity->delaying_start);
        long int execution_start_tm = timeval_to_time(competitor_sandbox->activity->execution_start);
        long int executing_time = current_tm - execution_start_tm;
        long int delayed_time = current_tm - delaying_start_tm;
//        printf("delayed_time  %lu, tid %lu\n", delayed_time,competitor_sandbox->tid);
//        printf("executing_time  %lu, tid %lu\n", executing_time,competitor_sandbox->tid);
        int num = psandbox_thread_map.size;
        if (delayed_time > executing_time*sandbox->delay_ratio*num) {
          delayed_in_s += delayed_time;
          tid = competitor_sandbox->tid;
          delayed_competitors++;

          break;
        }
      }

      if (delayed_competitors) {
        printf("give penalty to sandbox %d, delayed_in_s %lu\n", sandbox->tid,delayed_in_s);
//        syscall(SYS_PSANDBOX_WAKEUP, tid);
        syscall(SYS_PSANDBOX_PENALTY,delayed_in_s);
      }
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
    if (hashmap_put(&key_condition_map, key, condition)) {
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
  pSandbox->state = BOX_ACTIVE;
  gettimeofday(&pSandbox->activity->execution_start, NULL);
  pSandbox->activity->delayed_time.tv_usec = 0;
  pSandbox->activity->delayed_time.tv_sec = 0;
  pSandbox->activity->delaying_start.tv_usec = 0;
  pSandbox->activity->delaying_start.tv_sec = 0;
  return 1;
}

int pbox_freeze(PSandbox* pSandbox) {
  pSandbox->state = BOX_FREEZE;
  pSandbox->activity->delayed_time.tv_usec = 0;
  pSandbox->activity->delayed_time.tv_sec = 0;
  pSandbox->activity->delaying_start.tv_usec = 0;
  pSandbox->activity->delaying_start.tv_sec = 0;
  pSandbox->activity->execution_start.tv_usec = 0;
  pSandbox->activity->execution_start.tv_sec = 0;
  pSandbox->activity->queue_state = QUEUE_NULL;
  return 1;
}

PSandbox *pbox_get() {
  pid_t tid = syscall(SYS_gettid);
  void* element = hashmap_get(&psandbox_thread_map, tid);
  PSandbox *sandbox =  (PSandbox *)element;
  if (NULL == element) {
    printf("Error: Can't get sandbox for the thread %d\n",tid);
    return NULL;
  }
  return sandbox;
}
