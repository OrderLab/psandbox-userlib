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
#include <stdlib.h>
#include <pthread.h>
#include <locale>
#include <fcntl.h>
#include <string.h>
#include "../libs/include/psandbox.h"

#define NUM_THREADS  1
# define os_atomic_increment(ptr, amount) \
	__sync_add_and_fetch(ptr, amount)

# define os_atomic_decrement(ptr, amount) \
	__sync_sub_and_fetch(ptr, amount)

pthread_mutex_t mutex;

int n_active = 0;
int srv_thread_concurrency = 1;
int srv_thread_sleep_delay	= 100000;


int mysqld_main();
void srv_conc_enter_innodb();
void os_thread_sleep(int tm);

int main() {
  return mysqld_main();
}

void* do_handle_one_connection(void* arg) {
  int j = *(int *)arg;
  char seq[50];
  char buffer[30];
  sprintf(seq, "%d", j);
//  char *file_name = strcat(seq,".testfile.txt");
//  printf("the file name is %s\n",file_name);
//  int fd = open(file_name, O_WRONLY | O_APPEND);
//  write(fd,"my name is yigonghu!\n",20);
  clock_t start,end;
  start = clock();
  for(int i = 0; i < 100000; i++) {
//    read(fd,buffer,30);
      malloc(1024);
  }
  end = clock();
  printf("time =%fs\n",(double)(end-start)/CLOCKS_PER_SEC );
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
