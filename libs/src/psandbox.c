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
#include <signal.h>

#define SYS_CREATE_PSANDBOX    436
#define SYS_RELEASE_PSANDBOX 437
#define SYS_GET_PSANDBOX 438
#define SYS_WAKEUP_PSANDBOX 439
#define SYS_PENALIZE_PSANDBOX 440
#define SYS_COMPENSATE_PSANDBOX 441
#define SYS_START_MANAGER 442
#define SYS_ACTIVE_PSANDBOX 443
#define SYS_FREEZE_PSANDBOX 444
#define SYS_UPDATE_EVENT 445

#define NSEC_PER_USEC    1000L
#define NSEC_PER_SEC 1000000000L
#define USEC_PER_SEC 1000000L

GHashTable *psandbox_map;
GHashTable *competitors_map;
GHashTable *interfered_competitors;
GHashTable *holders_map;
//GHashTable condition_map;
double **rules;

/* lock for updating the stats variables */
pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

/// @brief Check whether the current activity is interfered or not
/// @param key The key of the queue
/// @param cond The condition for the current queue
/// @return On success 1 is returned.
int is_interfered(PSandbox *pSandbox, BoxEvent *event, GList *competitors);

/// @brief Find the noisy neighbor
/// @parm p_sandbox the victim p_sandbox
/// @param competitors the competitors of the p_sandbox
/// @return The bid of the noisy neighbor
PSandbox *find_noisyNeighbor(PSandbox *p_sandbox, GList *competitors);

time_t get_penalty(PSandbox *psandbox, GList *competitors);

int penalize_competitor(PSandbox *noisy_neighbor, PSandbox *victim, BoxEvent *event);

typedef struct timespec Time;

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
  clock_gettime(CLOCK_REALTIME, &current_tm);
  defer_tm = timeDiff(p_sandbox->activity->delaying_start, current_tm);
  p_sandbox->activity->defer_time = timeAdd(defer_tm, p_sandbox->activity->defer_time);
}

static inline long time2ns(Time t1) {
  return t1.tv_sec * 1000000000L + t1.tv_nsec;
}

int psandbox_manager_init() {
  syscall(SYS_START_MANAGER,&stats_lock);
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

  Activity *act = (Activity *) calloc(1, sizeof(Activity));
  p_sandbox->activity = act;
  p_sandbox->bid = sandbox_id;
  p_sandbox->state = BOX_START;
  p_sandbox->max_defer = 1.0;
  p_sandbox->tail_threshold = 0.2;
  p_sandbox->bad_activities = 0;
  p_sandbox->finished_activities = 0;
  p_sandbox->action_level = LOW_PRIORITY;
  p_sandbox->compensation_ticket = 0;
  p_sandbox->total_activity = 0;

  if (psandbox_map == NULL) {
    psandbox_map = g_hash_table_new(g_int_hash, g_int_equal);
  }

  *key = (int) (sandbox_id);

  pthread_mutex_lock(&stats_lock);
  g_hash_table_insert(psandbox_map, key, p_sandbox);
  pthread_mutex_unlock(&stats_lock);

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

PSandbox *get_psandbox() {
  int bid = (int) syscall(SYS_GET_PSANDBOX);
//  int bid = syscall(SYS_gettid);
  gint *key = g_new(gint, 1);
  (*key) = bid;
  if (bid == -1)
    return NULL;

  pthread_mutex_lock(&stats_lock);
  PSandbox *psandbox = (PSandbox *) g_hash_table_lookup(psandbox_map, key);
  pthread_mutex_unlock(&stats_lock);
  if (NULL == psandbox) {
    printf("Error: Can't get sandbox for the thread %d\n", bid);
    return NULL;
  }

  return psandbox;
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
  GList *competitors, *interfered_psandboxs, *holders;

  if (!event->key || !p_sandbox || !p_sandbox->activity) {
    return -1;
  }

  if (competitors_map == NULL) {
    competitors_map = g_hash_table_new(g_direct_hash, g_direct_equal);
  }

  if (holders_map == NULL) {
    holders_map = g_hash_table_new(g_direct_hash, g_direct_equal);
  }

  if (interfered_competitors == NULL) {
    interfered_competitors = g_hash_table_new(g_direct_hash, g_direct_equal);
  }

  if (p_sandbox->state == BOX_FREEZE)
    return 0;


  switch (event_type) {
    case PREPARE:{
//      pthread_mutex_lock(&stats_lock);
//      clock_gettime(CLOCK_REALTIME, &p_sandbox->activity->delaying_start);
//      p_sandbox->activity->activity_state = QUEUE_WAITING;

//      competitors = g_hash_table_lookup(competitors_map, GINT_TO_POINTER(key));
//      competitors = g_list_append(competitors, p_sandbox);
//      g_hash_table_insert(competitors_map, GINT_TO_POINTER(key), competitors);
//      printf("PREPARE the size is %d, the key is %d, %d\n",g_list_length(competitors), (*(int *)event->key),p_sandbox->bid);
//      if (p_sandbox->action_level == HIGHEST_PRIORITY) {
//        interfered_psandboxs = g_hash_table_lookup(interfered_competitors, GINT_TO_POINTER(key));
//        interfered_psandboxs = g_list_append(interfered_psandboxs, p_sandbox);
//        g_hash_table_insert(interfered_competitors, GINT_TO_POINTER(key), interfered_psandboxs);
//      }
      syscall(SYS_UPDATE_EVENT,event,p_sandbox->bid);
//      pthread_mutex_unlock(&stats_lock);
      break;
    }
    case ENTER:{
//      GList *iterator;
      syscall(SYS_UPDATE_EVENT,event,p_sandbox->bid);
//      printf("ENTER get the mutex psandbox %d,the key is %d\n",p_sandbox->bid,(*(int *)event->key));
//      pthread_mutex_lock(&stats_lock);
//      competitors = g_hash_table_lookup(competitors_map, GINT_TO_POINTER(key));
//      competitors = g_list_remove(competitors, p_sandbox);
//      g_hash_table_insert(competitors_map, GINT_TO_POINTER(key), competitors);
//      p_sandbox->activity->activity_state = QUEUE_ENTER;
//
//      holders = g_hash_table_lookup(holders_map, GINT_TO_POINTER(key));
//      holders = g_list_append(holders, p_sandbox);
//      g_hash_table_insert(holders_map, GINT_TO_POINTER(key), holders);
//
////      printf("ENTER the size is %d,the key is %d, %d\n",g_list_length(competitors),(*(int *)event->key),p_sandbox->bid);
//      if (p_sandbox->action_level == HIGHEST_PRIORITY) {
//        interfered_psandboxs = g_hash_table_lookup(interfered_competitors, GINT_TO_POINTER(key));
//        interfered_psandboxs = g_list_remove(interfered_psandboxs, p_sandbox);
//        g_hash_table_insert(interfered_competitors, GINT_TO_POINTER(key), interfered_psandboxs);
//
//        for (iterator = competitors; iterator; iterator = iterator->next) {
//          PSandbox *psandbox = (PSandbox *) iterator->data;
////          printf("find psandbox %d, activity state %d, promoting %d, state %d\n",psandbox->bid,psandbox->activity->activity_state,psandbox->action_level,psandbox->state);
//          if (psandbox == NULL || p_sandbox == psandbox || psandbox->activity->activity_state != QUEUE_WAITING
//              || psandbox->action_level != LOW_PRIORITY || psandbox->state != BOX_PREEMPTED)
//            continue;
//
//          psandbox->state = BOX_ACTIVE;
//          syscall(SYS_WAKEUP_PSANDBOX, psandbox->bid);
//        }
//      }
//      updateDefertime(p_sandbox);
//      pthread_mutex_unlock(&stats_lock);
      break;
    }
    case EXIT:
    {
      syscall(SYS_UPDATE_EVENT,event,p_sandbox->bid);
//      pthread_mutex_lock(&stats_lock);
//      GList *iterator = NULL;
//      p_sandbox->activity->activity_state = QUEUE_EXIT;
//
//      competitors = g_hash_table_lookup(competitors_map, GINT_TO_POINTER(key));
//      interfered_psandboxs = g_hash_table_lookup(interfered_competitors, GINT_TO_POINTER(key));
//
//      holders = g_hash_table_lookup(holders_map, GINT_TO_POINTER(key));
//      holders = g_list_remove(holders, p_sandbox);
//      g_hash_table_insert(holders_map, GINT_TO_POINTER(key), holders);
//      if (p_sandbox->action_level == LOW_PRIORITY && competitors && interfered_psandboxs) {
//        for (iterator = competitors; iterator; iterator = iterator->next) {
//          PSandbox *competitor = (PSandbox *) iterator->data;
//          if (competitor == NULL || p_sandbox == competitor || competitor->activity->activity_state != QUEUE_WAITING
//              || competitor->action_level != LOW_PRIORITY)
//            continue;
//
//          competitor->state = BOX_PREEMPTED;
//          syscall(SYS_PENALIZE_PSANDBOX, competitor->bid, 1000);
//        }
//
//        interfered_psandboxs = g_hash_table_lookup(interfered_competitors, GINT_TO_POINTER(key));
//
//        for (iterator = interfered_psandboxs; iterator; iterator = iterator->next) {
//          PSandbox *victim = (PSandbox *) iterator->data;
//
//          if (victim == NULL || victim->activity->activity_state != QUEUE_WAITING)
//            continue;
//
//          interfered_psandboxs = g_list_remove(interfered_psandboxs, victim);
//          g_hash_table_insert(interfered_competitors, GINT_TO_POINTER(key), interfered_psandboxs);
////        printf("psandbox %d wakeup %d\n",p_sandbox->bid, victim->bid);
//          syscall(SYS_WAKEUP_PSANDBOX, victim->bid);
//        }
//      }

//      if (is_interfered(p_sandbox, event, holders)) {
//        PSandbox* noisy_neighbor;
//        noisy_neighbor = find_noisyNeighbor(p_sandbox, holders);
//
//        if (noisy_neighbor) {
//          //Give penalty to the noisy neighbor
//          printf("1.thread %lu sleep thread %lu\n", p_sandbox->bid, noisy_neighbor->bid);
//          penalize_competitor(noisy_neighbor,p_sandbox,event);
//        }
//      }
//      pthread_mutex_unlock(&stats_lock);
      break;
    }
//    case MUTEX_RELEASE: {
//      time_t penalty_ns;
//
//      pthread_mutex_lock(&stats_lock);
//      competitors = g_hash_table_lookup(competitors_map, GINT_TO_POINTER(key));
//      if (!competitors) {
//        pthread_mutex_unlock(&stats_lock);
//        printf("Error: fail to get competitor list\n");
//        return -1;
//      }
//
//      competitors = g_list_remove(competitors, p_sandbox);
//      g_hash_table_insert(competitors_map, GINT_TO_POINTER(key), competitors);

//      if (p_sandbox->state == BOX_PENDING_PENALTY) {
//          if (p_sandbox->activity->owned_mutex == 0) {
//              // FIXME why -- to key since you basically replace that box w/ this one
//              // TODO still need this tmp value??? same in EXIT
//              penalty_ns = 100000;
//              printf("2.thread %lu sleep thread %lu\n", p_sandbox->victim->bid, p_sandbox->bid);
//              (*(int *)p_sandbox->victim->activity->key)--;
//              p_sandbox->victim->activity->is_preempted = 1;
//              p_sandbox->state = BOX_PREEMPTED;
//              pthread_mutex_unlock(&stats_lock);
//              syscall(SYS_WAKEUP_PSANDBOX, p_sandbox->victim->bid);
//              syscall(SYS_PENALIZE_PSANDBOX, p_sandbox->bid, penalty_ns);
//              break;
//          }
//      } else if (p_sandbox->state == BOX_COMPENSATED || p_sandbox->state == BOX_PREEMPTED) {
//        break;
//      }
//
//      penalty_ns = get_penalty(p_sandbox,competitors);
//      pthread_mutex_unlock(&stats_lock);
//      if (penalty_ns > 100000) {
//        penalty_ns = 100000;
//        syscall(SYS_PENALIZE_PSANDBOX, p_sandbox->bid, penalty_ns);
//      }
//      break;
//    }



  }
  return success;
}


//time_t get_penalty(PSandbox *psandbox, GList *competitors) {
//  GList *iterator = NULL;
//  struct timespec current_time;
//  time_t executing_tm, defer_tm;
//  time_t penalty_ns = -1;
//
//  for (iterator = competitors; iterator; iterator = iterator->next) {
//    PSandbox *competitor = (PSandbox *) iterator->data;
//
//    if (competitor == NULL)
//      continue;
//
//    if (psandbox == competitor || competitor->activity->activity_state == QUEUE_ENTER
//        || competitor->state == BOX_PREEMPTED || competitor->activity->is_preempted == 1
//        || competitor->state == BOX_COMPENSATED || competitor->state == BOX_PENDING_PENALTY)
//      continue;
//
//    clock_gettime(CLOCK_REALTIME, &current_time);
//
//    executing_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
//        competitor->activity->execution_time.tv_sec * NSEC_PER_SEC - competitor->activity->execution_time.tv_nsec;
//    defer_tm = current_time.tv_sec * NSEC_PER_SEC + current_time.tv_nsec -
//        competitor->activity->delaying_start.tv_sec * NSEC_PER_SEC
//        - competitor->activity->delaying_start.tv_nsec;
//
//    if (defer_tm >
//        (executing_tm - defer_tm) * psandbox->max_defer * g_list_length(competitors)) {
//      penalty_ns += defer_tm;
//    }
//  }
//  return penalty_ns;
//}
//
int is_interfered(PSandbox *psandbox, BoxEvent *event, GList *competitors) {
  struct timespec executing_tm, delayed_tm;
  struct timespec current_time;

  if (!psandbox)
    return 0;

  clock_gettime(CLOCK_REALTIME, &current_time);

  executing_tm = timeDiff(psandbox->activity->execution_start,current_time);
  delayed_tm = timeDiff(psandbox->activity->delaying_start,current_time);

  if (time2ns(delayed_tm) > time2ns(timeDiff(delayed_tm, executing_tm)) *
      psandbox->max_defer * g_list_length(competitors)) {
    return 1;
  }
  return 0;
}

PSandbox *find_noisyNeighbor(PSandbox *p_sandbox, GList *competitors) {
  GList *iterator = NULL;

  for (iterator = competitors; iterator; iterator = iterator->next) {
    PSandbox *competitor = iterator->data;

    // Don't wakeup itself
    if (p_sandbox == competitor || competitor->activity->activity_state != QUEUE_ENTER)
      continue;

    return competitor;
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
    time_t penalty_us = 1000;
    (*(int *) event->key)--;
    noisy_neighbor->activity->activity_state = QUEUE_PREEMPTED;
    victim->noisy_neighbor = noisy_neighbor;
    victim->activity->activity_state = QUEUE_PROMOTED;
    printf("call compensate\n");
    syscall(SYS_COMPENSATE_PSANDBOX, noisy_neighbor->bid, victim->bid ,penalty_us);
  }
  return 0;
}

void active_psandbox(PSandbox *p_sandbox) {
  if (!p_sandbox || !p_sandbox->activity) {
    printf("the active psandbox %lu is empty, the activity %p is empty\n", p_sandbox->bid, p_sandbox->activity);
    return;
  }
//  p_sandbox->total_activity++;
  syscall(SYS_ACTIVE_PSANDBOX);
//  p_sandbox->state = BOX_ACTIVE;
//  clock_gettime(CLOCK_REALTIME, &p_sandbox->activity->execution_start);
//  p_sandbox->activity->competitors = g_hash_table_size(psandbox_map) - 1;
}

void freeze_psandbox(PSandbox *p_sandbox) {
  struct timespec current_tm;
  if (!p_sandbox || !p_sandbox->activity) {
    printf("the active psandbox %lu is empty, the activity %p is empty\n", p_sandbox->bid, p_sandbox->activity);
    return;
  }

//  if (p_sandbox->compensation_ticket > 1) {
//    p_sandbox->compensation_ticket--;
//  } else {
//    p_sandbox->action_level = LOW_PRIORITY;
//  }

//  p_sandbox->finished_activities++;
//  p_sandbox->state = BOX_FREEZE;
//  clock_gettime(CLOCK_REALTIME, &current_tm);
//  pthread_mutex_lock(&stats_lock);
//  p_sandbox->activity->execution_time =
//      timeDiff(p_sandbox->activity->defer_time, timeDiff(p_sandbox->activity->execution_start, current_tm));
//  pthread_mutex_unlock(&stats_lock);
  syscall(SYS_FREEZE_PSANDBOX);
//  if(p_sandbox->activity->competitors == 0 )
//    return;
//
//  if (time2ns(p_sandbox->activity->execution_time) * p_sandbox->max_defer * p_sandbox->activity->competitors
//      < time2ns(p_sandbox->activity->defer_time)) {
//    p_sandbox->bad_activities++;
//    if (p_sandbox->action_level == LOW_PRIORITY)
//      p_sandbox->action_level = MID_PRIORITY;
//    if (p_sandbox->action_level != HIGHEST_PRIORITY && p_sandbox->finished_activities > PROBING_NUMBER && p_sandbox->bad_activities
//        > (p_sandbox->tail_threshold * p_sandbox->finished_activities)) {
//      p_sandbox->action_level = HIGHEST_PRIORITY;
//      printf("give a ticket for %lu, bad activity %d, finish activity %d\n", p_sandbox->bid, p_sandbox->bad_activities, p_sandbox->finished_activities);
//      p_sandbox->compensation_ticket = COMPENSATION_TICKET_NUMBER;
//    }
//  }

//  memset(p_sandbox->activity, 0, sizeof(Activity));
}

//int track_mutex(BoxEvent *event, PSandbox *p_sandbox) {
//  int event_type = event->event_type;
//
//  if (!event->key)
//    return -1;
//  if (!p_sandbox)
//    return 0;
//  if (p_sandbox->state == BOX_FREEZE)
//    return 0;
//
//  switch (event_type) {
//    case PREPARE:
//      if (event->key_size == 1) {
//        p_sandbox->activity->owned_mutex++;
//      } else {
//        p_sandbox->activity->queue_event++;
//      }
//      break;
//    case EXIT:
//      if (event->key_size == 1) {
//        p_sandbox->activity->owned_mutex--;
//      } else {
//        p_sandbox->activity->queue_event--;
//      }
//      break;
//    case MUTEX_GET:p_sandbox->activity->owned_mutex++;
//      break;
//    case MUTEX_RELEASE:p_sandbox->activity->owned_mutex--;
//      break;
//    default:break;
//  }
//  return 1;
//}

//int track_time(BoxEvent *event, PSandbox *p_sandbox) {
//  int event_type = event->event_type;
//
//  if (!p_sandbox)
//    return 0;
//  if (p_sandbox->state == BOX_FREEZE)
//    return 0;
//
//  switch (event_type) {
//    case PREPARE: {
//      pthread_mutex_lock(&stats_lock);
//      clock_gettime(CLOCK_REALTIME, &p_sandbox->activity->delaying_start);
//      pthread_mutex_unlock(&stats_lock);
//      break;
//    }
//    case ENTER: {
//      updateDefertime(p_sandbox);
//      break;
//    }
//    default:break;
//  }
//  return 1;
//}

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
