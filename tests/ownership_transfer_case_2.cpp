//
// The Psandbox project
//
// Created by amongthestarss on 7/15/21.
//
// Copyright (c) 2021, Johns Hopkins University - Order Lab
//
//      All rights reserved.
//      Licensed under the Apache License, Version 2.0 (the "License");

#include <stdio.h>
#include <pthread.h>
#include "../libs/include/psandbox.h"

# define os_atomic_increment(ptr, amount) \
	__sync_add_and_fetch(ptr, amount)

# define os_atomic_decrement(ptr, amount) \
	__sync_sub_and_fetch(ptr, amount)

pthread_mutex_t mutex;

int n_active = 0;
int srv_thread_sleep_delay	= 10000;

#define NUM_THREADS  2 
#define NUM_TASKS    5 
int queue_size = 2;

int mysqld_main();
void find_available_thread();
void os_thread_sleep(int tm);

int main() {
  return mysqld_main();
}

void
os_thread_sleep(
/*============*/
    int	tm)	/*!< in: time in microseconds */
{
  struct timeval t;

  t.tv_sec = tm / 1000000;
  t.tv_usec = tm % 1000000;

  select(0, NULL, NULL, NULL, &t);
}

void push_to_queue() {
  for (;;) {
    int	sleep_in_us;

    if (n_active < queue_size) {
      int active = os_atomic_increment(
          &n_active, 1);
      if (active <= queue_size) {
        return;
      }
      (void) os_atomic_decrement(
          &n_active, 1);
    }

    sleep_in_us = srv_thread_sleep_delay;
    os_thread_sleep(sleep_in_us);
  }
}

void pop_from_queue() {
  for (;;) {
    int	sleep_in_us;

    if (n_active > 0) {
      int active = os_atomic_decrement(
          &n_active, 1);
      if (active >= 0) {
        return;
      }
      (void) os_atomic_increment(
          &n_active, 1);
    }

    sleep_in_us = srv_thread_sleep_delay;
    os_thread_sleep(sleep_in_us);
  }
}



void* do_handle_one_task(void* arg) {
  int j = *(int *)arg;
  int sleep_in_us;

  for (;;) {
    pop_from_queue();
    printf("worker %d: pulled one task\n", j);
    sleep_in_us = 1000000 * 1;
    os_thread_sleep(sleep_in_us);
    printf("worker %d: finished one task\n", j);
  }

  return 0;
}


void handle_tasks() {
  pthread_t threads[NUM_THREADS];
  int arg[NUM_THREADS];
  int i;

  // init worker threads
  for (i = 0; i < NUM_THREADS; i++) {
    arg[i] = i;
  }
  for (i = 0; i < NUM_THREADS; i++) {
    pthread_create(&threads[i], NULL, do_handle_one_task, &arg[i]);
  }

  // assign tasks
  for (i = 0; i < NUM_TASKS; i++) {
    push_to_queue();
    printf("added task %d to queue\n", i);
  }
  
  for (i = 0; i < NUM_THREADS; i++)
  {
    pthread_join (threads[i], NULL);
  }
}

int mysqld_main() {
  handle_tasks();
  return 0;
}
