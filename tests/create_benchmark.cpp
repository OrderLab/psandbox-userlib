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

  struct timespec  start, stop;
  long total_time = 0;


  for (i = 0; i < NUMBER; i++) {
    IsolationRule rule;
    rule.priority = 0;
    rule.isolation_level = 50;
    rule.type = RELATIVE;
    DBUG_TRACE(&start);
    ids[i] = create_psandbox(rule);
    DBUG_TRACE(&stop);
    long time = time2ns(timeDiff(start,stop));
    total_time += time;
    release_psandbox(ids[i]);
  }

  for (i = 0; i < NUMBER; i++) {
    DBUG_TRACE(&start);
    DBUG_TRACE(&stop);
    long time = time2ns(timeDiff(start,stop));
    total_time -= time;
  }

  printf("create, %lu\n", total_time/NUMBER);

  return 0;
}
