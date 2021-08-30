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
#define SYS_UNBIND_PSANDBOX 446
#define SYS_BIND_PSANDBOX 447

#define NSEC_PER_USEC    1000L
#define NSEC_PER_SEC 1000000000L
#define USEC_PER_SEC 1000000L

GHashTable *psandbox_map;
//GHashTable condition_map;
GHashTable *psandbox_transfer_map;
// GHashTable *psandbox_transfer_event_map;
double **rules;

/* lock for updating the stats variables */
pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

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

static inline void 


Defertime(PSandbox *p_sandbox) {
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
  p_sandbox->count = 0;
  p_sandbox->s_count = 0;
  p_sandbox->total_time = 0;
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
  pthread_mutex_lock(&stats_lock);
  g_hash_table_remove(psandbox_map, key);
  pthread_mutex_unlock(&stats_lock);

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

void unbind_psandbox(size_t addr, PSandbox *p_sandbox) {

  printf("unbind psandbox syscall %d Start\n", p_sandbox->bid);
  syscall(SYS_UNBIND_PSANDBOX, addr); 
  printf("unbind psandbox syscall %d End\n", p_sandbox->bid);
  p_sandbox->bid = -1;
  
  gint64 *key = g_new(gint64, 1);
  (*key) = addr;

  if (psandbox_transfer_map == NULL) {
    psandbox_transfer_map = g_hash_table_new(g_int64_hash, g_int64_equal);
  }

  pthread_mutex_lock(&psandbox_transfer_lock);
  g_hash_table_insert(psandbox_transfer_map, key, p_sandbox);
  pthread_mutex_unlock(&psandbox_transfer_lock);

}

// void mount_psandbox_event(size_t event_key, PSandbox *p_sandbox) {
//   GList *p_sandboxes;

//   gint64 *key = g_new(gint64, 1);
//   (*key) = event_key;

//   if (psandbox_transfer_event_map == NULL) {
//     psandbox_transfer_event_map = g_hash_table_new(g_int64_hash, g_int64_equal);
//   }

//   pthread_mutex_lock(&psandbox_transfer_event_lock);
//   p_sandboxes = g_hash_table_lookup(psandbox_transfer_event_map, key);

//   if (p_sandboxes == NULL) {
//     p_sandboxes = g_list_append(p_sandboxes, p_sandbox);
//     g_hash_table_insert(psandbox_transfer_event_map, key, p_sandboxes);
//   } else {
//     g_list_append(p_sandboxes, p_sandbox);
//   }
//   pthread_mutex_unlock(&psandbox_transfer_event_lock);
// }

PSandbox *bind_psandbox(size_t addr) {
  PSandbox *p_sandbox = NULL;
  // GList *p_sandboxes, *tl;

  printf("bind psandbox dd syscall Start\n");
  int sandbox_id = (int) syscall(SYS_BIND_PSANDBOX, addr);
  
  gint64 *key = g_new(gint64, 1);
  (*key) = addr;

  pthread_mutex_lock(&psandbox_transfer_lock);
  p_sandbox = g_hash_table_lookup(psandbox_transfer_map, key);
  if (p_sandbox) {
    g_hash_table_remove(psandbox_transfer_map, key);
  }
  pthread_mutex_unlock(&psandbox_transfer_lock);

  // TODO check for psandbox fail
  p_sandbox->bid = sandbox_id;

  printf("bind psandbox %d syscall End\n", p_sandbox->bid);

  // if (p_sandbox == NULL) {
  //   (*key) = event_key;
  //   pthread_mutex_lock(&psandbox_transfer_event_lock);
  //   p_sandboxes = g_hash_table_lookup(psandbox_transfer_event_map, key);
  //   if (p_sandboxes) {
  //     tl = g_list_last(p_sandboxes);
  //     p_sandbox = (PSandbox *) tl->data;
  //     p_sandboxes = g_list_remove(p_sandboxes, p_sandbox);
  //     if (p_sandboxes == NULL) {
  //       g_hash_table_remove(psandbox_transfer_event_map, key);
  //     }
  //   }
  //   pthread_mutex_unlock(&psandbox_transfer_event_lock);
  // } 

  return p_sandbox;
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


int update_psandbox(unsigned int key, enum enum_event_type event_type) {
  int success = 0;
  BoxEvent event;
#ifdef TRACE_DEBUG
  struct timespec  start, stop;
#endif
  PSandbox* psandbox = get_psandbox();
  if (!key || !psandbox || !psandbox->activity) {
    return -1;
  }


#ifdef TRACE_DEBUG
  DBUG_TRACE(&p_sandbox->every_second_start);
  DBUG_TRACE(&start);
#endif
  event.key = key;
  event.event_type = event_type;
  syscall(SYS_UPDATE_EVENT,event,psandbox->bid);
#ifdef TRACE_DEBUG
  DBUG_TRACE(&stop);
  long time = time2ns(timeDiff(start,stop));
  long s_time = time2ns(timeDiff(p_sandbox->every_second_start,stop));
  p_sandbox->total_time = p_sandbox->total_time + time;
  p_sandbox->count++;
  p_sandbox->s_count++;
  if((double )(s_time)/1000000000 > 3) {
    printf("tps %d, psandbox %d\n",p_sandbox->s_count, p_sandbox->bid);
    p_sandbox->s_count = 0;
    clock_gettime(CLOCK_REALTIME,&p_sandbox->every_second_start);
  }
#endif
  return success;
}

void active_psandbox(PSandbox *p_sandbox) {
  if (!p_sandbox || !p_sandbox->activity) {
//    printf("the active psandbox %lu is empty, the activity %p is empty\n", p_sandbox->bid, p_sandbox->activity);
    return;
  }

  syscall(SYS_ACTIVE_PSANDBOX);
}

void freeze_psandbox(PSandbox *p_sandbox) {
  if (!p_sandbox || !p_sandbox->activity) {
//    printf("the active psandbox %lu is empty, the activity %p is empty\n", p_sandbox->bid, p_sandbox->activity);
    return;
  }

  syscall(SYS_FREEZE_PSANDBOX);
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
