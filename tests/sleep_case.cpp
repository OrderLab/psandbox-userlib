//
// The Psandbox project
//
// Created by yigonghu on 2/24/21.
//
// Copyright (c) 2021, Johns Hopkins University - Order Lab
//
//      All rights reserved.
//      Licensed under the Apache License, Version 2.0 (the "License");

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <fstream>
#include "../libs/include/psandbox.h"
#include <unistd.h>
#include <sys/syscall.h>

#define NUM_THREADS  2
# define os_atomic_increment(ptr, amount) \
	__sync_add_and_fetch(ptr, amount)

# define os_atomic_decrement(ptr, amount) \
	__sync_sub_and_fetch(ptr, amount)

pthread_mutex_t mutex;
#define ibool        int
#define TRUE        (1)    /* Logical true */
#define FALSE        (0)    /* Logical false */


int n_active = 0;
int n_pending_flushes=0;
enum enum_sql_command {
  SQLCOM_SELECT, SQLCOM_CREATE_TABLE, SQLCOM_CREATE_INDEX, SQLCOM_ALTER_TABLE,
  SQLCOM_UPDATE, SQLCOM_INSERT, SQLCOM_INSERT_SELECT};

int    srv_flush_log_at_trx_commit = 1;
void log_write_up_to(ibool flush_to_disk);
int mysqld_main();
int mysql_execute_command(enum_sql_command sql_command);
void os_thread_sleep(int tm);
int mysql_insert();


int main() {
  return mysqld_main();
}

void
os_thread_sleep(
/*============*/
    int	tm)	/*!< in: time in microseconds */
{
  struct timeval	t;

  t.tv_sec = tm / 1000000;
  t.tv_usec = tm % 1000000;

  select(0, NULL, NULL, NULL, &t);
//  usleep(tm);
}

int mysql_execute_command(enum_sql_command sql_command) {
    int ret = 0;
    switch (sql_command) {
      case SQLCOM_INSERT:
      {
        ret= mysql_insert();
      }
    }
    return ret;
  }

int mysql_insert() {
    if (srv_flush_log_at_trx_commit == 0) {
      // assert(0); //Do nothing
    } else if (srv_flush_log_at_trx_commit == 1) {
        log_write_up_to(TRUE);

    } else if (srv_flush_log_at_trx_commit == 2) {
      log_write_up_to(FALSE);
      // assert(0);
    }
}

void log_write_up_to(ibool flush_to_disk) {
  BoxEvent event;
  if (flush_to_disk) {
    PSandbox *sandbox = pbox_get();
    Condition cond;
    cond.value = 0;
    cond.compare = LARGE;
    pbox_update_condition(&n_pending_flushes,cond);
    event.event_type = ENTERLOOP;
    event.key_type = INTEGER;
    event.key = &n_pending_flushes;
    pbox_update(event,sandbox);
    pthread_mutex_lock(&mutex);
retry:
    if(n_pending_flushes>0) {
      pthread_mutex_unlock(&mutex);
      event.event_type = SLEEP_BEGIN;
      event.key_type = INTEGER;
      event.key = &n_pending_flushes;
      pbox_update(event,sandbox);
      os_thread_sleep(5000000);
      event.event_type = SLEEP_END;
      event.key = &n_pending_flushes;
      event.key_type = INTEGER;
      pbox_update(event,sandbox);
      printf("getpid %d sleep\n",syscall(SYS_gettid));
      pthread_mutex_lock(&mutex);
      goto retry;
    }

    n_pending_flushes++;
    event.event_type = UPDATE_KEY;
    event.key = &n_pending_flushes;
    event.key_type = INTEGER;
    pbox_update(event,sandbox);
    pthread_mutex_unlock(&mutex);
    char *buffer = "Yigong Hu";
    std::ofstream file;
    file.open("output.txt");
    file.write(buffer, 9);
    file.flush();
    os_thread_sleep(1000);
    file.close();

    pthread_mutex_lock(&mutex);
    n_pending_flushes--;
    event.event_type = UPDATE_KEY;
    event.key_type = INTEGER;
    pbox_update(event,sandbox);
    pthread_mutex_unlock(&mutex);
  }
}

void* do_handle_one_connection(void* arg) {
  pSandbox* box = pbox_create(0.2);
  
  printf("create box %d\n",syscall(SYS_gettid));
  for(int i = 0; i < 1; i++) {
    pbox_active(box);
    mysql_execute_command(SQLCOM_INSERT);
    pbox_freeze(box);
  }
  //sleep(1);
  pbox_release(box);
  printf("release %d\n",syscall(SYS_gettid));
  return 0;
}

void create_new_thread(){
  pthread_t threads[NUM_THREADS];
  int arg[NUM_THREADS];
  int i;
  pthread_mutex_init(&mutex, NULL);

  for(i = 0; i<NUM_THREADS;i++) {
    arg[i] = i;
  }
  for (i = 0; i < NUM_THREADS; i++) {
    pthread_create (&threads[i], NULL, do_handle_one_connection, &arg[i]);
  }

  for (i = 0; i < NUM_THREADS; i++)
  {
    pthread_join (threads[i], NULL);
  }
  pthread_mutex_destroy (&mutex);
}

int mysqld_main() {
  create_new_thread();
  return 0;
}
