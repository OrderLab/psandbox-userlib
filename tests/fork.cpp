//
// Created by yigonghu on 11/24/21.
//

#include <stdio.h>
#include <pthread.h>
#include "psandbox.h"

#define NUMBER  1000000

int main() {
  int i,id;
  pthread_t threads;
  int arg = 0;
  struct timespec  start, stop;
  static struct timespec every_second_start;
  static long total_time = 0;

  for (i = 0; i < NUMBER; i++) {
    DBUG_TRACE(&start);
    if(fork() > 0 ) {
      DBUG_TRACE(&stop);
      long time = time2ns(timeDiff(start,stop));
      total_time += time;
    } else {
      return 0;
    }
  }


  printf("average time for update psandbox %lu ns\n", total_time/NUMBER);
  return 0;
}