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
#include "hashmap.h"

#define SYS_CREATE_PSANDBOX    436
#define SYS_RELEASE_PSANDBOX 437
#define SYS_GET_CURRENT_PSANDBOX 438
#define SYS_GET_PSANDBOX 439
#define SYS_START_MANAGER 442
#define SYS_ACTIVATE_PSANDBOX 443
#define SYS_FREEZE_PSANDBOX 444
#define SYS_UPDATE_EVENT 445
#define SYS_UNBIND_PSANDBOX 446
#define SYS_BIND_PSANDBOX 447
#define SYS_PENALIZE_EVENT 448

static __thread int psandbox_id;

#define NSEC_PER_SEC 1000000000L

//#define DISABLE_PSANDBOX
struct hashmap_s  *psandbox_map = NULL;

/* lock for updating the stats variables */
pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;
//pthread_mutex_t transfer_lock = PTHREAD_MUTEX_INITIALIZER;
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

static inline void Defertime(PSandbox *p_sandbox) {
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

int create_psandbox(IsolationRule rule) {
#ifdef DISABLE_PSANDBOX
  return -1;
#endif
  long bid;
  PSandbox *p_sandbox;

  if(rule.type == ISOLATION_DEFAULT) {
    rule.type = SCALABLE;
    rule.isolation_level = 100;
    rule.priority = LOW_PRIORITY;
  }

  bid = syscall(SYS_CREATE_PSANDBOX,rule.type,rule.isolation_level,rule.priority);
//  bid = syscall(SYS_gettid);
  if (bid == -1) {
    printf("syscall failed with errno: %s\n", strerror(errno));
    return NULL;
  }

  if (psandbox_map == NULL) {
    psandbox_map = (struct hashmap_s *)malloc(sizeof(struct hashmap_s));
    hashmap_create(32, psandbox_map);
  }

  psandbox_id = bid;
  p_sandbox = (struct pSandbox *) malloc(sizeof(struct pSandbox));
  p_sandbox->pid = bid;

  pthread_mutex_lock(&stats_lock);
  hashmap_put(psandbox_map, bid, p_sandbox,0);
//  printf("create psandbox %d\n",psandbox_id);
  pthread_mutex_unlock(&stats_lock);
  return bid;
}

int release_psandbox(int pid) {
  int success = 0;
  gint *key = g_new(gint, 1);
  #ifdef DISABLE_PSANDBOX
  return -1;
  #endif

  if (pid == -1)
    return success;

  success = (int) syscall(SYS_RELEASE_PSANDBOX, pid);

  if (success == -1) {
    printf("failed to release sandbox in the kernel: %s\n", strerror(errno));
    return success;
  }
  hashmap_remove(psandbox_map, pid);
  psandbox_id = 0;
  return success;
}

int get_current_psandbox() {
  #ifdef DISABLE_PSANDBOX
  return -1;
  #endif
  int pid = (int) syscall(SYS_GET_CURRENT_PSANDBOX);

//  int bid = syscall(SYS_gettid);

  if (pid == -1) {
//    printf("Error: Can't get sandbox for the thread %d\n",syscall(SYS_gettid));
    return -1;
  }

  return pid;
}

int get_psandbox(size_t addr) {
  #ifdef DISABLE_PSANDBOX
  return -1;
  #endif
  int pid = (int) syscall(SYS_GET_PSANDBOX, addr);

  if (pid == -1) {
    printf("Error: Can't get sandbox for the id %d\n", pid);
    return -1;
  }

  return pid;
}

int unbind_psandbox(size_t addr, int bid) {
#ifdef DISABLE_PSANDBOX
  return -1;
#endif
  if (bid == -1) {
    printf("Error: Can't unbind sandbox for the thread %d\n",syscall(SYS_gettid));
    return -1;
  }

  if(syscall(SYS_UNBIND_PSANDBOX, addr)) {
    psandbox_id = 0;
    return 0;
  }
  printf("error: unbind fail for psandbox %d\n", bid);
  return -1;
}

int bind_psandbox(size_t addr) {
  #ifdef DISABLE_PSANDBOX
  return -1;
#endif

  int bid = (int) syscall(SYS_BIND_PSANDBOX, addr);

  if (bid == -1) {
    printf("Error: Can't bind address %d for the thread %d\n", addr,syscall(SYS_gettid));
    return -1;
  }
  psandbox_id = bid;
  return bid;
}

int find_holder(unsigned int key) {
  PSandbox *psandbox;
  int i;
  psandbox = (PSandbox *) hashmap_get(psandbox_map, psandbox_id, 0);
  for (i = 0; i < HOLDER_SIZE ; ++i) {
    if (psandbox->holders[i] == key) {
      return i;
    }
  }
  return -1;
}


long int do_update_psandbox(unsigned int key, enum enum_event_type event_type, int is_lazy) {
  long int success = 0;
  BoxEvent event;
  GList* holders = NULL;
  PSandbox *psandbox;
#ifdef DISABLE_PSANDBOX
  return 1;
#endif
  #ifdef TRACE_DEBUG
  struct timespec  start, stop;
  static struct timespec every_second_start;
  static long total_time = 0;
  static int count = 0;
  DBUG_TRACE(&start);
#endif
  event.key = key;
  event.event_type = event_type;
  if(psandbox_id == 0)
    return -1;
  switch (event_type) {
    case HOLD: {
      int i;
      psandbox = (PSandbox *) hashmap_get(psandbox_map, psandbox_id, 0);
      for (i = 0; i < HOLDER_SIZE ; ++i) {
        if (psandbox->holders[i] == 0) {
          psandbox->holders[i] = key;
          psandbox->hold_resource++;
          break;
        }
      }

      if (i == HOLDER_SIZE){
        printf("can't create holder with malloc by psandbox %ld\n",psandbox->pid);
      }

      break;
    }
    case UNHOLD: {
      int i;

      psandbox = (PSandbox *) hashmap_get(psandbox_map, psandbox_id, 0);
      for (i = 0; i < HOLDER_SIZE ; ++i) {
        if (psandbox->holders[i] == key) {
          psandbox->holders[i] = 0;
          psandbox->hold_resource--;
          if (psandbox->hold_resource == 0)
            success = syscall(SYS_UPDATE_EVENT,&event,is_lazy);
        }
      }
      break;
    }
    case COND_WAKE: {
      event.event_type = UNHOLD;
      success = syscall(SYS_UPDATE_EVENT,&event,is_lazy);
    }
    default:
      syscall(SYS_UPDATE_EVENT,&event);
      break;
  }

#ifdef TRACE_DEBUG
  DBUG_TRACE(&stop);
  long time = time2ns(timeDiff(start,stop));
  total_time += time;
  count++;
  if(count % 1000000 == 0) {
    printf("update call 1000000 times average time %luns\n", total_time/count);
  }
#endif


  return success;
}

void penalize_psandbox(long int penalty, unsigned int key) {
  if(penalty > 1000) {
    syscall(SYS_PENALIZE_EVENT,penalty,key);
  }
}

void activate_psandbox(int bid) {
  if (bid == -1) {
//    printf("the active psandbox %lu is empty, the activity %p is empty\n", p_sandbox->bid, p_sandbox->activity);
    return;
  }
#ifdef DISABLE_PSANDBOX
  return ;
#endif
  syscall(SYS_ACTIVATE_PSANDBOX);
}

void freeze_psandbox(int bid) {
  if (bid == -1) {
//    printf("the active psandbox %lu is empty, the activity %p is empty\n", p_sandbox->bid, p_sandbox->activity);
    return;
  }
#ifdef DISABLE_PSANDBOX
  return ;
#endif
  syscall(SYS_FREEZE_PSANDBOX);
}

//int get_bid(){
//  return bid;
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
