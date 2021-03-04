//
// The Psandbox project
//
// Created by yigonghu on 2/18/21.
//
// Copyright (c) 2021, Johns Hopkins University - Order Lab
//
//      All rights reserved.
//      Licensed under the Apache License, Version 2.0 (the "License");
#include "../include/psandbox.h"
#include "../include/hashmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>



#define SYS_CREATE_PSANDBOX	436
#define SYS_RELEASE_PSANDBOX 437
#define SYS_GET_PSANDBOX 438
#define SYS_ACTIVE_PSANDBOX 439
#define SYS_FREEZE_PSANDBOX 440
#define SYS_UPDATE_PSANDBOX 441
#define SYS_UPDATE_CONDITION_PSANDBOX 442




const unsigned initial_size = 8;
HashMap psandbox_map;

PSandbox *create_psandbox(int rule) {
  struct pSandbox *psandbox;
  long sandbox_id;


  psandbox = (struct pSandbox*)malloc(sizeof(struct pSandbox));

  if (psandbox == NULL) return NULL;
  sandbox_id = syscall(SYS_CREATE_PSANDBOX, rule);

  if (sandbox_id == -1) {
    free(psandbox);
    printf("syscall failed with errno: %s\n", strerror(errno));
    return NULL;
  }
  Activity *act = (Activity *) calloc(1,sizeof(Activity));
  psandbox->activity = act;
  psandbox->bid = sandbox_id;
  psandbox->state = BOX_START;
  psandbox->delay_ratio = rule;

  if(psandbox_map.table_size == 0) {
    if (0 != hashmap_create(initial_size, &psandbox_map)) {
      free(psandbox);
      return NULL;
    }
  }

//  printf("set sandbox for the thread %d\n",tid);
  hashmap_put(&psandbox_map, sandbox_id, psandbox);
  return psandbox;
}

int release_psandbox(PSandbox* pSandbox){
  long success = 0;
  if (!pSandbox)
    return 0;

  success = syscall(SYS_RELEASE_PSANDBOX, pSandbox->bid);

  if(success == -1) {
    free(pSandbox);
    printf("failed to release sandbox in the kernel: %s\n", strerror(errno));
    return -1;
  }
  hashmap_remove(&psandbox_map, pSandbox->bid);
  free(pSandbox->activity);
  free(pSandbox);
  return 1;
}

int update_psandbox(struct sandboxEvent event, PSandbox *sandbox) {
  int success;

  if(!event.key)
    return -1;

  success = syscall(SYS_UPDATE_PSANDBOX,&event,sandbox->bid);

  return success;
}

int pbox_update_condition(int* key, Condition cond){
  int success;
  success = syscall(SYS_UPDATE_CONDITION_PSANDBOX,&cond, key);
  return success;
}

int active_psandbox(PSandbox* pSandbox) {
  int success = syscall(SYS_ACTIVE_PSANDBOX,pSandbox->bid);
  return success;
}

int freeze_psandbox(PSandbox* pSandbox) {
  int success = syscall(SYS_FREEZE_PSANDBOX,pSandbox->bid);

  return success;
}

PSandbox *get_psandbox() {
  int bid = syscall(SYS_GET_PSANDBOX);
  PSandbox *sandbox = (PSandbox *)hashmap_get(&psandbox_map, bid);

  if (NULL == sandbox) {
    printf("Error: Can't get sandbox for the thread %d\n",bid);
    return NULL;
  }
  return sandbox;
}
