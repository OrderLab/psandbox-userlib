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
  static struct timespec every_second_start;
  static long total_time = 0;
  DBUG_TRACE(&start);
  for (i = 0; i < NUMBER; i++) {
    IsolationRule rule;
    rule.priority = 0;
    rule.isolation_level = 50;
    rule.type = RELATIVE;
    ids[i] = create_psandbox(rule);
  }
  DBUG_TRACE(&stop);
  long time = time2ns(timeDiff(start,stop));
  total_time += time;
  printf("average time for create psandbox %lu ns\n", total_time/NUMBER);


  for (i = 0; i < NUMBER; i++) {
    release_psandbox(ids[i]);
  }
  return 0;
}
