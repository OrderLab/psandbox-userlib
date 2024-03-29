//
// The Psandbox project
//
// Created by yigonghu on 2/18/21.
//
// Copyright (c) 2021, Johns Hopkins University - Order Lab
//
//      All rights reserved.
//      Licensed under the Apache License, Version 2.0 (the "License");

#ifndef PSANDBOX_USERLIB_PSANDBOX_H
#define PSANDBOX_USERLIB_PSANDBOX_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>


#ifdef __cplusplus
extern "C" {
#endif

#define HOLDER_SIZE 50
#define DBUG_TRACE(A) clock_gettime(CLOCK_REALTIME, A)
#define NSEC_PER_SEC 1000000000L
#define MAX_TIME 500

#define HIGHEST_PRIORITY 2
#define MID_PRIORITY 1
#define LOW_PRIORITY 0

enum enum_event_type { PREPARE, ENTER, HOLD, UNHOLD, UNHOLD_IN_QUEUE_PENALTY, COND_WAKE };
enum enum_isolation_type { ABSOLUTE, RELATIVE, SCALABLE, ISOLATION_DEFAULT};
enum enum_unbind_flag {
    UNBIND_LAZY           = 0x1,
    UNBIND_ACT_UNFINISHED = 0x2,
    UNBIND_HANDLE_ACCEPT  = 0x4,
    UNBIND_NONE           = 0x0,
};

typedef struct sandboxEvent {
  enum enum_event_type event_type;
  unsigned int key;
} BoxEvent;


typedef struct pSandbox PSandbox;


typedef struct pSandbox {
  long bid;  // sandbox id used by syscalls
  long pid; // the thread that the perfSandbox is bound

  size_t holders[HOLDER_SIZE];
  int hold_resource;

  //Debugger for tracing syscall number
  long step;
  long start_time;
  long result[MAX_TIME];
  long count;
  long activity;
  long sample_count;
  int is_sample;
}PSandbox;

typedef struct isolationRule {
  enum enum_isolation_type type;
  int isolation_level;
  int priority;
  int is_retro;
}IsolationRule;

/// @brief Create a performance sandbox
/// @return The point to the performance sandbox.
int create_psandbox(IsolationRule rule);

/// @brief release a performance sandbox
/// @param p_sandbox The performance sandbox to release.
/// @return On success 1 is return
int release_psandbox(int pid);

/// @brief Update an event to the performance p_sandbox
/// @param event The event to notify the performance p_sandbox.
/// @param p_sandbox The p_sandbox to notify
/// @return On success 1 is returned.
long int do_update_psandbox(size_t key, enum enum_event_type event_type, int is_lazy, int is_pass);

/// @brief Update an event to the performance p_sandbox
/// @param event The event to notify the performance p_sandbox.
/// @param p_sandbox The p_sandbox to notify
/// @return On success 1 is returned.
long inline int update_psandbox(size_t key, enum enum_event_type event_type) {
  return do_update_psandbox(key,event_type,false,false);
}

void activate_psandbox(int pid);
void freeze_psandbox(int pid);
int get_current_psandbox();
int get_psandbox(size_t key);
int find_holder(size_t key);
void penalize_psandbox(long int penalty,size_t key);

/// The functions are to transfer psandbox ownership between threads
int unbind_psandbox(size_t key, int pid, enum enum_unbind_flag flags);
int bind_psandbox(size_t key);

// Add sampling logic
int record_psandbox();
int get_sample_rate();
int get_psandbox_record();
int sample_psandbox();
int is_sample(int is_end);
int psandbox_manager_init();

void print_all();


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

static inline long time2ns(Time t1) {
  return t1.tv_sec * 1000000000L + t1.tv_nsec;
}

static inline long time2ms(Time t1) {
  return (t1.tv_sec * 1000000000L + t1.tv_nsec)/1000000;
}

#ifdef __cplusplus
}
#endif

#endif  // PSANDBOX_USERLIB_PSANDBOX_H
