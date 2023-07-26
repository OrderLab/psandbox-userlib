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
#include "psandbox.h"

#define NUMBER  10000

int main() {
  int i;
  int ids[NUMBER];
  long total_time = 0;
  long time;
  struct timespec  start, stop;

  for (i = 0; i < NUMBER; i++) {
    IsolationRule rule;
    rule.priority = 0;
    rule.isolation_level = 50;
    rule.type = RELATIVE;
    ids[i] = create_psandbox(rule);

    DBUG_TRACE(&start);
    release_psandbox(ids[i]);
    DBUG_TRACE(&stop);
    time = time2ns(timeDiff(start,stop));
    total_time += time/2;
  }

  for (i = 0; i < NUMBER; i++) {
    DBUG_TRACE(&start);
    DBUG_TRACE(&stop);
    long time = time2ns(timeDiff(start,stop));
    total_time -= time;
  }

  printf("release, %lu\n", total_time/NUMBER);
  return 0;

}
