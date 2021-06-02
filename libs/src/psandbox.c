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
#include <sys/time.h>
#include "syscall.h"

#define SYS_CREATE_PSANDBOX    436
#define SYS_RELEASE_PSANDBOX 437
#define SYS_GET_PSANDBOX 438
#define SYS_WAKEUP_PSANDBOX 439
#define SYS_PENALIZE_PSANDBOX 440

#define NSEC_PER_USEC    1000L
#define NSEC_PER_SEC 1000000000L
#define USEC_PER_SEC 1000000L

GHashTable *psandbox_map;
GHashTable *competitors_map;
GHashTable *interfered_competitors;
//GHashTable condition_map;
double **rules;

/* lock for updating the stats variables */
pthread_spinlock_t stats_lock = 0;

/// @brief Check whether the current activity is interfered or not
/// @param key The key of the queue
/// @param cond The condition for the current queue
/// @return On success 1 is returned.
int is_interfered(PSandbox *pSandbox, GList *competitors);

/// @brief Find the noisy neighbor
/// @parm p_sandbox the victim p_sandbox
/// @param competitors the competitors of the p_sandbox
/// @return The bid of the noisy neighbor
PSandbox *find_noisyNeighbor(PSandbox *p_sandbox, GList *competitors);

time_t get_penalty(PSandbox *psandbox, GList *competitors);

typedef struct timespec Time;

static inline Time getTime() {
  static struct timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);
  return time;
}

static inline Time timeAdd(Time t1, Time t2) {
  long sec = t2.tv_sec + t1.tv_sec;
  long nsec = t2.tv_nsec + t1.tv_nsec;
  if (nsec >= NSEC_PER_SEC) {
    nsec -= NSEC_PER_SEC;
    sec++;
  }
  return (Time) {.tv_sec = sec, .tv_nsec = nsec};
}

static inline Time timeDiff(Time start, Time stop) {
  struct timespec result;
  if ((stop.tv_nsec - start.tv_nsec) < 0) {
    result.tv_sec = stop.tv_sec - start.tv_sec - 1;
    result.tv_nsec = stop.tv_nsec - start.tv_nsec + 1000000000;
  } else {
    result.tv_sec = stop.tv_sec - start.tv_sec;
    result.tv_nsec = stop.tv_nsec - start.tv_nsec;
  }

  return result;
}

static inline void updateDefertime(PSandbox *p_sandbox) {
  struct timespec current_tm, defer_tm;
  pthread_spin_lock(&stats_lock);
  clock_gettime(CLOCK_REALTIME, &current_tm);
  defer_tm = timeDiff(p_sandbox->activity->delaying_start, current_tm);
  p_sandbox->activity->defer_time = timeAdd(defer_tm, p_sandbox->activity->defer_time);
  pthread_spin_unlock(&stats_lock);
}

static inline long time2ns(Time t1) {
  return t1.tv_sec * 1000000000L + t1.tv_nsec;
}

PSandbox *create_psandbox() {
  struct pSandbox *p_sandbox;
  long sandbox_id;
  gint *key = g_new(gint, 1);

  p_sandbox = (struct pSandbox *) malloc(sizeof(struct pSandbox));
  if (p_sandbox == NULL) return NULL;

  sandbox_id = syscall(SYS_CREATE_PSANDBOX);
//  sandbox_id = syscall(SYS_gettid);
  if (sandbox_id == -1) {
    free(p_sandbox);
    printf("syscall failed with errno: %s\n", strerror(errno));
    return NULL;
  }

  if ( pthread_spin_init (&stats_lock, PTHREAD_PROCESS_PRIVATE ) != 0 ) {
    exit ( 1 );
  }

  Activity *act = (Activity *) calloc(1, sizeof(Activity));
  p_sandbox->activity = act;
  p_sandbox->bid = sandbox_id;
  p_sandbox->state = BOX_START;
  p_sandbox->interference_threshold = 1.0;
  p_sandbox->activity_threshold = 0.01;
  p_sandbox->bad_activity = 0;
  p_sandbox->finished_activity = 0;
  p_sandbox->is_interference = 0;
  p_sandbox->compensation_ticket = 0;
  if (psandbox_map == NULL) {
    psandbox_map = g_hash_table_new(g_int_hash, g_int_equal);
  }

  *key = (int) (sandbox_id);
  g_hash_table_insert(psandbox_map, key, p_sandbox);
  return p_sandbox;
}

int release_psandbox(PSandbox *p_sandbox) {
  int success = 0;
  gint *key = g_new(gint, 1);
  if (!p_sandbox)
    return success;

  success = (int) syscall(SYS_RELEASE_PSANDBOX, p_sandbox->bid);

  if (success == -1) {
    free(p_sandbox);
    printf("failed to release sandbox in the kernel: %s\n", strerror(errno));
    return success;
  }
  *key = (int) p_sandbox->bid;
  g_hash_table_remove(psandbox_map, key);
  free(p_sandbox->activity);
  free(p_sandbox);
  return success;
}

int add_rules(int total_types, double *defer_rule) {
  if (!rules) {
    rules = (double **) malloc(sizeof(double *) * total_types);
    for (int i = 0; i < total_types; i++)
      rules[i] = (double *) malloc(sizeof(double) * total_types);
    if (!rules)
      return -1;
  }

  for (int i = 0; i < total_types; i++) {
    for (int j = 0; j < total_types; j++) {
      rules[i][j] = *(defer_rule + i * total_types + j);
    }
  }
  return 1;
}

int update_psandbox(BoxEvent *event, PSandbox *p_sandbox) {
  int success = 0;
  int event_type = event->event_type;
  unsigned long key = (unsigned long) event->key;
  GList *competitors, *interfered_psandboxs;

  if (!event->key)
    return -1;

  if (competitors_map == NULL) {
    competitors_map = g_hash_table_new(g_direct_hash, g_direct_equal);
  }

  if (interfered_competitors == NULL) {
    interfered_competitors = g_hash_table_new(g_direct_hash, g_direct_equal);
  }

  if (p_sandbox->state == BOX_FREEZE)
    return 0;

  switch (event_type) {
    case PREPARE_QUEUE:
    case MUTEX_REQUIRE: {
      pthread_spin_lock(&stats_lock);
      competitors = g_hash_table_lookup(competitors_map, GINT_TO_POINTER(key));
      competitors = g_list_append(competitors, p_sandbox);
      g_hash_table_insert(competitors_map, GINT_TO_POINTER(key), competitors);

      if (p_sandbox->state == BOX_INTERFERENCE) {
        interfered_psandboxs = g_hash_table_lookup(interfered_competitors, GINT_TO_POINTER(key));
        interfered_psandboxs = g_list_append(interfered_psandboxs, p_sandbox);
//        printf("PREPARE_QUEUE the list size is %d\n", g_list_length(interfered_psandboxs));
        g_hash_table_insert(interfered_competitors, GINT_TO_POINTER(key), interfered_psandboxs);
      }

      clock_gettime(CLOCK_REALTIME, &p_sandbox->activity->delaying_start);
      pthread_spin_unlock(&stats_lock);
      break;
    }
    case ENTER_QUEUE:
    case MUTEX_GET: {
      if (p_sandbox->state == BOX_INTERFERENCE) {
        pthread_spin_lock(&stats_lock);
        interfered_psandboxs = g_hash_table_lookup(interfered_competitors, GINT_TO_POINTER(key));
        interfered_psandboxs = g_list_remove(interfered_psandboxs, p_sandbox);
//        printf("ENTER_QUEUE the list size is %d\n", g_list_length(interfered_psandboxs));
        g_hash_table_insert(interfered_competitors, GINT_TO_POINTER(key), interfered_psandboxs);
        pthread_spin_unlock(&stats_lock);
      } else {
        interfered_psandboxs = g_hash_table_lookup(interfered_competitors, GINT_TO_POINTER(key));

        if (g_list_length(interfered_psandboxs)) {
//          printf("EXIT_QUEUE the list size is %d\n", g_list_length(interfered_psandboxs));
            (*(int *)event->key)--;
            syscall(SYS_PENALIZE_PSANDBOX, p_sandbox->bid, 100);
        }
      }

      updateDefertime(p_sandbox);
      break;
    }
    case EXIT_QUEUE: {
      pthread_spin_lock(&stats_lock);
      p_sandbox->activity->activity_state = QUEUE_EXIT;

      competitors = g_hash_table_lookup(competitors_map, GINT_TO_POINTER(key));
      if (!competitors) {
        printf("Error: fail to create competitor list \n");
        pthread_spin_unlock(&stats_lock);
        return -1;
      }
      competitors = g_list_remove(competitors, p_sandbox);
      g_hash_table_insert(competitors_map, GINT_TO_POINTER(key), competitors);


      pthread_spin_unlock(&stats_lock);
//      printf("psandbox %d running\n", p_sandbox->bid);
//      if (event->key_size != 1) {
//        if (p_sandbox->activity->is_preempted) {
//          (*(int *) event->key)++;
//          printf("1.thread %lu wakeup thread %lu\n", p_sandbox->bid, p_sandbox->noisy_neighbor->bid);
//          syscall(SYS_WAKEUP_PSANDBOX, p_sandbox->noisy_neighbor->bid);
//          p_sandbox->noisy_neighbor->state = BOX_ACTIVE;
//          p_sandbox->state = BOX_ACTIVE;
//          p_sandbox->activity->is_preempted = 0;
//        } else if (p_sandbox->state == BOX_COMPENSATED) {
//          p_sandbox->state = BOX_ACTIVE;
//          p_sandbox->noisy_neighbor->state = BOX_ACTIVE;
//          p_sandbox->noisy_neighbor->victim = 0;
//          p_sandbox->noisy_neighbor = 0;
//        } else if (p_sandbox->state == BOX_PENDING_PENALTY) {
//          p_sandbox->state = BOX_ACTIVE;
//          p_sandbox->victim->state = BOX_ACTIVE;
//          p_sandbox->victim->activity->activity_state = SHOULD_ENTER;
//          p_sandbox->victim->noisy_neighbor = 0;
//          p_sandbox->victim = 0;
//          p_sandbox->activity->is_preempted = 0;
//        }
//
//        penalty_ns = get_penalty(p_sandbox,competitors);
//        pthread_spin_unlock(&stats_lock);
//        if (penalty_ns > 1000) {
//          penalty_ns = 1000;
//          syscall(SYS_PENALIZE_PSANDBOX, p_sandbox->bid, penalty_ns);
//        }
//      } else {
//        // treat as a mutex release
//        GList* iterator = NULL;
//        int delayed_competitors = 0;
//
//        struct timespec current_time;
//        time_t executing_tm, defer_tm;
//
//        for (iterator = competitors; iterator; iterator = iterator->next) {
//          PSandbox *competitor = iterator->data;
//
//          if (p_sandbox == iterator->data)
//            continue;
//
//          clock_gettime(CLOCK_REALTIME, &current_time);
//
//          executing_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
//              competitor->activity->execution_time.tv_sec * NSEC_PER_SEC - competitor->activity->execution_time.tv_nsec;
//          defer_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec - competitor->activity->delaying_start.tv_sec * NSEC_PER_SEC
//              - competitor->activity->delaying_start.tv_nsec;
//
//          if (defer_tm >
//              (executing_tm - defer_tm) * p_sandbox->interference_threshold * g_list_length(competitors)) {
//            penalty_ns += defer_tm;
//            delayed_competitors++;
//          }
//        }
//
//        if (delayed_competitors > 0) {
//          syscall(SYS_PENALIZE_PSANDBOX, p_sandbox->bid, penalty_ns);
//        }
//      }
//

      break;
    }
    case MUTEX_RELEASE: {
      time_t penalty_ns;

      pthread_spin_lock(&stats_lock);
      competitors = g_hash_table_lookup(competitors_map, GINT_TO_POINTER(key));
      if (!competitors) {
        pthread_spin_unlock(&stats_lock);
        printf("Error: fail to get competitor list\n");
        return -1;
      }

      competitors = g_list_remove(competitors, p_sandbox);
      g_hash_table_insert(competitors_map, GINT_TO_POINTER(key), competitors);

//      if (p_sandbox->state == BOX_PENDING_PENALTY) {
//          if (p_sandbox->activity->owned_mutex == 0) {
//              // FIXME why -- to key since you basically replace that box w/ this one
//              // TODO still need this tmp value??? same in EXIT_QUEUE
//              penalty_ns = 100000;
//              printf("2.thread %lu sleep thread %lu\n", p_sandbox->victim->bid, p_sandbox->bid);
//              (*(int *)p_sandbox->victim->activity->key)--;
//              p_sandbox->victim->activity->is_preempted = 1;
//              p_sandbox->state = BOX_PREEMPTED;
//              pthread_spin_unlock(&stats_lock);
//              syscall(SYS_WAKEUP_PSANDBOX, p_sandbox->victim->bid);
//              syscall(SYS_PENALIZE_PSANDBOX, p_sandbox->bid, penalty_ns);
//              break;
//          }
//      } else if (p_sandbox->state == BOX_COMPENSATED || p_sandbox->state == BOX_PREEMPTED) {
//        break;
//      }
//
//      penalty_ns = get_penalty(p_sandbox,competitors);
      pthread_spin_unlock(&stats_lock);
//      if (penalty_ns > 100000) {
//        penalty_ns = 100000;
//        syscall(SYS_PENALIZE_PSANDBOX, p_sandbox->bid, penalty_ns);
//      }
      break;
    }
      //    case RETRY_QUEUE: {
//      if (event->key_size == 1) break;
//      pthread_spin_lock(&stats_lock);
//      competitors = g_hash_table_lookup(competitors_map, GINT_TO_POINTER(key));
//
//      if (!competitors) {
//        pthread_spin_unlock(&stats_lock);
//        printf("Error: fail to create competitor list\n");
//        return -1;
//      }
//
////      printf("get the competitor retry queue %d, activity_state %d\n",p_sandbox->bid,p_sandbox->activity->activity_state);
////      switch (cond->compare) {
////        case COND_LARGE:
////          if (*(int*)event.key <= cond->value) {
////            syscall(SYS_SCHEDULE_PSANDBOX, key,competitors);
////          }
////          break;
////        case COND_SMALL:
////          if (*(int*)event.key >= cond->value) {
////            syscall(SYS_SCHEDULE_PSANDBOX, key,competitors);
////          }
////          break;
////        case COND_LARGE_OR_EQUAL:
////          if (*(int*)event.key < cond->value) {
////            syscall(SYS_SCHEDULE_PSANDBOX, key,competitors);
////          }
////          break;
////        case COND_SMALL_OR_EQUAL:
////          if (*(int*)event.key > cond->value) {
////            syscall(SYS_SCHEDULE_PSANDBOX, key,competitors);
////          }
////          break;
////      }
//      if (p_sandbox->activity->activity_state == SHOULD_ENTER) {
//        pthread_spin_unlock(&stats_lock);
//        break;
//      }
//
//      if (is_interfered(p_sandbox, competitors)) {
//        PSandbox* noisy_neighbor;
//        noisy_neighbor = find_noisyNeighbor(p_sandbox, competitors);
//        if (noisy_neighbor) {
//          //Give penalty to the noisy neighbor
//          penalize_competitor(noisy_neighbor,p_sandbox,event);
//        }
//      }
//      pthread_spin_unlock(&stats_lock);
//    }
//      break;
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
//    case SLEEP_BEGIN: {
//      p_sandbox->activity->activity_state = QUEUE_SLEEP;
//      break;
//    }
//    case SLEEP_END: {
//      p_sandbox->activity->activity_state = QUEUE_AWAKE;
//      break;
//    }

    default:break;
  }
  return success;
}

//int psandbox_update_condition(int *keys, Condition cond) {
//  Condition *condition;
//  pthread_spin_lock(&stats_lock);
//  if (condition_map.table_size == 0) {
//    if (0 != hashmap_create(initial_size, &condition_map)) {
//      pthread_spin_unlock(&stats_lock);
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
//      pthread_spin_unlock(&stats_lock);
//      return -1;
//    }
//  } else {
//    condition->compare = cond.compare;
//    condition->value = cond.value;
//  }
//  pthread_spin_unlock(&stats_lock);
//  return 0;
//}

time_t get_penalty(PSandbox *psandbox, GList *competitors) {
  GList *iterator = NULL;
  struct timespec current_time;
  time_t executing_tm, defer_tm;
  time_t penalty_ns = -1;

  for (iterator = competitors; iterator; iterator = iterator->next) {
    PSandbox *competitor = (PSandbox *) iterator->data;

    if (competitor == NULL)
      continue;

    if (psandbox == competitor || competitor->activity->activity_state == QUEUE_ENTER
        || competitor->state == BOX_PREEMPTED || competitor->activity->is_preempted == 1
        || competitor->state == BOX_COMPENSATED || competitor->state == BOX_PENDING_PENALTY)
      continue;

    clock_gettime(CLOCK_REALTIME, &current_time);

    executing_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
        competitor->activity->execution_time.tv_sec * NSEC_PER_SEC - competitor->activity->execution_time.tv_nsec;
    defer_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
        competitor->activity->delaying_start.tv_sec * NSEC_PER_SEC
        - competitor->activity->delaying_start.tv_nsec;

    if (defer_tm >
        (executing_tm - defer_tm) * psandbox->interference_threshold * g_list_length(competitors)) {
      penalty_ns += defer_tm;
    }
  }
  return penalty_ns;
}

int is_interfered(PSandbox *psandbox, GList *competitors) {
  time_t executing_tm, delayed_tm;
  struct timespec current_time;

  if (!psandbox)
    return 0;

  if (psandbox->state == BOX_COMPENSATED || psandbox->state == BOX_PENDING_PENALTY
      || psandbox->activity->activity_state == QUEUE_ENTER)
    return 0;

  clock_gettime(CLOCK_REALTIME, &current_time);

  executing_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
      psandbox->activity->execution_time.tv_sec * NSEC_PER_SEC - psandbox->activity->execution_time.tv_nsec;
  delayed_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
      psandbox->activity->delaying_start.tv_sec * NSEC_PER_SEC - psandbox->activity->delaying_start.tv_nsec;

  //TODO: how to detect an interference
  if (delayed_tm > (executing_tm - delayed_tm) *
      psandbox->interference_threshold * g_list_length(competitors)) {
    return 1;
  }
  return 0;
}

//TODO: find the most noisy neighbor
PSandbox *find_noisyNeighbor(PSandbox *p_sandbox, GList *competitors) {
  GList *iterator = NULL;

  for (iterator = competitors; iterator; iterator = iterator->next) {
    PSandbox *competitor = iterator->data;

    // Don't wakeup itself
    if (p_sandbox == competitor || competitor->activity->activity_state != QUEUE_ENTER
        || competitor->state == BOX_PREEMPTED || competitor->activity->is_preempted == 1
        || p_sandbox->state == BOX_COMPENSATED || p_sandbox->state == BOX_PENDING_PENALTY)
      continue;

    return competitor;
  }
  return 0;
}

int wakeup_competitor(GList *competitors, PSandbox *sandbox) {
  GList *iterator;
  time_t executing_tm, delayed_tm;
  struct timespec current_time;

  for (iterator = competitors; iterator; iterator = iterator->next) {
    PSandbox *competitor_sandbox = iterator->data;

    // Don't wakeup itself
    if (sandbox == competitor_sandbox || competitor_sandbox->activity->activity_state != QUEUE_SLEEP)
      continue;

    clock_gettime(CLOCK_REALTIME, &current_time);
    executing_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
        competitor_sandbox->activity->execution_time.tv_sec * NSEC_PER_SEC
        - competitor_sandbox->activity->execution_time.tv_nsec;
    delayed_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
        competitor_sandbox->activity->delaying_start.tv_sec * NSEC_PER_SEC
        - competitor_sandbox->activity->delaying_start.tv_nsec;

    if ((float) delayed_tm > (float) (executing_tm - delayed_tm) *
        competitor_sandbox->interference_threshold * (float) g_list_length(competitors)) {
      syscall(SYS_WAKEUP_PSANDBOX, competitor_sandbox->bid);
      break;
    }
  }
  return 0;
}

int penalize_competitor(PSandbox *noisy_neighbor, PSandbox *victim, BoxEvent *event) {
  if (noisy_neighbor->activity->owned_mutex > 0) {
//    printf("the number of owned mutex is %d, victim %d, noisy %d\n",noisy_neighbor->activity->owned_mutex,victim->bid,noisy_neighbor->bid);
    //TODO:fixing the q-w-q case that would cause the noisy neighbor blocked in the queue.
    //TODO:fixing the wakeup from the futex problem
    victim->state = BOX_COMPENSATED;
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
      noisy_neighbor->state = BOX_PREEMPTED;
      victim->noisy_neighbor = noisy_neighbor;
      victim->activity->is_preempted = 1;
    }
  }
  return 0;
}

void active_psandbox(PSandbox *p_sandbox) {
  if (!p_sandbox)
    return;
  if (p_sandbox->is_interference) {
    p_sandbox->state = BOX_INTERFERENCE;
    p_sandbox->is_interference = 0;
  } else {
    p_sandbox->state = BOX_ACTIVE;
  }

  clock_gettime(CLOCK_REALTIME, &p_sandbox->activity->execution_start);
  p_sandbox->activity->activity_count = g_hash_table_size(psandbox_map) - 1;
}

void freeze_psandbox(PSandbox *p_sandbox) {
  struct timespec current_tm;
  if (!p_sandbox)
    return;

  if (p_sandbox->state == BOX_INTERFERENCE) {
    p_sandbox->state = BOX_FREEZE;
    if (p_sandbox->compensation_ticket > 1) {
      p_sandbox->is_interference = 1;
      p_sandbox->compensation_ticket--;
    }
  } else {
    p_sandbox->finished_activity++;
    p_sandbox->state = BOX_FREEZE;
    clock_gettime(CLOCK_REALTIME, &current_tm);
    pthread_spin_lock(&stats_lock);
    p_sandbox->activity->execution_time = timeDiff(p_sandbox->activity->execution_start, current_tm);
    pthread_spin_unlock(&stats_lock);

    if (p_sandbox->finished_activity > PROBING_NUMBER && (time2ns(p_sandbox->activity->execution_time) - time2ns(p_sandbox->activity->defer_time))
        * p_sandbox->interference_threshold *
        p_sandbox->activity->activity_count < time2ns(p_sandbox->activity->defer_time)) {
        p_sandbox->bad_activity++;

        if (p_sandbox->bad_activity > p_sandbox->activity_threshold / 2 * p_sandbox->finished_activity) {
          p_sandbox->bad_activity = 0;
          p_sandbox->finished_activity = 0;
          p_sandbox->is_interference = 1;
          printf("give a ticket for %d\n", p_sandbox->bid);
          p_sandbox->compensation_ticket = COMPENSATION_TICKET_NUMBER;
        }
      }
  }

  memset(p_sandbox->activity, 0, sizeof(Activity));

}

PSandbox *get_psandbox() {
  int bid = (int) syscall(SYS_GET_PSANDBOX);
//  int bid = syscall(SYS_gettid);
  gint *key = g_new(gint, 1);
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

int track_mutex(BoxEvent *event, PSandbox *p_sandbox) {
  int event_type = event->event_type;

  if (!event->key)
    return -1;
  if (!p_sandbox)
    return 0;
  if (p_sandbox->state == BOX_FREEZE)
    return 0;

  switch (event_type) {
    case PREPARE_QUEUE:
      if (event->key_size == 1) {
        p_sandbox->activity->owned_mutex++;
      } else {
        p_sandbox->activity->queue_event++;
      }
      break;
    case EXIT_QUEUE:
      if (event->key_size == 1) {
        p_sandbox->activity->owned_mutex--;
      } else {
        p_sandbox->activity->queue_event--;
      }
      break;
    case MUTEX_GET:p_sandbox->activity->owned_mutex++;
      break;
    case MUTEX_RELEASE:p_sandbox->activity->owned_mutex--;
      break;
    default:break;
  }
  return 1;
}

int track_time(BoxEvent *event, PSandbox *p_sandbox) {
  int event_type = event->event_type;

  if (!p_sandbox)
    return 0;
  if (p_sandbox->state == BOX_FREEZE)
    return 0;

  switch (event_type) {
    case PREPARE_QUEUE: {
      pthread_spin_lock(&stats_lock);
      clock_gettime(CLOCK_REALTIME, &p_sandbox->activity->delaying_start);
      pthread_spin_unlock(&stats_lock);
      break;
    }
    case ENTER_QUEUE: {
      updateDefertime(p_sandbox);
      break;
    }
    default:break;
  }
  return 1;
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
