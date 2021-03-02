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


enum enum_event_type
{
  ENTERLOOP,EXITLOOP,SLEEP_BEGIN,SLEEP_END,UPDATE_KEY,UPDATE_CONDITION
};

enum enum_key_type
{
  INTEGER, FLOAT, LONGLONG
};

enum enum_psandbox_state{
  ACTIVE,FREEZE,DELETED,START
};

enum enum_condition{
  LARGE,SMALL,LARGE_OR_EQUAL,SMALL_OR_EQUAL
};

typedef struct pSandbox {
  long box_id;    // sandbox id used by syscalls
  enum enum_psandbox_state state;
  pid_t tid;
  clock_t execution_start;
  clock_t delayed_time;
  clock_t delaying_time;
  float delay_ratio;
}PSandbox;

typedef struct condition {
  int value;
  enum enum_condition compare;
}Condition;

typedef struct sandboxEvent {
  enum enum_event_type event_type;
  void* key;
  enum enum_key_type key_type;
}BoxEvent;

PSandbox *pbox_create(float ratio);
int pbox_release(PSandbox* pSandbox);

/// @brief Update an event to the performance sandbox
/// @param event The event to notify the performance sandbox.
/// @param sandbox The sandbox to notify
/// @return On success 0 is returned.
int pbox_update(struct sandboxEvent event, PSandbox *sandbox);

int pbox_active(PSandbox* pSandbox);
int pbox_freeze(PSandbox* pSandbox);
struct pSandbox *pbox_get();
int pbox_update_condition(int* key, Condition cond);

#ifdef __cplusplus
}
#endif


#endif //PSANDBOX_USERLIB_PSANDBOX_H
