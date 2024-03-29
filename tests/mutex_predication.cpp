//
// The Psandbox project
//
// Created by yigonghu on 3/16/21.
//
// Copyright (c) 2021, Johns Hopkins University - Order Lab
//
//      All rights reserved.
//      Licensed under the Apache License, Version 2.0 (the "License");

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
#include <syscall.h>
#include "../libs/include/psandbox.h"

#define NUMBER  4
#define LOOP_BASE 1
# define os_atomic_increment(ptr, amount) \
	__sync_add_and_fetch(ptr, amount)

# define os_atomic_decrement(ptr, amount) \
	__sync_sub_and_fetch(ptr, amount)

pthread_mutex_t mutex;
pthread_mutex_t mutex1;

enum enum_sql_command {
  SQLCOM_SELECT, SQLCOM_CREATE_TABLE, SQLCOM_CREATE_INDEX, SQLCOM_ALTER_TABLE,
  SQLCOM_UPDATE, SQLCOM_INSERT, SQLCOM_INSERT_SELECT};

int n_active = 0;
int srv_thread_concurrency = 4;
int srv_thread_sleep_delay	= 1000;


int mysqld_main();
void srv_conc_enter_innodb();
void os_thread_sleep(int tm);
int mysql_execute_command(enum_sql_command sql_command, int id);
int mysql_select(int id);
void row_search_mysql(int id, PSandbox* sandbox);

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
}

int mysql_execute_command(enum_sql_command sql_command, int id) {
  int ret = 0;
  switch (sql_command) {
    case SQLCOM_SELECT:
    {
      ret= mysql_select(id);
    }
  }
  return ret;
}

int mysql_select(int id) {
  int loop = LOOP_BASE;
  int i;
  PSandbox *sandbox = get_current_psandbox();
  if (id == 0 ) {
    os_thread_sleep(10000);
  } else if (id >= 1 && id <=2) {
    os_thread_sleep(4000000);
  }
  for (i = 0; i < loop; i++) {
//      printf("call row_search_mysql %d\n",syscall(SYS_gettid));
    row_search_mysql(id,sandbox);
  }
}

void row_search_mysql(int id, PSandbox* psandbox) {
  BoxEvent event;

  pthread_mutex_lock(&mutex);

  if(id == 3) {
    os_thread_sleep(5000000);
  } else if (id == 1) {
    os_thread_sleep(1000000);
  }

  pthread_mutex_unlock(&mutex);

}

void* do_handle_one_connection(void* arg) {
  int id = *(int *)arg;
  if ( id == 0) {
    os_thread_sleep(4000000);
  }
  PSandbox* box = create_psandbox();

//  printf("create box %d\n",syscall(SYS_gettid));
  for(int i = 0; i < 1; i++) {
    active_psandbox(box);
    mysql_execute_command(SQLCOM_SELECT,id);
    freeze_psandbox(box);
  }

  release_psandbox(box);
//  printf("release %d\n",syscall(SYS_gettid));
  return 0;
}

void create_new_thread(){
  pthread_t threads[NUMBER];
  int arg[NUMBER];
  int i;
  pthread_mutex_init(&mutex, NULL);
  pthread_mutex_init(&mutex1, NULL);
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
  pthread_mutex_destroy (&mutex1);
}

int mysqld_main() {
  create_new_thread();
  return 0;
}

