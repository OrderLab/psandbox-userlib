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
#include "linked_list.h"

# define os_atomic_increment(ptr, amount) \
	__sync_add_and_fetch(ptr, amount)

# define os_atomic_decrement(ptr, amount) \
	__sync_sub_and_fetch(ptr, amount)

pthread_mutex_t task_queue_lock;
node_t *task_queue;
int queue_count = 0;
int queue_size = 3;

int srv_thread_sleep_delay	= 10000;

#define NUM_WORKERS  2 
#define NUM_TASKS    3 

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

void push_to_queue(size_t task) {
  for (;;) {
    int	sleep_in_us;

    pthread_mutex_lock(&task_queue_lock);

    // if (queue_count < queue_size) {
    //   int active = os_atomic_increment(
    //       &queue_count, 1);
    //   if (active <= queue_size) {
    //     return;
    //   }
    //   (void) os_atomic_decrement(
    //       &queue_count, 1);
    // }
    if (queue_count < queue_size) {
      task_queue = queue_push(task_queue, task);
      queue_count++;
      pthread_mutex_unlock(&task_queue_lock);
      return;
    } 
    pthread_mutex_unlock(&task_queue_lock);

    sleep_in_us = srv_thread_sleep_delay;
    os_thread_sleep(sleep_in_us);
  }
}

size_t pop_from_queue() {
  for (;;) {
    int	sleep_in_us;
    size_t task;

    // if (queue_count > 0) {
    //   int active = os_atomic_decrement(
    //       &queue_count, 1);
    //   if (active >= 0) {
    //     return;
    //   }
    //   (void) os_atomic_increment(
    //       &queue_count, 1);
    // }

    pthread_mutex_lock(&task_queue_lock);
    if (queue_count > 0) {
      task = task_queue->data;
      task_queue = queue_pop(task_queue);
      queue_count--;
      pthread_mutex_unlock(&task_queue_lock);
      return task;
    } 
    pthread_mutex_unlock(&task_queue_lock);

    sleep_in_us = srv_thread_sleep_delay;
    os_thread_sleep(sleep_in_us);
  }
}



void* do_handle_one_task(void* arg) {
  int j = *(int *)arg;
  int sleep_in_us;

  for (;;) {
    PSandbox *psandbox;
    size_t task;

    task = pop_from_queue();
    printf("worker %d: pulled one task\n", j);
    psandbox = bind_psandbox(task);
    sleep_in_us = 1000000 * 1;
    os_thread_sleep(sleep_in_us);
    printf("worker %d: finished one task\n", j);
    release_psandbox(psandbox);
  }

  return 0;
}


void handle_tasks() {
  pthread_t threads[NUM_WORKERS];
  int arg[NUM_WORKERS];
  int i;

  if (pthread_mutex_init(&task_queue_lock, NULL) != 0) {
    printf("failed to init lock\n");
    return;
  }

  // init worker threads
  for (i = 0; i < NUM_WORKERS; i++) {
    arg[i] = i;
  }
  for (i = 0; i < NUM_WORKERS; i++) {
    pthread_create(&threads[i], NULL, do_handle_one_task, &arg[i]);
  }



  // assign tasks
  for (i = 0; i < NUM_TASKS; i++) {
    size_t task = i;
    PSandbox* psandbox = create_psandbox(); 
    // active the sandbox
    // do some work
    unbind_psandbox(task, psandbox);
    push_to_queue(task);
    printf("added task %d to queue\n", i);
  }
  


  for (i = 0; i < NUM_WORKERS; i++) {
    pthread_join (threads[i], NULL);
  }
}

int mysqld_main() {
  handle_tasks();
  return 0;
}
