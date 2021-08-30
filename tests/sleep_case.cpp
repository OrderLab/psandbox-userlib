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
#include <pthread.h>
#include <fstream>
#include "../libs/include/psandbox.h"
#include <unistd.h>


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
  if (flush_to_disk) {
    update_psandbox((size_t)&n_pending_flushes, PREPARE);


    pthread_mutex_lock(&mutex);
retry:
    if(n_pending_flushes>0) {
      pthread_mutex_unlock(&mutex);
      os_thread_sleep(5000000);
      pthread_mutex_lock(&mutex);

      goto retry;
    }

    update_psandbox((size_t)&n_pending_flushes, ENTER);

    n_pending_flushes++;

    char *buffer = "Yigong Hu";
    std::ofstream file;
    file.open("output.txt");
    file.write(buffer, 9);
    file.flush();

    file.close();
    os_thread_sleep(1000);
    pthread_mutex_lock(&mutex);
    n_pending_flushes--;

    pthread_mutex_unlock(&mutex);
    update_psandbox((size_t) &n_pending_flushes, HOLD);
  }
}

void* do_handle_one_connection(void* arg) {
  PSandbox* box = create_psandbox();

  for(int i = 0; i < 1; i++) {
    active_psandbox(box);
    mysql_execute_command(SQLCOM_INSERT);
    freeze_psandbox(box);
  }

  release_psandbox(box);
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
