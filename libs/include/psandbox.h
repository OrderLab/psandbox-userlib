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
#include "hashmap.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HOLDER_SIZE 1000
#define DBUG_TRACE(A) clock_gettime(CLOCK_REALTIME, A)

#define HIGHEST_PRIORITY 2
#define MID_PRIORITY 1
#define LOW_PRIORITY 0

enum enum_event_type { PREPARE, ENTER, HOLD, UNHOLD, COND_WAKE };
enum enum_isolation_type { ABSOLUTE, RELATIVE, SCALABLE, ISOLATION_DEFAULT};

typedef struct sandboxEvent {
  enum enum_event_type event_type;
  unsigned int key;
} BoxEvent;

typedef struct activity {
  struct timespec defer_time;
  struct timespec delaying_start;
  struct timespec execution_time;
  struct timespec execution_start;
  uint competitors;
  int owned_mutex;  // TODO: use a slab to store each element and get one from
                    // the pool when you need to use it.
  int is_preempted;
  int queue_event;
  unsigned int key;
} Activity;

typedef struct pSandbox PSandbox;


typedef struct pSandbox {
  long bid;  // sandbox id used by syscalls
  long pid; // the thread that the perfSandbox is bound
  Activity *activity;

  unsigned int holders[HOLDER_SIZE];
};

typedef struct isolationRule {
  enum enum_isolation_type type;
  int isolation_level;
  int priority;
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
long int do_update_psandbox(unsigned int key, enum enum_event_type event_type, int is_lazy);

/// @brief Update an event to the performance p_sandbox
/// @param event The event to notify the performance p_sandbox.
/// @param p_sandbox The p_sandbox to notify
/// @return On success 1 is returned.
inline long int update_psandbox(unsigned int key, enum enum_event_type event_type) {
  return do_update_psandbox(key,event_type,false);
}

void activate_psandbox(int bid);
void freeze_psandbox(int bid);
int get_current_psandbox();
int get_psandbox(size_t addr);
int find_holder(unsigned int key);
void penalize_psandbox(long int penalty,unsigned int key);

/// The functions are to transfer psandbox ownership between threads
/// Case A, thread A -> B, A knows B's thread id
///   In A, call mount_psandbox (A_p_sandbox, B's thread id)
///     -> this stop tracing the current task and 
///        mount psandbox in to a global struct for B to recieve
///   In B, call unmount_psandbox()
///     -> this lookup the global struct to recieve its 
/// Case B, thread A -> B, A doesn't know B's thread id
///   solution, another global struct, B's get pbox by the same event key?
///   unmount, iterate first global struct, if no, check the second for the 
///   same key
int unbind_psandbox(size_t addr, int sandbox_id);
int bind_psandbox(size_t addr);

int psandbox_manager_init();

void print_all();

#ifdef __cplusplus
}
#endif

#endif  // PSANDBOX_USERLIB_PSANDBOX_H
