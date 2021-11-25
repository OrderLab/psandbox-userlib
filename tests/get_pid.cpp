//
// Created by yigonghu on 11/24/21.
//

#include <stdio.h>
#include <pthread.h>
#include "psandbox.h"

#define NUMBER  10000000

int main() {
  int i,id;

  struct timespec  start, stop;
  static long total_time = 0;

  for (i = 0; i < NUMBER; i++) {
    DBUG_TRACE(&start);
    getpid();
    DBUG_TRACE(&stop);
    long time = time2ns(timeDiff(start,stop));
    total_time += time;
  }



  printf("average time for getpid %lu ns\n", total_time/NUMBER);
  return 0;
}