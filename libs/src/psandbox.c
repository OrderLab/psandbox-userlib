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

//#define DISABLE_PSANDBOX
//#define IS_RETRO
//#define TRACE_NUMBER
#define NO_LIB
struct hashmap_s  *psandbox_map = NULL;

/* lock for updating the stats variables */
pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;
//pthread_mutex_t transfer_lock = PTHREAD_MUTEX_INITIALIZER;


#define TRACK_SYSCALL() do {\
  PSandbox* p_sandbox = (PSandbox *) hashmap_get(psandbox_map, psandbox_id, 0); \
  if(!p_sandbox) {         \
    printf("error the psandbox is none\n");     \
    break;                  \
  }                          \
  p_sandbox->count++;\
  long i = p_sandbox->count % p_sandbox->step;\
  if (i == 0) {\
  if (p_sandbox->result[p_sandbox->count / p_sandbox->step] != 0)\
      printf("error the result is there in %lu\n",i);\
    struct timespec current;\
    DBUG_TRACE(&current);\
    p_sandbox->result[p_sandbox->count / p_sandbox->step] = time2ms(current) - p_sandbox->start_time; \
  }\
} while(0)\

int psandbox_manager_init() {
  return syscall(SYS_START_MANAGER,&stats_lock);
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
    rule.is_retro = false;
  }
#ifdef IS_RETRO
  rule.is_retro = true;
  bid = syscall(SYS_CREATE_PSANDBOX,rule.type,rule.isolation_level,rule.priority);
#elif defined(NO_LIB)
  bid = syscall(SYS_CREATE_PSANDBOX,rule.type,rule.isolation_level,rule.priority);
  return bid;
#else
  bid = syscall(SYS_CREATE_PSANDBOX,rule.type,rule.isolation_level,rule.priority);
#endif

//  bid = syscall(SYS_gettid);
  if (bid == -1) {
    printf("syscall failed with errno: %s\n", strerror(errno));
    return -1;
  }

  if (psandbox_map == NULL) {
    psandbox_map = (struct hashmap_s *)malloc(sizeof(struct hashmap_s));
    hashmap_create(32, psandbox_map);
  }

  psandbox_id = bid;
  p_sandbox = (struct pSandbox *) calloc(sizeof(struct pSandbox),1);
  p_sandbox->pid = bid;

  pthread_mutex_lock(&stats_lock);
  hashmap_put(psandbox_map, bid, p_sandbox,0);
//  printf("create psandbox %d\n",psandbox_id);
  pthread_mutex_unlock(&stats_lock);
#ifdef TRACE_NUMBER
  p_sandbox->step = 10000;
  struct timespec start;
  DBUG_TRACE(&start);
  p_sandbox->start_time  = time2ms(start);
  p_sandbox->count++;
  p_sandbox->activity = 0;
#endif
  return bid;
}

int release_psandbox(int pid) {
  int success = 0;

  #ifdef DISABLE_PSANDBOX
  return -1;
  #endif

  if (pid == -1)
    return success;

#ifdef NO_LIB
  success = (int) syscall(SYS_RELEASE_PSANDBOX, pid);
  return success;
#else
  success = (int) syscall(SYS_RELEASE_PSANDBOX, pid);
#endif


  if (success == -1) {
    printf("failed to release sandbox in the kernel: %s\n", strerror(errno));
    return success;
  }
   #ifdef TRACE_NUMBER
  print_all();
  #endif
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
#ifdef TRACE_NUMBER
 TRACK_SYSCALL();
#endif
  if (pid == -1) {
//    printf("Error: Can't get sandbox for the thread %d\n",syscall(SYS_gettid));
    return -1;
  }

  return pid;
}

int get_psandbox(size_t key) {
  #ifdef DISABLE_PSANDBOX
  return -1;
  #endif
  int pid = (int) syscall(SYS_GET_PSANDBOX, key);
#ifdef TRACE_NUMBER
  TRACK_SYSCALL();
#endif
  if (pid == -1) {
    printf("Error: Can't get sandbox for the id %d\n", pid);
    return -1;
  }
  return pid;
}


int unbind_psandbox(size_t key, int pid, enum enum_unbind_flag flags) {
#ifdef DISABLE_PSANDBOX
  return -1;
#endif
  if (pid == -1) {
    printf("Error: Can't unbind sandbox for the thread %ld\n",syscall(SYS_gettid));
    return -1;
  }

#ifdef TRACE_NUMBER
  TRACK_SYSCALL();
#endif
  if(syscall(SYS_UNBIND_PSANDBOX, key, flags)) {
    psandbox_id = 0;
    return 0;
  }

  printf("error: unbind fail for psandbox %d\n", pid);
  return -1;
}

int bind_psandbox(size_t key) {
#ifdef DISABLE_PSANDBOX
  return -1;
#endif
#ifdef TRACE_NUMBER
  TRACK_SYSCALL();
#endif
  int bid = (int) syscall(SYS_BIND_PSANDBOX, key);

  if (bid == -1) {
    printf("Error: Can't bind address %ld for the thread %ld\n", key, syscall(SYS_gettid));
    return -1;
  }
  psandbox_id = bid;
  return bid;
}

int find_holder(size_t key) {
  PSandbox *psandbox;
  int i;
  psandbox = (PSandbox *) hashmap_get(psandbox_map, psandbox_id, 0);
#ifdef TRACE_NUMBER
  TRACK_SYSCALL();
#endif
  for (i = 0; i < HOLDER_SIZE ; ++i) {
    if (psandbox->holders[i] == key) {
      return i;
    }
  }
  return -1;
}


long int do_update_psandbox(size_t key, enum enum_event_type event_type, int is_lazy, int is_pass) {
  long int success = 0;
  BoxEvent event;

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
  if(psandbox_id == 0) {
    return -1;
  }
#ifdef TRACE_NUMBER
  TRACK_SYSCALL();
#endif

  switch (event_type) {
    case HOLD: {
      int i;
      psandbox = (PSandbox *) hashmap_get(psandbox_map, psandbox_id, 0);
      for (i = 0; i < HOLDER_SIZE ; ++i) {
        if (psandbox->holders[i] == 0) {
          psandbox->holders[i] = key;
//          psandbox->hold_resource++;
          break;
        } else if (psandbox->holders[i] == key){
          break;
        }
      }

      if (i == HOLDER_SIZE){
        printf("can't create holder with malloc by psandbox %ld\n",psandbox->pid);
      }

      break;
    }
    case UNHOLD:
    case UNHOLD_IN_QUEUE_PENALTY: {
      int i;
      if(is_lazy) {
        success = syscall(SYS_UPDATE_EVENT,&event,is_lazy);
        break;
      }
      psandbox = (PSandbox *) hashmap_get(psandbox_map, psandbox_id, 0);
      for (i = 0; i < HOLDER_SIZE ; ++i) {
        if (psandbox->holders[i] == key) {
          psandbox->holders[i] = 0;
//          psandbox->hold_resource--;
//          if (psandbox->hold_resource == 0)
          if (!is_pass)
              success = syscall(SYS_UPDATE_EVENT,&event,is_lazy);
          break;
        }

      }
      if (is_pass)
        success = syscall(SYS_UPDATE_EVENT,&event,is_lazy);
      break;
    }
    default:
      success = syscall(SYS_UPDATE_EVENT,&event,is_lazy);
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

void activate_psandbox(int pid) {

  if (pid == -1) {
//    printf("the active psandbox %lu is empty, the activity %p is empty\n", p_sandbox->pid, p_sandbox->activity);
    return;
  }
#ifdef DISABLE_PSANDBOX
  return ;
#endif
#ifdef TRACE_NUMBER
  TRACK_SYSCALL();
  PSandbox* p_sandbox = (PSandbox *) hashmap_get(psandbox_map, psandbox_id, 0);
  p_sandbox->activity++;
#endif
  syscall(SYS_ACTIVATE_PSANDBOX);
}

void freeze_psandbox(int pid) {
#ifdef TRACE_NUMBER
  TRACK_SYSCALL();
#endif
  if (pid == -1) {
//    printf("the active psandbox %lu is empty, the activity %p is empty\n", p_sandbox->pid, p_sandbox->activity);
    return;
  }
#ifdef DISABLE_PSANDBOX
  return ;
#endif
#ifdef TRACE_NUMBER
  TRACK_SYSCALL();
#endif
  syscall(SYS_FREEZE_PSANDBOX);
}

void print_all(){
  long i;
  PSandbox *psandbox = (PSandbox *) hashmap_get(psandbox_map, psandbox_id, 0);
  printf("Latency histogram (values are in nanoseconds) for pid %d\n",psandbox_id);
  printf("value -- count\n");
  printf("average number %lu\n",psandbox->count/psandbox->activity);
  for (i = 0; i < (psandbox->count / psandbox->step); i++) {
    printf("syscall: %lu | %u ms\n",(i)*psandbox->step,psandbox->result[i]);
  }
}
