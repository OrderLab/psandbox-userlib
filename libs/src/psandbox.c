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
#include <sys/time.h>
#include <syscall.h>

#define SYS_CREATE_PSANDBOX	436
#define SYS_RELEASE_PSANDBOX 437
#define SYS_GET_PSANDBOX 438
#define SYS_ACTIVE_PSANDBOX 439
#define SYS_FREEZE_PSANDBOX 440
#define SYS_UPDATE_PSANDBOX 441
#define SYS_DESTROY_PSANDBOX 442

#define USEC_PER_SEC	1000000L

const unsigned initial_size = 8;
HashMap psandbox_map;
HashMap competed_sandbox_set;
HashMap key_condition_map;

PSandbox *create_psandbox(int rule) {
  struct pSandbox *psandbox;
  long sandbox_id;

  psandbox = (struct pSandbox*)malloc(sizeof(struct pSandbox));

  if (psandbox == NULL) return NULL;
  sandbox_id = syscall(SYS_CREATE_PSANDBOX, rule);

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

long int timeval_to_time(struct timeval tm) {
  return tm.tv_sec * USEC_PER_SEC + tm.tv_usec;
}

struct timeval time_to_timeval(long int tm) {
  struct timeval t;
  t.tv_sec = tm / 1000000;
  t.tv_usec = tm % 1000000;
  return t;
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
  for (node = competitors->head; node != NULL; node = node->next) {
    PSandbox* competitor_sandbox = (PSandbox *)(node->data);

    // Don't wakeup itself
    if (sandbox == node->data || competitor_sandbox->activity->queue_state != QUEUE_SLEEP)
      continue;
    syscall(SYS_UPDATE_PSANDBOX, competitor_sandbox->bid,UPDATE_QUEUE_CONDITION,list_size(competitors));
  }
  return 0;
}

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

      cond = hashmap_get(&key_condition_map, key);
      if(!cond) {
        printf("Error: fail to find condition\n");
        return -1;
      }

      switch (cond->compare) {
        case COND_LARGE:
          if (*(int*)event.key <= cond->value) {
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
    case TRY_QUEUE: {
      Condition* cond;
      int success;

      competitors = hashmap_get(&competed_sandbox_set, key);

      if(!competitors) {
        printf("Error: fail to create competitor list\n");
        return -1;
      }

      cond = hashmap_get(&key_condition_map, key);
      if(!cond) {
        printf("Error: fail to find condition\n");
        return -1;
      }

      success = syscall(SYS_UPDATE_PSANDBOX, psandbox->bid,TRY_QUEUE,0);
      if(success == 1) {
        struct linkedlist_element_s* node;
        for (node = competitors->head; node != NULL; node = node->next) {
          PSandbox* competitor_sandbox = (PSandbox *)(node->data);

          // Don't wakeup itself
          if (psandbox == node->data || competitor_sandbox->activity->queue_state != QUEUE_ENTER)
            continue;


          if(syscall(SYS_UPDATE_PSANDBOX, competitor_sandbox->bid,TRY_QUEUE,0) == 1) {
            cond->temp_value = *(int*)event.key;
            (*(int*)(event.key))--;
            psandbox->psandbox = competitor_sandbox;
            psandbox->activity->is_preempted = 1;
            break;
          }
        }
      }
    }
    break;
    case START_QUEUE:
      competitors = hashmap_get(&competed_sandbox_set, key);
      if(push_or_create_competitors(competitors,&event,psandbox)) {
        printf("Error: fail to create competitor list\n");
        return -1;
      }
      syscall(SYS_UPDATE_PSANDBOX, psandbox->bid, START_QUEUE, p);
      break;
    case ENTER_QUEUE: {
      psandbox->activity->queue_state = QUEUE_ENTER;
      syscall(SYS_UPDATE_PSANDBOX, psandbox->bid,ENTER_QUEUE,p);
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
        if(hashmap_remove(&competed_sandbox_set, key) || hashmap_remove(&key_condition_map,key)) {
          printf("Error: fail to remove empty list\n");
          return -1;
        }

      }
      psandbox->activity->queue_state = QUEUE_NULL;

      if (psandbox->activity->is_preempted) {
        cond = hashmap_get(&key_condition_map, key);
        if(!cond) {
          printf("Error: fail to find condition\n");
          return -1;
        }

        (*(int*)event.key) = cond->temp_value;
        syscall(SYS_UPDATE_PSANDBOX, psandbox->psandbox->bid,EXIT_QUEUE,1);
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
      syscall(SYS_UPDATE_PSANDBOX, psandbox->bid,MUTEX_REQUIRE,p);
      break;
    case MUTEX_GET: {
      syscall(SYS_UPDATE_PSANDBOX, psandbox->bid,MUTEX_GET,p);
      break;
    }
    case MUTEX_RELEASE: {
      struct linkedlist_element_s* node;
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
        syscall(SYS_UPDATE_PSANDBOX, competitor_sandbox->bid,MUTEX_RELEASE,list_size(competitors));
      }

      break;
    }

    default:break;
  }

  return success;
}

int pbox_update_condition(int* keys, Condition cond){
  Condition *condition;

  if (key_condition_map.table_size == 0) {
    if (0 != hashmap_create(initial_size, &key_condition_map)) {
      return -1;
    }
  }
  condition = hashmap_get(&key_condition_map, keys);
  if (!condition) {
    condition = malloc(sizeof(Condition));
    condition->compare = cond.compare;
    condition->value = cond.value;
    if (hashmap_put(&key_condition_map, keys, condition)) {
      printf("Error: fail to add condition list\n");
      return -1;
    }
  } else {
    condition->compare = cond.compare;
    condition->value = cond.value;
  }
  return 0;
}

int active_psandbox(PSandbox* psandbox) {
  int success = syscall(SYS_ACTIVE_PSANDBOX,psandbox->bid);
  psandbox->state = BOX_ACTIVE;
  psandbox->activity->is_preempted = 0;
  return success;
}

int freeze_psandbox(PSandbox* psandbox) {
  int success = syscall(SYS_FREEZE_PSANDBOX,psandbox->bid);
  psandbox->state = BOX_FREEZE;
  psandbox->activity->queue_state = QUEUE_NULL;
  psandbox->activity->is_preempted = 0;
  return success;
}

PSandbox *get_psandbox() {
  int bid = syscall(SYS_GET_PSANDBOX);
  PSandbox *psandbox = (PSandbox *)hashmap_get(&psandbox_map, bid);

  if (NULL == psandbox) {
    printf("Error: Can't get sandbox for the thread %d\n",bid);
    return NULL;
  }
  return psandbox;
}
