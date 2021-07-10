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


#ifdef __cplusplus
extern "C" {
#endif

#define HIGHEST_PRIORITY 2
#define MID_PRIORITY 1
#define LOW_PRIORITY 0

#define COMPENSATION_TICKET_NUMBER	1000L
#define PROBING_NUMBER 100

enum enum_event_type {
  PREPARE,
  ENTER,
  EXIT,
};

//Add comment for each enum type to describe the semantic and the constrains of the action for that state
enum enum_psandbox_state {
  BOX_ACTIVE, // psandbox starts to handle an activity
  BOX_FREEZE, // psandbox finishs an activity
  BOX_START, // create a psandbox
  BOX_PREEMPTED, // psandbox is a noisy neighbor and being preempted by the victim
  BOX_COMPENSATED, // psandbox is victim and is penalizing others
  BOX_PENDING_PENALTY // the penalty is pending
};

enum enum_condition {
  COND_LARGE, COND_SMALL, COND_LARGE_OR_EQUAL, COND_SMALL_OR_EQUAL
};

enum enum_activity_state {
  QUEUE_WAITING,QUEUE_ENTER,QUEUE_EXIT,QUEUE_PREEMPTED,QUEUE_PROMOTED
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
  int owned_mutex; //TODO: use a slab to store each element and get one from the pool when you need to use it.
  int is_preempted;
  int queue_event;
  void *key;
} Activity;

typedef struct pSandbox {
  long bid;    // sandbox id used by syscalls
  enum enum_psandbox_state state;
  Activity *activity;
  double tail_threshold; // the maximum ratio of activity that are allowed to break the threshold
  double max_defer; // the interference that allowed for each psandbox
  int finished_activities;
  int bad_activities;
  int action_level; // the psandbox is interferenced and needs future concern
  int compensation_ticket;
  struct pSandbox *noisy_neighbor;
  struct pSandbox *victim;

  //debugging
  int total_activity;
} PSandbox;

typedef struct condition {
  int value;
  enum enum_condition compare;
} Condition;



/// @brief Create a performance sandbox
/// @return The point to the performance sandbox.
PSandbox *create_psandbox();

/// @brief release a performance sandbox
/// @param p_sandbox The performance sandbox to release.
/// @return On success 1 is return
int release_psandbox(PSandbox *p_sandbox);

int add_rules(int total_types, double* defer_rule);

/// @brief Update an event to the performance p_sandbox
/// @param event The event to notify the performance p_sandbox.
/// @param p_sandbox The p_sandbox to notify
/// @return On success 1 is returned.
int update_psandbox(struct sandboxEvent *event, PSandbox *p_sandbox);

void active_psandbox(PSandbox *p_sandbox);
void freeze_psandbox(PSandbox *p_sandbox);
PSandbox *get_psandbox();

/// @brief Update the queue condition to enter the queue
/// @param key The key of the queue
/// @param cond The condition for the current queue
/// @return On success 0 is returned.
/// The function must be called right after the try queue update
int psandbox_update_condition(int *keys, Condition cond);

int psandbox_manager_init();

int track_mutex(struct sandboxEvent *event, PSandbox *p_sandbox);

int track_time(struct sandboxEvent *event, PSandbox *p_sandbox);
void print_all();

#ifdef __cplusplus
}
#endif

#endif //PSANDBOX_USERLIB_PSANDBOX_H
