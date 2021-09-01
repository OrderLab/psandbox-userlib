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

#define HIGHEST_PRIORITY 2
#define MID_PRIORITY 1
#define LOW_PRIORITY 0

#define DBUG_TRACE(A) clock_gettime(CLOCK_REALTIME, A)

enum enum_event_type { PREPARE, ENTER, HOLD, UNHOLD };

// Add comment for each enum type to describe the semantic and the constrains of
// the action for that state
enum enum_psandbox_state {
  BOX_ACTIVE,       // psandbox starts to handle an activity
  BOX_FREEZE,       // psandbox finishs an activity
  BOX_START,        // create a psandbox
  BOX_PREEMPTED,    // psandbox is a noisy neighbor and being preempted by the
                    // victim
  BOX_COMPENSATED,  // psandbox is victim and is penalizing others
  BOX_PENDING_PENALTY  // the penalty is pending
};

enum enum_activity_state {
  QUEUE_WAITING,
  QUEUE_ENTER,
  QUEUE_EXIT,
  QUEUE_PREEMPTED,
  QUEUE_PROMOTED
};

typedef struct sandboxEvent {
  enum enum_event_type event_type;
  unsigned int key;
} BoxEvent;

typedef struct activity {
  enum enum_activity_state activity_state;
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

typedef struct pSandbox {
  long bid;  // sandbox id used by syscalls
  enum enum_psandbox_state state;
  Activity *activity;
  double tail_threshold;  // the maximum ratio of activity that are allowed to
                          // break the threshold
  double max_defer;       // the interference that allowed for each psandbox
  int finished_activities;
  int bad_activities;
  int action_level;  // the psandbox is interferenced and needs future concern
  int compensation_ticket;
  struct pSandbox *noisy_neighbor;
  struct pSandbox *victim;

  // debugging
  int count;
  clock_t total_time;
  int s_count;
  struct timespec every_second_start;
} PSandbox;

/// @brief Create a performance sandbox
/// @return The point to the performance sandbox.
PSandbox *create_psandbox();

/// @brief release a performance sandbox
/// @param p_sandbox The performance sandbox to release.
/// @return On success 1 is return
int release_psandbox(PSandbox *p_sandbox);

int add_rules(int total_types, double *defer_rule);

/// @brief Update an event to the performance p_sandbox
/// @param event The event to notify the performance p_sandbox.
/// @param p_sandbox The p_sandbox to notify
/// @return On success 1 is returned.
int update_psandbox(unsigned int key, enum enum_event_type event_type);

void active_psandbox(PSandbox *p_sandbox);
void freeze_psandbox(PSandbox *p_sandbox);
PSandbox *get_current_psandbox();

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
int unbind_psandbox(size_t addr, PSandbox *p_sandbox);
PSandbox *bind_psandbox(size_t addr);



int psandbox_manager_init();

int track_mutex(struct sandboxEvent *event, PSandbox *p_sandbox);

int track_time(struct sandboxEvent *event, PSandbox *p_sandbox);
void print_all();

#ifdef __cplusplus
}
#endif

#endif  // PSANDBOX_USERLIB_PSANDBOX_H
