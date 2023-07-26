//
// Created by yigonghu on 11/24/21.
//

#include <stdio.h>
#include <pthread.h>
#include "psandbox.h"

#define NUMBER  10000

void* do_handle_one_connection(void* arg) {
  return 0;
}

int main() {
  int i,id;
  pthread_t threads;
  int arg = 0;
  struct timespec  start, stop;
  static struct timespec every_second_start;
  static long total_time = 0;
  DBUG_TRACE(&start);
  for (i = 0; i < NUMBER; i++) {
    pthread_create (&threads, NULL, do_handle_one_connection, &arg);
  }
  DBUG_TRACE(&stop);
  long time = time2ns(timeDiff(start,stop));
  total_time += time;

  printf("pthread_create, %lu\n", total_time/NUMBER);
  return 0;
}