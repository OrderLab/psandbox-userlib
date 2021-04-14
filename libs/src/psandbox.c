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
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <syscall.h>


#define SYS_CREATE_PSANDBOX	436
#define SYS_RELEASE_PSANDBOX 437
#define SYS_GET_PSANDBOX 438
#define SYS_WAKEUP_PSANDBOX 439
#define SYS_PENALIZE_PSANDBOX 440
#define SYS_UPDATE_PSANDBOX 441
#define SYS_SCHEDULE_PSANDBOX 443

#define NSEC_PER_SEC	1000000000L

const unsigned initial_size = 8;
HashMap psandbox_map;
HashMap competed_sandbox_set;
HashMap competitor_lists;

PSandbox *create_psandbox(float rule) {
  struct pSandbox *psandbox;
  long sandbox_id;

  psandbox = (struct pSandbox*)malloc(sizeof(struct pSandbox));

  if (psandbox == NULL) return NULL;
  sandbox_id = syscall(SYS_CREATE_PSANDBOX, rule);
//  int bid = syscall(SYS_gettid);
  if (sandbox_id == -1) {
    free(psandbox);
    printf("syscall failed with errno: %s\n", strerror(errno));
    return NULL;
  }
  Activity *act = (Activity *) calloc(1,sizeof(Activity));
  psandbox->activity = act;
  psandbox->bid = sandbox_id;
  psandbox->state = BOX_START;
  psandbox->delay_ratio = rule;

  if(psandbox_map.table_size == 0) {
    if (0 != hashmap_create(initial_size, &psandbox_map)) {
      free(psandbox);
      return NULL;
    }
  }

//  printf("set sandbox for the thread %d\n",tid);
  hashmap_put(&psandbox_map, sandbox_id, psandbox);
  return psandbox;
}

int release_psandbox(PSandbox* pSandbox){
  long success = 0;
  if (!pSandbox)
    return 0;

  success = syscall(SYS_RELEASE_PSANDBOX, pSandbox->bid);

  if(success == -1) {
    free(pSandbox);
    printf("failed to release sandbox in the kernel: %s\n", strerror(errno));
    return -1;
  }
  hashmap_remove(&psandbox_map, pSandbox->bid);
  free(pSandbox->activity);
  free(pSandbox);
  return 1;
}

int push_or_create_competitors(LinkedList* competitors, struct sandboxEvent *event, PSandbox* new_competitor) {
  int success = 0;
  unsigned long key = (unsigned long ) event->key;
  if (NULL == competitors) {
    competitors = (LinkedList *)malloc(sizeof(LinkedList));
    if (!competitors) {
      printf("Error: fail to create linked list\n");
      return -1;
    }

    success = list_push_front(competitors,new_competitor);
    hashmap_put(&competed_sandbox_set,key,competitors);
  } else {
    if(!list_find(competitors,new_competitor)) {
      success = list_push_front(competitors,new_competitor);
    }
  }

  return success;
}

int wakeup_competitor(LinkedList *competitors, PSandbox* sandbox) {
  struct linkedlist_element_s* node;
  time_t executing_tm, delayed_tm;
  struct timespec current_time;

  for (node = competitors->head; node != NULL; node = node->next) {
    PSandbox* competitor_sandbox = (PSandbox *)(node->data);

    // Don't wakeup itself
    if (sandbox == node->data || competitor_sandbox->activity->queue_state != QUEUE_SLEEP)
      continue;

    clock_gettime(CLOCK_REALTIME,&current_time);
    executing_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
        competitor_sandbox->activity->execution_start.tv_sec * NSEC_PER_SEC - competitor_sandbox->activity->execution_start.tv_nsec;
    delayed_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec  -
        competitor_sandbox->activity->delaying_start.tv_sec * NSEC_PER_SEC - competitor_sandbox->activity->delaying_start.tv_nsec;

    if (delayed_tm > (executing_tm - delayed_tm) *
        competitor_sandbox->delay_ratio * list_size(competitors)) {
      syscall(SYS_WAKEUP_PSANDBOX, competitor_sandbox->bid);
      break;
    }
  }
  return 0;
}

//todo: send the point to the kernel instead of the whole list
int update_psandbox(struct sandboxEvent event, PSandbox *psandbox) {
  int success = 0;
  int event_type = event.event_type;
  unsigned long key = (unsigned long)event.key;
  LinkedList *competitors;
  int p = 0;

  if(!event.key)
    return -1;

  if (competed_sandbox_set.table_size == 0) {
    if (0 != hashmap_create(initial_size, &competed_sandbox_set))
      return -1;
  }

  if (psandbox->state == BOX_FREEZE)
    return 0;

  switch (event_type) {
    case UPDATE_QUEUE_CONDITION: {
      Condition* cond;
      competitors = hashmap_get(&competed_sandbox_set, key);
      if(!competitors) {
        printf("Error: fail to find the competitor sandbox\n");
        return -1;
      }

      cond = hashmap_get(&competitor_lists, key);
      if(!cond) {
        printf("Error: fail to find condition\n");
        return -1;
      }

      switch (cond->compare) {
        case COND_LARGE:
          if (*(int*)event.key <= cond->value) {
            printf("wake up .. %d, %d\n", *(int*)event.key, cond->value);
            wakeup_competitor(competitors,psandbox);
          }
          break;
        case COND_SMALL:
          if (*(int*)event.key >= cond->value) {
            wakeup_competitor(competitors,psandbox);
          }
          break;
        case COND_LARGE_OR_EQUAL:
          if (*(int*)event.key < cond->value) {
            wakeup_competitor(competitors,psandbox);
          }
          break;
        case COND_SMALL_OR_EQUAL:
          if (*(int*)event.key > cond->value) {
            wakeup_competitor(competitors,psandbox);
          }
          break;
      }
      break;
    }
    case RETRY_QUEUE: {
      Condition* cond;
      int success;
      time_t executing_tm, delayed_tm;
      struct timespec current_time;

      //printf("--- pboxlib:  retry queue\n");

      competitors = hashmap_get(&competed_sandbox_set, key);

      if(!competitors) {
        printf("Error: fail to create competitor list\n");
        return -1;
      }

      cond = hashmap_get(&competitor_lists, key);
      if(!cond) {
        printf("Error: fail to find condition\n");
        return -1;
      }

//      switch (cond->compare) {
//        case COND_LARGE:
//          if (*(int*)event.key <= cond->value) {
//            syscall(SYS_SCHEDULE_PSANDBOX, key,competitors);
//          }
//          break;
//        case COND_SMALL:
//          if (*(int*)event.key >= cond->value) {
//            syscall(SYS_SCHEDULE_PSANDBOX, key,competitors);
//          }
//          break;
//        case COND_LARGE_OR_EQUAL:
//          if (*(int*)event.key < cond->value) {
//            syscall(SYS_SCHEDULE_PSANDBOX, key,competitors);
//          }
//          break;
//        case COND_SMALL_OR_EQUAL:
//          if (*(int*)event.key > cond->value) {
//            syscall(SYS_SCHEDULE_PSANDBOX, key,competitors);
//          }
//          break;
//      }

      clock_gettime(CLOCK_REALTIME,&current_time);
      executing_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
          psandbox->activity->execution_start.tv_sec * NSEC_PER_SEC - psandbox->activity->execution_start.tv_nsec;
      delayed_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec  -
          psandbox->activity->delaying_start.tv_sec * NSEC_PER_SEC - psandbox->activity->delaying_start.tv_nsec;

      if (delayed_tm > (executing_tm - delayed_tm) *
          psandbox->delay_ratio * list_size(competitors)) {
        struct linkedlist_element_s* node;
        for (node = competitors->head; node != NULL; node = node->next) {
          PSandbox* competitor_sandbox = (PSandbox *)(node->data);

          // Don't wakeup itself
          if (psandbox == node->data || competitor_sandbox->activity->queue_state != QUEUE_ENTER)
            continue;

          if(syscall(SYS_UPDATE_PSANDBOX, competitor_sandbox->bid, RETRY_QUEUE, 0) == 1) {
            cond->temp_value = *(int*)event.key;
            // FIXME why -- to key since you basically replace that box w/ this one
            // (*(int*)(event.key))--;
            psandbox->psandbox = competitor_sandbox;
            psandbox->activity->is_preempted = 1;
            break;
          }
        }
      }
    }
    break;
    case PREPARE_QUEUE:
      competitors = hashmap_get(&competed_sandbox_set, key);
      if(push_or_create_competitors(competitors,&event,psandbox)) {
        printf("Error: fail to create competitor list\n");
        return -1;
      }
      clock_gettime(CLOCK_REALTIME,&psandbox->activity->delaying_start);
      break;
    case ENTER_QUEUE: {
      time_t defer_tm;
      struct timespec current_time;
      psandbox->activity->queue_state = QUEUE_ENTER;
      clock_gettime(CLOCK_REALTIME,&current_time);
      psandbox->activity->defer_time.tv_sec +=  current_time.tv_sec - psandbox->activity->delaying_start.tv_sec;
          psandbox->activity->defer_time.tv_nsec += current_time.tv_nsec - psandbox->activity->delaying_start.tv_nsec;
      break;
    }
    case EXIT_QUEUE: {
      Condition* cond;

      psandbox->activity->queue_state = QUEUE_EXIT;
      competitors = hashmap_get(&competed_sandbox_set, key);
      if(!competitors) {
        printf("Error: fail to create competitor list\n");
        return -1;
      }

      if(list_remove(competitors,psandbox)) {
        printf("Error: fail to remove competitor sandbox\n");
        return -1;
      }

      if(competitors->size == 0) {
        if(hashmap_remove(&competed_sandbox_set, key) || hashmap_remove(&competitor_lists, key)) {
          printf("Error: fail to remove empty list\n");
          return -1;
        }

      }
      psandbox->activity->queue_state = QUEUE_NULL;

      if (psandbox->activity->is_preempted) {
        cond = hashmap_get(&competitor_lists, key);
        if(!cond) {
          printf("Error: fail to find condition\n");
          return -1;
        }

        (*(int*)event.key) = cond->temp_value;
        printf("... wake up p->p->bid\n");
        syscall(SYS_WAKEUP_PSANDBOX, psandbox->psandbox->bid);
      }
      break;
    }
    case SLEEP_BEGIN:
      psandbox->activity->queue_state=QUEUE_SLEEP;
      break;
    case SLEEP_END:
      psandbox->activity->queue_state=QUEUE_AWAKE;
      break;
    case MUTEX_REQUIRE:
      competitors = hashmap_get (&competed_sandbox_set, key);
      if (push_or_create_competitors (competitors,&event,psandbox)) {
        printf("Error: fail to create competitor list\n");
        return -1;
      }

      clock_gettime(CLOCK_REALTIME,&psandbox->activity->delaying_start);
      break;
    case MUTEX_GET: {
      struct timespec current_time;

      clock_gettime(CLOCK_REALTIME,&current_time);
      psandbox->activity->defer_time.tv_sec +=  current_time.tv_sec - psandbox->activity->delaying_start.tv_sec;
      psandbox->activity->defer_time.tv_nsec += current_time.tv_nsec - psandbox->activity->delaying_start.tv_nsec;
      break;
    }
    case MUTEX_RELEASE: {
      struct linkedlist_element_s* node;
      int delayed_competitors = 0;
      time_t penalty_ns = 0;
      struct timespec current_time;
      time_t executing_tm, defer_tm;

      competitors = hashmap_get(&competed_sandbox_set, key);
      if(!competitors) {
        printf("Error: fail to create competitor list\n");
        return -1;
      }

      if(list_remove(competitors,psandbox)) {
        printf("Error: fail to remove competitor sandbox\n");
        return -1;
      }

      if(competitors->size == 0) {
        if(hashmap_remove(&competed_sandbox_set, key)) {
          printf("Error: fail to remove empty list\n");
          return -1;
        }
      }
      for (node = competitors->head; node != NULL; node = node->next) {
        PSandbox* competitor_sandbox = (PSandbox *)(node->data);
        if (psandbox == node->data)
          continue;

        clock_gettime(CLOCK_REALTIME,&current_time);

        executing_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
            psandbox->activity->execution_start.tv_sec * NSEC_PER_SEC - psandbox->activity->execution_start.tv_nsec;
        defer_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec  -
            competitor_sandbox->activity->delaying_start.tv_sec * NSEC_PER_SEC - competitor_sandbox->activity->delaying_start.tv_nsec;

        if (defer_tm >
            (executing_tm - defer_tm) * psandbox->delay_ratio * list_size(competitors)) {
          penalty_ns += defer_tm;
          delayed_competitors++;
        }
      }

      if(delayed_competitors > 0) {
        syscall(SYS_PENALIZE_PSANDBOX, penalty_ns);
      }

      break;
    }

    default:break;
  }

  return success;
}

int psandbox_update_condition(int* keys, Condition cond){
  Condition *condition;

  if (competitor_lists.table_size == 0) {
    if (0 != hashmap_create(initial_size, &competitor_lists)) {
      return -1;
    }
  }
  condition = hashmap_get(&competitor_lists, keys);
  if (!condition) {
    condition = malloc(sizeof(Condition));
    condition->compare = cond.compare;
    condition->value = cond.value;
    if (hashmap_put(&competitor_lists, keys, condition)) {
      printf("Error: fail to add condition list\n");
      return -1;
    }
  } else {
    condition->compare = cond.compare;
    condition->value = cond.value;
  }
  return 0;
}

void active_psandbox(PSandbox* psandbox) {
  psandbox->state = BOX_ACTIVE;
  psandbox->activity->is_preempted = 0;
  clock_gettime(CLOCK_REALTIME,&psandbox->activity->execution_start);
  psandbox->activity->defer_time.tv_nsec = 0;
  psandbox->activity->defer_time.tv_sec = 0;
  psandbox->activity->delaying_start.tv_nsec = 0;
  psandbox->activity->delaying_start.tv_sec = 0;
}

void freeze_psandbox(PSandbox* psandbox) {
  psandbox->state = BOX_FREEZE;
  psandbox->activity->queue_state = QUEUE_NULL;
  psandbox->activity->is_preempted = 0;
  psandbox->activity->defer_time.tv_nsec = 0;
  psandbox->activity->defer_time.tv_sec = 0;
  psandbox->activity->delaying_start.tv_nsec = 0;
  psandbox->activity->delaying_start.tv_sec = 0;
  psandbox->activity->execution_start.tv_nsec = 0;
  psandbox->activity->execution_start.tv_sec = 0;
}

PSandbox *get_psandbox() {
  int bid = syscall(SYS_GET_PSANDBOX);
//  int bid = syscall(SYS_gettid);
  PSandbox *psandbox = (PSandbox *)hashmap_get(&psandbox_map, bid);

  if (NULL == psandbox) {
    printf("Error: Can't get sandbox for the thread %d\n",bid);
    return NULL;
  }
  return psandbox;
}
