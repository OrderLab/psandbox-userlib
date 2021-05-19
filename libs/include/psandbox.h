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

#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdbool.h>
#include <pthread.h>
#include "hashmap.h"

#ifdef __cplusplus
extern "C" {
#endif


#define NSEC_PER_SEC	1000000000L

enum enum_event_type {
  PREPARE_QUEUE,
  RETRY_QUEUE,
  ENTER_QUEUE,
  EXIT_QUEUE,
  SLEEP_BEGIN,
  SLEEP_END,
  UPDATE_QUEUE_CONDITION,
  MUTEX_REQUIRE,
  MUTEX_GET,
  MUTEX_RELEASE
};
//Add comment for each enum type to describe the semantic and the constrains of the action for that state
enum enum_psandbox_state {
  BOX_ACTIVE, //
  BOX_FREEZE, //
  BOX_START, //
  BOX_PENALIZED,//
  BOX_INTERFERED, //
  BOX_PENDING_PENALTY //
};

enum enum_condition {
  COND_LARGE, COND_SMALL, COND_LARGE_OR_EQUAL, COND_SMALL_OR_EQUAL
};

enum enum_queue_state {
  QUEUE_NULL,QUEUE_ENTER,QUEUE_SLEEP,QUEUE_AWAKE,QUEUE_EXIT,SHOULD_ENTER
};

typedef struct sandboxEvent {
  enum enum_event_type event_type;
  void* key;
  int key_size;
} BoxEvent;

typedef struct activity {
  enum enum_queue_state queue_state;
  struct timespec execution_start;
  struct timespec defer_time;
  struct timespec delaying_start;
  HashMap delaying_starts;
  int owned_mutex; //TODO: use a slab to store each element and get one from the pool when you need to use it.
  int is_preempted;
  int queue_event;
  void *key;
} Activity;

typedef struct pSandbox {
  long bid;    // sandbox id used by syscalls
  enum enum_psandbox_state state;
  Activity *activity;
  float delay_ratio;
  struct pSandbox *noisy_neighbor;
  struct pSandbox *victim;
  int counts[1000];
  int _count;
  int small_counts[10];
  int sandbox_flag;
} PSandbox;

typedef struct condition {
  int value;
  enum enum_condition compare;
  int temp_value;
} Condition;



/// @brief Create a performance sandbox
/// @param rule The rule to apply performance interference rule that the performance sandbox need to satisfy.
/// @return The point to the performance sandbox.
PSandbox *create_psandbox(float rule);

int release_psandbox(PSandbox *pSandbox);

/// @brief Update an event to the performance p_sandbox
/// @param event The event to notify the performance p_sandbox.
/// @param p_sandbox The p_sandbox to notify
/// @return On success 0 is returned.
int update_psandbox(struct sandboxEvent *event, PSandbox *p_sandbox);

void active_psandbox(PSandbox *pSandbox);
void freeze_psandbox(PSandbox *pSandbox);
struct pSandbox *get_psandbox();

/// @brief Update the queue condition to enter the queue
/// @param key The key of the queue
/// @param cond The condition for the current queue
/// @return On success 0 is returned.
/// The function must be called right after the try queue update
int psandbox_update_condition(int *keys, Condition cond);

/// @brief Find the sandbox that need this resource most to run first
/// @param sandbox that needs to be checked
/// @return On interference 1 is returned.
int psandbox_schedule();

int track_mutex(struct sandboxEvent *event, PSandbox *p_sandbox);

    void print_all();
#ifdef __cplusplus
}
#endif

#endif //PSANDBOX_USERLIB_PSANDBOX_H
