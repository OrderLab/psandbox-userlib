//
// Created by yigonghu on 11/24/21.
//

#include <stdio.h>
#include <pthread.h>
#include "psandbox.h"

#define NUMBER  100000

int main() {
  int i,id;
  struct timespec  start, stop;
  static long total_time = 0;
  DBUG_TRACE(&start);
  for (i = 0; i < NUMBER; i++) {

    if(fork() > 0 ) {
      continue;
    } else {
      return 0;
    }
  }
  DBUG_TRACE(&stop);
  long time = time2ns(timeDiff(start,stop));
  total_time += time;

  printf("average time for fork %lu ns\n", total_time/NUMBER);
  return 0;
}