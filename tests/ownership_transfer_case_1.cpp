//
// The Psandbox project
//
// Created by amongthestarss on 7/15/21.
//
// Copyright (c) 2021, Johns Hopkins University - Order Lab
//
//      All rights reserved.
//      Licensed under the Apache License, Version 2.0 (the "License");

// PSandbox ownership transfer case 1
//  PSandbox owned by thread A transfers its ownership to thread B, 
//  and A knows B's thread id
// 

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

#define NUM_TASKS  5
int thread_pool_size = 2;

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



void find_available_thread() {
  for (;;) {
    int	sleep_in_us;

    if (n_active < thread_pool_size) {
      int active = os_atomic_increment(
          &n_active, 1);
      if (active <= thread_pool_size) {
        return;
      }
      (void) os_atomic_decrement(
          &n_active, 1);
    }

    sleep_in_us = srv_thread_sleep_delay;
    os_thread_sleep(sleep_in_us);
  }
}

void* do_handle_one_task(void* arg) {
  int j = *(int *)arg;

  printf("task %d  start\n", j);

  PSandbox *psandbox;
  psandbox = unmount_psandbox(pthread_self(), NULL);

  if(j > 0) {
    int sleep_in_us = 1000000 * 2;
    os_thread_sleep(sleep_in_us);
  } else {
    int sleep_in_us = 1000000 * 1;
    os_thread_sleep(sleep_in_us);
  }

  printf("task %d  end\n", j);
  (void) os_atomic_decrement(&n_active, 1);

  release_psandbox(psandbox);
  return 0;
}


void handle_tasks() {
  pthread_t threads[NUM_TASKS];
  int arg[NUM_TASKS];
  int i;

  for (i = 0; i < NUM_TASKS; i++) {
    arg[i] = i;
  }

  for (i = 0; i < NUM_TASKS; i++) {
    PSandbox* psandbox = create_psandbox(); 
    // and active the psandbox
    find_available_thread();
    printf("found available thread for task %d\n", i);
    pthread_create(&threads[i], NULL, do_handle_one_task, &arg[i]);
    mount_psandbox_thread(threads[i], psandbox);
  }
  
  for (i = 0; i < NUM_TASKS; i++)
  {
    pthread_join (threads[i], NULL);
  }
}

int mysqld_main() {
  handle_tasks();
  return 0;
}
