//
// Created by yigonghu on 11/25/21.
//

#include <stdio.h>
#include <pthread.h>
#include <csignal>
#include "psandbox.h"

#define NUMBER  10000000
#define THREAD 20

void* do_handle_one_connection(void* arg) {
  int i,id;
  int key = 1000;
  IsolationRule rule;
  struct timespec  start, stop;
  int j = *(int *)arg;
  static long total_time = 0;
  rule.priority = 0;
  rule.isolation_level = 50;
  rule.type = RELATIVE;
  id = create_psandbox(rule);
  activate_psandbox(id);
  if (j == 19) {
    DBUG_TRACE(&start);
  }

  for (i = 0; i < NUMBER; i++) {
    update_psandbox(key,PREPARE);
    update_psandbox(key,ENTER);
    update_psandbox(key,HOLD);
    if(j != 19) {
      usleep(100);
    }
    update_psandbox(key,UNHOLD);
  }
  if (j == 19) {
    DBUG_TRACE(&stop);
    long time = time2ns(timeDiff(start,stop));
    total_time += time;
  }


  freeze_psandbox(id);
  if (j == 19) {
    printf("average time for update psandbox %lu ns\n", total_time/(4*NUMBER));
  }

  release_psandbox(id);
  return NULL;
}

int main() {
  pthread_t threads[THREAD];
  int arg[THREAD];
  int i;


  for(i = 0; i<THREAD; i++) {
    arg[i] = i;
  }

  for (i = 0; i < THREAD; i++) {
    pthread_create(&threads[i], NULL, do_handle_one_connection, &arg[i]);
  }

  pthread_join (threads[19], NULL);
  for (i = 0; i < THREAD; i++) {
    pthread_cancel(threads[i]);
  }
  return 0;
}