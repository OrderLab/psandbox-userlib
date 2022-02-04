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

#define NUMBER  10000000

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

  activate_psandbox(id);

  for (i = 0; i < NUMBER; i++) {
    unbind_psandbox(key,id,UNBIND_NONE);
    DBUG_TRACE(&start);
    bind_psandbox(key);
    DBUG_TRACE(&stop);
    long time = time2ns(timeDiff(start,stop));
    total_time += time;
  }

  for (i = 0; i < NUMBER; i++) {
    DBUG_TRACE(&start);
    DBUG_TRACE(&stop);
    long time = time2ns(timeDiff(start,stop));
    total_time -= time;
  }


  freeze_psandbox(id);

  printf("average time for bind psandbox %lu ns\n", total_time/NUMBER);
  release_psandbox(id);
  return 0;
}
