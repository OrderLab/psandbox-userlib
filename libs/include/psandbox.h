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

#ifdef __cplusplus
extern "C" {
#endif

enum enum_event_type {
  TRY_QUEUE,
  ENTER_QUEUE,
  EXIT_QUEUE,
  SLEEP_BEGIN,
  SLEEP_END,
  UPDATE_QUEUE_CONDITION,
  MUTEX_REQUIRE,
  MUTEX_GET,
  MUTEX_RELEASE
};

enum enum_key_type {
  INTEGER, FLOAT, LONGLONG, MUTEX
};

enum enum_psandbox_state {
  BOX_ACTIVE, BOX_FREEZE, BOX_START
};

enum enum_condition {
  COND_LARGE, COND_SMALL, COND_LARGE_OR_EQUAL, COND_SMALL_OR_EQUAL
};

enum enum_queue_state {
  QUEUE_NULL,QUEUE_ENTER,QUEUE_SLEEP,QUEUE_AWAKE
};

typedef struct activity {
  struct timeval execution_start;
  struct timeval delayed_time;
  struct timeval delaying_start;
  enum enum_queue_state queue_state;
} Activity;

typedef struct pSandbox {
  long box_id;    // sandbox id used by syscalls
  enum enum_psandbox_state state;
  Activity *activity;
  pid_t tid;
  float delay_ratio;
} PSandbox;

typedef struct condition {
  int value;
  enum enum_condition compare;
} Condition;

typedef struct sandboxEvent {
  enum enum_event_type event_type;
  void *key;
  enum enum_key_type key_type;
} BoxEvent;

PSandbox *pbox_create(float ratio);
int pbox_release(PSandbox *pSandbox);

/// @brief Update an event to the performance sandbox
/// @param event The event to notify the performance sandbox.
/// @param sandbox The sandbox to notify
/// @return On success 0 is returned.
int pbox_update(struct sandboxEvent event, PSandbox *sandbox);

int pbox_active(PSandbox *pSandbox);
int pbox_freeze(PSandbox *pSandbox);
struct pSandbox *pbox_get();

/// @brief Update the queue condition to enter the queue
/// @param key The key of the queue
/// @param cond The condition for the current queue
/// @return On success 0 is returned.
/// The function must be called right after the try queue update
int pbox_update_condition(int *key, Condition cond);

#ifdef __cplusplus
}
#endif

#endif //PSANDBOX_USERLIB_PSANDBOX_H
