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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <glib.h>
#include "syscall.h"

#define SYS_CREATE_PSANDBOX    436
#define SYS_RELEASE_PSANDBOX 437
#define SYS_GET_PSANDBOX 438
#define SYS_WAKEUP_PSANDBOX 439
#define SYS_PENALIZE_PSANDBOX 440

#define NSEC_PER_SEC    1000000000L

#define INDIRECT 1
#define DIRECT 0

//const unsigned initial_size = 8;
GHashTable *psandbox_map;
GHashTable *competitors_map;
//GHashTable condition_map;

/* mutex for updating the stats variables */
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

/* We need to keep values. */
struct competitor_node {
  long bid;
  PSandbox *p_sandbox;
  struct competitor_node *next;
};

typedef struct competitor_list {
  int size;
  struct competitor_node *head;
} Competitor_list;


/// @brief Check whether the current sandbox is interfered by other noisy_neighbor
/// @param key The key of the queue
/// @param cond The condition for the current queue
/// @return On success 1 is returned.
int is_interfered(PSandbox *pSandbox, Competitor_list *competitors, unsigned long key);

/// @brief Find the noisy neighbor
/// @parm p_sandbox the victim p_sandbox
/// @param competitors the competitors of the p_sandbox
/// @return The bid of the noisy neighbor
long find_noisyNeighbor(PSandbox *p_sandbox, Competitor_list *competitors, unsigned long key);

time_t get_penalty(PSandbox *psandbox, Competitor_list *competitors, unsigned long key);

// Insert an node into the head of competitor list.
int list_push_front(Competitor_list *const linkedlist, PSandbox *const value) {
  //create a node
  struct competitor_node *node =
      (struct competitor_node *) malloc(
          sizeof(struct competitor_node));
  if (!node) {
    printf("Error: fail to create linked list\n");
    return -1;
  }

  //point it to old first node
  node->next = linkedlist->head;
  node->bid = value->bid;
  node->p_sandbox = value;

  //point first to new first node
  linkedlist->head = node;
  linkedlist->size++;
  return 0;
}

//Check the size of the linked list.
int list_size(Competitor_list *const linkedlist) {
  return linkedlist->size;
}

//Find the node based on the bid.
struct competitor_node *list_find(Competitor_list *const linkedlist,
                                  long bid) {
  //start from the first link
  struct competitor_node *current_list = linkedlist->head;

  //if list is empty
  if (linkedlist->head == NULL) {
    return NULL;
  }

  //navigate through list
  while (current_list->bid != bid) {
    //if it is last node
    if (current_list->next == NULL) {
      return NULL;
    } else {
      //go to next link
      current_list = current_list->next;
    }
  }

  //if sandbox found, return the current Link
  return current_list;
}

//Remove an element from the competitor list
int list_remove(Competitor_list *const linkedlist, long bid) {
  //start from the first link
  struct competitor_node *current_list = linkedlist->head;
  struct competitor_node *previous = NULL;

  if (linkedlist->head == NULL) {
    return -1;
  }

  //navigate through list
  while (current_list->bid != bid) {
    //if it is last node
    if (current_list->next == NULL) {
      return -1;
    } else {
      //store reference to current link
      previous = current_list;
      current_list = current_list->next;

    }
  }

  //found a match, update the link
  if (current_list == linkedlist->head) {
    //change first to point to next link
    linkedlist->head = linkedlist->head->next;
  } else {
    //bypass the current link
    if(previous == NULL) {
      return -1;
    }
    previous->next = current_list->next;
  }

  linkedlist->size--;
  free(current_list);

  return 0;
}

PSandbox *create_psandbox() {
  struct pSandbox *psandbox;
  long sandbox_id;
  gint* key = g_new(gint, 1);

  psandbox = (struct pSandbox *) malloc(sizeof(struct pSandbox));

  if (psandbox == NULL) return NULL;
  sandbox_id = syscall(SYS_CREATE_PSANDBOX);
//   sandbox_id = syscall(SYS_gettid);

  if (sandbox_id == -1) {
    free(psandbox);
    printf("syscall failed with errno: %s\n", strerror(errno));
    return NULL;
  }
  Activity *act = (Activity *) calloc(1, sizeof(Activity));
  psandbox->activity = act;
  psandbox->bid = sandbox_id;
  psandbox->state = BOX_START;
  psandbox->_count = 0;

//  for (int i = 0; i < 1000; i++) {
//    psandbox->counts[i] = 0;
//  }
  if (psandbox_map == NULL) {
    psandbox_map = g_hash_table_new(g_int_hash, g_int_equal);
  }

  *key = sandbox_id;
  g_hash_table_insert(psandbox_map, key, psandbox);
  return psandbox;
}

int release_psandbox(PSandbox *pSandbox) {
  int success = 0;
  gint* key = g_new(gint, 1);
  if (!pSandbox)
    return success;

  success = syscall(SYS_RELEASE_PSANDBOX, pSandbox->bid);

  if (success == -1) {
    free(pSandbox);
    printf("failed to release sandbox in the kernel: %s\n", strerror(errno));
    return success;
  }
  *key = pSandbox->bid;
  g_hash_table_remove(psandbox_map, key);
  free(pSandbox->activity);
  free(pSandbox);
  return success;
}

int push_or_create_competitors(Competitor_list *competitors, struct sandboxEvent *event, PSandbox *new_competitor) {
  int success = 0;
  unsigned long key = (unsigned long) event->key;
  if (NULL == competitors) {
    competitors = (Competitor_list *) calloc(1, sizeof(Competitor_list));
    if (!competitors) {
      printf("Error: fail to create linked list\n");
      return -1;
    }

    success = list_push_front(competitors, new_competitor);
    g_hash_table_insert(competitors_map, GINT_TO_POINTER(key), GINT_TO_POINTER(competitors) );
  } else {
    if (!list_find(competitors, new_competitor->bid)) {
      success = list_push_front(competitors, new_competitor);
    } else {
      list_remove(competitors,new_competitor->bid);
      success = list_push_front(competitors, new_competitor);
    }
  }

  return success;
}

int wakeup_competitor(Competitor_list *competitors, PSandbox *sandbox) {
  struct competitor_node *node;
  time_t executing_tm, delayed_tm;
  struct timespec current_time;

  for (node = competitors->head; node != NULL; node = node->next) {
    PSandbox *competitor_sandbox = node->p_sandbox;

    // Don't wakeup itself
    if (sandbox == node->p_sandbox || competitor_sandbox->activity->queue_state != QUEUE_SLEEP)
      continue;

    clock_gettime(CLOCK_REALTIME, &current_time);
    executing_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
        competitor_sandbox->activity->execution_start.tv_sec * NSEC_PER_SEC
        - competitor_sandbox->activity->execution_start.tv_nsec;
    delayed_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
        competitor_sandbox->activity->delaying_start.tv_sec * NSEC_PER_SEC
        - competitor_sandbox->activity->delaying_start.tv_nsec;

    if (delayed_tm > (executing_tm - delayed_tm) *
        competitor_sandbox->delay_ratio * list_size(competitors)) {
      syscall(SYS_WAKEUP_PSANDBOX, competitor_sandbox->bid);
      break;
    }
  }
  return 0;
}

int penalize_competitor(PSandbox *noisy_neighbor, PSandbox *victim, struct sandboxEvent *event) {
  if (noisy_neighbor->activity->owned_mutex > 0) {
//    printf("the number of owned mutex is %d, victim %d, noisy %d\n",noisy_neighbor->activity->owned_mutex,victim->bid,noisy_neighbor->bid);
    //TODO:fixing the q-w-q case that would cause the noisy neighbor blocked in the queue.
    //TODO:fixing the wakeup from the futex problem
    victim->state = BOX_INTERFERED;
    victim->noisy_neighbor = noisy_neighbor;
    victim->activity->key = event->key;
    noisy_neighbor->state = BOX_PENDING_PENALTY;
    noisy_neighbor->victim = victim;
  } else {
    time_t penalty_us = 100000;
    if (syscall(SYS_PENALIZE_PSANDBOX, noisy_neighbor->bid, penalty_us) != -1) {
      // FIXME why -- to key since you basically replace that box w/ this one
      // TODO still need this tmp value??? same in EXIT_QUEUE
//      printf("1.thread %lu sleep thread %lu\n", victim->bid, noisy_neighbor->bid);
      (*(int *) event->key)--;
      noisy_neighbor->state = BOX_PENALIZED;
      victim->noisy_neighbor = noisy_neighbor;
      victim->activity->is_preempted = 1;
    }
  }

  return 0;
}

int track_mutex(struct sandboxEvent *event, PSandbox *p_sandbox) {
  int event_type = event->event_type;

  if (!event->key)
    return -1;
  if(!p_sandbox)
    return 0;
  if (p_sandbox->state == BOX_FREEZE)
    return 0;

  switch (event_type) {
    case PREPARE_QUEUE:
      if(event->key_size == 1) {
        p_sandbox->activity->owned_mutex++;
      } else {
        p_sandbox->activity->queue_event++;
      }
      break;
    case EXIT_QUEUE:
      if(event->key_size == 1) {
        p_sandbox->activity->owned_mutex--;
      } else {
        p_sandbox->activity->queue_event--;
      }
      break;
    case MUTEX_GET:
      p_sandbox->activity->owned_mutex++;
      break;
    case MUTEX_RELEASE:
      p_sandbox->activity->owned_mutex--;
      break;
    default:
      break;
  }
  return 1;
}

int update_psandbox(struct sandboxEvent *event, PSandbox *p_sandbox) {
  int success = 0;
  int event_type = event->event_type;
  unsigned long key = (unsigned long) event->key;
  Competitor_list *competitors;

  if (!event->key)
    return -1;

  if (competitors_map == NULL) {
    competitors_map = g_hash_table_new(g_direct_hash,g_direct_equal);
  }

  if (p_sandbox->state == BOX_FREEZE)
    return 0;

  switch (event_type) {
//    case UPDATE_QUEUE_CONDITION: {
//      Condition *cond;
//      competitors = hashmap_get(&competitors_map, key, DIRECT);
//      if (!competitors) {
//        printf("Error: fail to find the competitor sandbox\n");
//        return -1;
//      }

//      cond = hashmap_get(&condition_map, key, DIRECT);
//      if (!cond) {
//        printf("Error: fail to find condition\n");
//        return -1;
//      }
//
//      switch (cond->compare) {
//        case COND_LARGE:
//          if (*(int *) event->key <= cond->value) {
//            wakeup_competitor(competitors, p_sandbox);
//          }
//          break;
//        case COND_SMALL:
//          if (*(int *) event->key >= cond->value) {
//            wakeup_competitor(competitors, p_sandbox);
//          }
//          break;
//        case COND_LARGE_OR_EQUAL:
//          if (*(int *) event->key < cond->value) {
//            wakeup_competitor(competitors, p_sandbox);
//          }
//          break;
//        case COND_SMALL_OR_EQUAL:
//          if (*(int *) event->key > cond->value) {
//            wakeup_competitor(competitors, p_sandbox);
//          }
//          break;
//      }
//      break;
//    }
    case SLEEP_BEGIN: {
      p_sandbox->activity->queue_state = QUEUE_SLEEP;
      break;
    }
    case SLEEP_END: {
      p_sandbox->activity->queue_state = QUEUE_AWAKE;
      break;
    }
    case PREPARE_QUEUE: {
      pthread_mutex_lock(&stats_mutex);
      competitors = g_hash_table_lookup(competitors_map, GINT_TO_POINTER(key));

      clock_gettime(CLOCK_REALTIME, &p_sandbox->activity->delaying_start);


      //move the adding part after the push to fix the bug
      if (push_or_create_competitors(competitors, event, p_sandbox)) {
        printf("Error: fail to create competitor list\n");
        pthread_mutex_unlock(&stats_mutex);
        return -1;
      }
      pthread_mutex_unlock(&stats_mutex);

      break;
    }
    case RETRY_QUEUE: {
      if (event->key_size == 1) break;
      pthread_mutex_lock(&stats_mutex);

//      competitors = hashmap_get(&competitors_map, key, DIRECT);
//
//      if (!competitors) {
//        pthread_mutex_unlock(&stats_mutex);
//        printf("Error: fail to create competitor list\n");
//        return -1;
//      }

//      printf("get the competitor retry queue %d, queue_state %d\n",p_sandbox->bid,p_sandbox->activity->queue_state);
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
      if (p_sandbox->activity->queue_state == SHOULD_ENTER) {
        pthread_mutex_unlock(&stats_mutex);
        break;
      }

//      if (is_interfered(p_sandbox, competitors, key)) {
//        struct competitor_node *noisy_neighbor;
//        long bid = find_noisyNeighbor(p_sandbox, competitors,key);
//        if (bid) {
//          noisy_neighbor = list_find(competitors, bid);
//          //Give penalty to the noisy neighbor
//          penalize_competitor(noisy_neighbor->p_sandbox,p_sandbox,event);
//        }
//      }
      pthread_mutex_unlock(&stats_mutex);
    }
      break;
    case ENTER_QUEUE: {
      struct timespec current_time;

      p_sandbox->activity->queue_state = QUEUE_ENTER;
      pthread_mutex_lock(&stats_mutex);
      clock_gettime(CLOCK_REALTIME, &current_time);


      p_sandbox->activity->defer_time.tv_sec += current_time.tv_sec - p_sandbox->activity->delaying_start.tv_sec;
      p_sandbox->activity->defer_time.tv_nsec += current_time.tv_nsec - p_sandbox->activity->delaying_start.tv_nsec;
      pthread_mutex_unlock(&stats_mutex);
      break;
    }
    case EXIT_QUEUE: {
      pthread_mutex_lock(&stats_mutex);
      time_t penalty_ns;
      p_sandbox->activity->queue_state = QUEUE_EXIT;
      competitors = g_hash_table_lookup(competitors_map, GINT_TO_POINTER(key));

      if (!competitors) {
        printf("Error: fail to create competitor list \n");
        pthread_mutex_unlock(&stats_mutex);
        return -1;
      }

      if (list_remove(competitors, p_sandbox->bid)) {
//        printf("Error: fail to remove competitors list\n");
        pthread_mutex_unlock(&stats_mutex);
        return -1;
      }

      if (competitors->size == 0) {
        if (g_hash_table_remove(competitors_map, GINT_TO_POINTER(key)) ) {
//          || hashmap_remove(&condition_map, key)
          printf("Error: fail to remove empty list\n");
          pthread_mutex_unlock(&stats_mutex);
          return -1;
        }
      }

      if (event->key_size != 1) {
        if (p_sandbox->activity->is_preempted) {
          (*(int *) event->key)++;
          printf("1.thread %lu wakeup thread %lu\n", p_sandbox->bid, p_sandbox->noisy_neighbor->bid);
          syscall(SYS_WAKEUP_PSANDBOX, p_sandbox->noisy_neighbor->bid);
          p_sandbox->noisy_neighbor->state = BOX_ACTIVE;
          p_sandbox->state = BOX_ACTIVE;
          p_sandbox->activity->is_preempted = 0;
        } else if (p_sandbox->state == BOX_INTERFERED) {
          p_sandbox->state = BOX_ACTIVE;
          p_sandbox->noisy_neighbor->state = BOX_ACTIVE;
          p_sandbox->noisy_neighbor->victim = 0;
          p_sandbox->noisy_neighbor = 0;
        } else if (p_sandbox->state == BOX_PENDING_PENALTY) {
          p_sandbox->state = BOX_ACTIVE;
          p_sandbox->victim->state = BOX_ACTIVE;
          p_sandbox->victim->activity->queue_state = SHOULD_ENTER;
          p_sandbox->victim->noisy_neighbor = 0;
          p_sandbox->victim = 0;
          p_sandbox->activity->is_preempted = 0;
        }

        penalty_ns = get_penalty(p_sandbox,competitors,key);
        pthread_mutex_unlock(&stats_mutex);
        if (penalty_ns > 1000) {
          penalty_ns = 1000;
          syscall(SYS_PENALIZE_PSANDBOX, p_sandbox->bid, penalty_ns);
        }
      } else {
        // treat as a mutex release
        struct competitor_node *node;
        int delayed_competitors = 0;
        time_t penalty_ns = 0;
        struct timespec current_time;
        time_t executing_tm, defer_tm;

        for (node = competitors->head; node != NULL; node = node->next) {
          PSandbox *competitor = node->p_sandbox;
          if (p_sandbox == node->p_sandbox)
            continue;

          clock_gettime(CLOCK_REALTIME, &current_time);

          executing_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
              p_sandbox->activity->execution_start.tv_sec * NSEC_PER_SEC - p_sandbox->activity->execution_start.tv_nsec;
          defer_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec - p_sandbox->activity->delaying_start.tv_sec * NSEC_PER_SEC
              - p_sandbox->activity->delaying_start.tv_nsec;

          if (defer_tm >
              (executing_tm - defer_tm) * p_sandbox->delay_ratio * list_size(competitors)) {
            penalty_ns += defer_tm;
            delayed_competitors++;
          }
        }

        if (delayed_competitors > 0) {
          syscall(SYS_PENALIZE_PSANDBOX, p_sandbox->bid, penalty_ns);
        }
      }

      pthread_mutex_unlock(&stats_mutex);
      break;
    }
    case MUTEX_REQUIRE: {
      pthread_mutex_lock(&stats_mutex);
      competitors = g_hash_table_lookup(competitors_map, GINT_TO_POINTER(key));

      if (push_or_create_competitors(competitors, event, p_sandbox)) {
        pthread_mutex_unlock(&stats_mutex);
        printf("Error: fail to create competitor list\n");
        return -1;
      }
      clock_gettime(CLOCK_REALTIME, &(p_sandbox->activity->delaying_start));
      pthread_mutex_unlock(&stats_mutex);
      break;
    }
    case MUTEX_GET: {
      struct timespec current_time;
      pthread_mutex_lock(&stats_mutex);
      clock_gettime(CLOCK_REALTIME, &current_time);

      p_sandbox->activity->defer_time.tv_sec += current_time.tv_sec - p_sandbox->activity->delaying_start.tv_sec;
      p_sandbox->activity->defer_time.tv_nsec += current_time.tv_nsec - p_sandbox->activity->delaying_start.tv_nsec;
      pthread_mutex_unlock(&stats_mutex);
      break;
    }
    case MUTEX_RELEASE: {
      time_t penalty_ns ;

      pthread_mutex_lock(&stats_mutex);
      competitors = g_hash_table_lookup(competitors_map, GINT_TO_POINTER(key));
      if (!competitors) {
        pthread_mutex_unlock(&stats_mutex);
        printf("Error: fail to get competitor list\n");
        return -1;
      }

      if (list_remove(competitors, p_sandbox->bid)) {
        pthread_mutex_unlock(&stats_mutex);
        printf("Error: fail to remove competitor sandbox\n");
        return -1;
      }

      if (competitors->size == 0) {
        if (!g_hash_table_remove(competitors_map, GINT_TO_POINTER(key))) {
          pthread_mutex_unlock(&stats_mutex);
          printf("Error: fail to remove empty list\n");
          return -1;
        }
        pthread_mutex_unlock(&stats_mutex);
        return 0;
      }

      if (p_sandbox->state == BOX_PENDING_PENALTY) {
          if (p_sandbox->activity->owned_mutex == 0) {
              // FIXME why -- to key since you basically replace that box w/ this one
              // TODO still need this tmp value??? same in EXIT_QUEUE
              time_t penalty_ns = 100000;
              printf("2.thread %lu sleep thread %lu\n", p_sandbox->victim->bid, p_sandbox->bid);
              (*(int *)p_sandbox->victim->activity->key)--;
              p_sandbox->victim->activity->is_preempted = 1;
              p_sandbox->state = BOX_PENALIZED;
              pthread_mutex_unlock(&stats_mutex);
              syscall(SYS_WAKEUP_PSANDBOX, p_sandbox->victim->bid);
              syscall(SYS_PENALIZE_PSANDBOX, p_sandbox->bid, penalty_ns);
              break;
          }
      } else if (p_sandbox->state == BOX_INTERFERED || p_sandbox->state == BOX_PENALIZED) {
        break;
      }

      penalty_ns = get_penalty(p_sandbox,competitors,key);
      pthread_mutex_unlock(&stats_mutex);
      if (penalty_ns > 100000) {
        penalty_ns = 100000;
//        printf("call SYS_PENALIZE_PSANDBOX\n");
        syscall(SYS_PENALIZE_PSANDBOX, p_sandbox->bid, penalty_ns);
      }

      break;
    }
    default:break;
  }
  return success;
}

//int psandbox_update_condition(int *keys, Condition cond) {
//  Condition *condition;
//  pthread_mutex_lock(&stats_mutex);
//  if (condition_map.table_size == 0) {
//    if (0 != hashmap_create(initial_size, &condition_map)) {
//      pthread_mutex_unlock(&stats_mutex);
//      return -1;
//    }
//  }
//  condition = hashmap_get(&condition_map, keys, DIRECT);
//  if (!condition) {
//    condition = malloc(sizeof(Condition));
//    condition->compare = cond.compare;
//    condition->value = cond.value;
//    if (hashmap_put(&condition_map, keys, condition, INDIRECT)) {
//      printf("Error: fail to add condition list\n");
//      pthread_mutex_unlock(&stats_mutex);
//      return -1;
//    }
//  } else {
//    condition->compare = cond.compare;
//    condition->value = cond.value;
//  }
//  pthread_mutex_unlock(&stats_mutex);
//  return 0;
//}

time_t get_penalty(PSandbox *psandbox, Competitor_list *competitors, unsigned long key) {
  struct competitor_node *node;
  struct timespec current_time;
  time_t executing_tm, defer_tm;
  time_t penalty_ns = -1;
  for (node = competitors->head; node != NULL; node = node->next) {
    PSandbox *competitor = node->p_sandbox;

    if(competitor == NULL)
      continue;

    if (psandbox == competitor || competitor->activity->queue_state == QUEUE_ENTER
        || competitor->state == BOX_PENALIZED || competitor->activity->is_preempted == 1
        || competitor->state == BOX_INTERFERED || competitor->state == BOX_PENDING_PENALTY)
      continue;

    clock_gettime(CLOCK_REALTIME, &current_time);


    executing_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
        competitor->activity->execution_start.tv_sec * NSEC_PER_SEC - competitor->activity->execution_start.tv_nsec;
    defer_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
        competitor->activity->delaying_start.tv_sec * NSEC_PER_SEC
        - competitor->activity->delaying_start.tv_nsec;

    if (defer_tm >
        (executing_tm - defer_tm) * psandbox->delay_ratio * list_size(competitors)) {
      penalty_ns += defer_tm;
    }
  }
  return penalty_ns;
}

int is_interfered(PSandbox *psandbox, Competitor_list *competitors, unsigned long key) {
  time_t executing_tm, delayed_tm;
  struct timespec current_time;
  struct timespec *delaying_start;

  if (!psandbox)
    return 0;

  if (psandbox->state == BOX_INTERFERED || psandbox->state == BOX_PENDING_PENALTY || psandbox->activity->queue_state == QUEUE_ENTER)
    return 0;

  clock_gettime(CLOCK_REALTIME, &current_time);

  executing_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
      psandbox->activity->execution_start.tv_sec * NSEC_PER_SEC - psandbox->activity->execution_start.tv_nsec;
  delayed_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
      psandbox->activity->delaying_start.tv_sec * NSEC_PER_SEC - psandbox->activity->delaying_start.tv_nsec;

  //TODO: how to detect an interference
  if (delayed_tm > (executing_tm - delayed_tm) *
      psandbox->delay_ratio * list_size(competitors)) {
    return 1;
  }
  return 0;
}

//TODO: find the most noisy neighbor
long find_noisyNeighbor(PSandbox *p_sandbox, Competitor_list *competitors, unsigned long key) {
  struct competitor_node *node;

  for (node = competitors->head; node != NULL; node = node->next) {
    PSandbox *competitor = node->p_sandbox;

    // Don't wakeup itself
    if (p_sandbox == competitor || competitor->activity->queue_state != QUEUE_ENTER
        || competitor->state == BOX_PENALIZED || competitor->activity->is_preempted == 1
        || p_sandbox->state == BOX_INTERFERED || p_sandbox->state == BOX_PENDING_PENALTY)
      continue;

    return competitor->bid;
  }
  return 0;
}

void active_psandbox(PSandbox *psandbox) {
  if (!psandbox)
    return;
  psandbox->state = BOX_ACTIVE;
  clock_gettime(CLOCK_REALTIME, &psandbox->activity->execution_start);
//  if (0 != hashmap_create(initial_size, &psandbox->activity->delaying_starts)) {
//    return;
//  }
}

void freeze_psandbox(PSandbox *psandbox) {
  if (!psandbox)
    return;
  psandbox->state = BOX_FREEZE;
  memset(psandbox->activity, 0, sizeof(Activity));
}

PSandbox *get_psandbox() {
  int bid = syscall(SYS_GET_PSANDBOX);
//  int bid = syscall(SYS_gettid);
  gint* key = g_new(gint, 1);
  (*key) = bid;
  if (bid == -1)
    return NULL;

  PSandbox *psandbox = (PSandbox *) g_hash_table_lookup(psandbox_map, key);

  if (NULL == psandbox) {
    printf("Error: Can't get sandbox for the thread %d\n", bid);
    return NULL;
  }
  return psandbox;
}

//void print_all(){
//  int counts[1000],small_counts[10];
//  int _count;
//  int i;
//
//  _count = 0;
//  for ( i = 0; i < 1000; i++) {
//    counts[i] = 0;
//  }
//
//  for ( i = 0; i < 10; i++) {
//    small_counts[i] = 0;
//  }
//
//  for (i = 0; i < psandbox_map.table_size; i++) {
//    if (psandbox_map.data[i].in_use) {
//      _count += ((PSandbox *)psandbox_map.data[i].data)->_count;
//      for(int j = 0; j < 1000; j++) {
//        counts[j] += ((PSandbox *)psandbox_map.data[i].data)->counts[j];
//
//      }
//      for (int j = 0; j < 10; ++j) {
//        small_counts[j]=  ((PSandbox *)psandbox_map.data[i].data)->small_counts[j];
//      }
//    }
//  }
//
//    printf("Latency histogram (values are in milliseconds)\n");
//    printf("value -- count\n");
//    for(i = 0; i < 1000; i ++) {
//      if (counts[i])
//        printf("<%ums,%ums>| %u\n",i,i+1,counts[i]);
//    }
//    if (_count > 0) {
//      printf("<%us,>| %u\n",i,_count);
//    }
//}
