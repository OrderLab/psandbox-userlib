//
// The Psandbox project
//
// Created by yigonghu on 3/3/21.
//
// Copyright (c) 2021, Johns Hopkins University - Order Lab
//
//      All rights reserved.
//      Licensed under the Apache License, Version 2.0 (the "License");

#include <stdio.h>
#include <pthread.h>
#include "../libs/include/psandbox.h"

#define NUMBER  2
# define os_atomic_increment(ptr, amount) \
	__sync_add_and_fetch(ptr, amount)

# define os_atomic_decrement(ptr, amount) \
	__sync_sub_and_fetch(ptr, amount)

pthread_mutex_t mutex;

int n_active = 0;
int srv_thread_concurrency = 1;
int srv_thread_sleep_delay	= 10000;


int mysqld_main();
void srv_conc_enter_innodb();
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

void srv_conc_enter_innodb(){
  update_psandbox((size_t)&n_active, PREPARE);

  for (;;) {
    int	sleep_in_us;

    if (n_active < srv_thread_concurrency) {
      int active = os_atomic_increment(
          &n_active, 1);
      if (active <= srv_thread_concurrency) {
        return;
      }
      (void) os_atomic_decrement(
          &n_active, 1);
    }

    sleep_in_us = srv_thread_sleep_delay;
    os_thread_sleep(sleep_in_us);
  }

}

void* do_handle_one_connection(void* arg) {
  int j = *(int *)arg;
  PSandbox* psandbox = create_psandbox();

  active_psandbox(psandbox);
  for(int i = 0; i < 5; i++) {
    struct sandboxEvent event;
    srv_conc_enter_innodb();

    update_psandbox((size_t)&n_active, ENTER);
    if(j > 0) {
      int sleep_in_us = 1000000 * 2;
      os_thread_sleep(sleep_in_us);
    } else {
      int sleep_in_us = 1000000 * 1;
      os_thread_sleep(sleep_in_us);
    }

    (void) os_atomic_decrement(&n_active, 1);
    os_thread_sleep(10000);
  }
  freeze_psandbox(psandbox);
  release_psandbox(psandbox);
  return 0;
}

void create_new_thread(){
  pthread_t threads[NUMBER];
  int arg[NUMBER];
  int i;

  pthread_mutex_init(&mutex, NULL);

  for(i = 0; i<NUMBER; i++) {
    arg[i] = i;
  }
  for (i = 0; i < NUMBER; i++) {
    pthread_create (&threads[i], NULL, do_handle_one_connection, &arg[i]);
  }

  for (i = 0; i < NUMBER; i++)
  {
    pthread_join (threads[i], NULL);
  }
  pthread_mutex_destroy (&mutex);
}

int mysqld_main() {
  create_new_thread();
  return 0;
}
