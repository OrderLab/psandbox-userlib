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

#define NUMBER  1000000

int main() {
  int i,id;
  int key = 1000;

  IsolationRule rule;
  rule.priority = 0;
  rule.isolation_level = 50;
  rule.type = RELATIVE;
  id = create_psandbox(rule);
  struct timespec  start, stop;
  static struct timespec every_second_start;
  static long total_time = 0;


  for (i = 0; i < NUMBER; i++) {
    activate_psandbox(id);
    DBUG_TRACE(&start);
    update_psandbox(key,PREPARE);
    update_psandbox(key,ENTER);
    update_psandbox(key,HOLD);
    update_psandbox(key,UNHOLD);
    DBUG_TRACE(&stop);
    long time = time2ns(timeDiff(start,stop));
    total_time += time;
    freeze_psandbox(id);
  }


  printf("average time for activate psandbox %lu ns\n", total_time/(4*NUMBER));
  release_psandbox(id);
  return 0;
}
