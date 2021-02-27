#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "../libs/include/psandbox.h"

#define NUM_THREADS  5
# define os_atomic_increment(ptr, amount) \
	__sync_add_and_fetch(ptr, amount)

# define os_atomic_decrement(ptr, amount) \
	__sync_sub_and_fetch(ptr, amount)

pthread_mutex_t mutex;

int n_active = 0;
int srv_thread_concurrency = 4;
int srv_thread_sleep_delay	= 1000;


int mysqld_main();
void srv_conc_enter_innodb();
void os_thread_sleep(int tm);

int main() {
  return mysqld_main();
}



void
os_thread_sleep(
/*============*/
    int	tm)	/*!< in: time in microseconds */
{
  struct timeval	t;

  t.tv_sec = tm / 1000000;
  t.tv_usec = tm % 1000000;

  select(0, NULL, NULL, NULL, &t);
}

void srv_conc_enter_innodb(){
  struct sandboxEvent event;
  event.event_type=ENTERLOOP;
//  pbox_update(event);
  for (;;) {
    int	sleep_in_us;

    if (n_active < srv_thread_concurrency) {
      int active = os_atomic_increment(
          &n_active, 1);
      if (active <= srv_thread_concurrency) {
        event.event_type=EXITLOOP;
//        pbox_update(event);

        return;
      }
      (void) os_atomic_increment(
          &n_active, 1);
    }

    sleep_in_us = srv_thread_sleep_delay;
    os_thread_sleep(sleep_in_us);
  }

}

void* do_handle_one_connection(void* arg) {
//  pSandbox* box = pbox_create();
  srv_conc_enter_innodb();
  pthread_mutex_lock(&mutex);
  int i = *(int *)arg;
  pthread_mutex_unlock(&mutex);
  if(i<4) {
    int sleep_in_us = 1000000 * 5;
    os_thread_sleep(sleep_in_us);
  } else {
    int sleep_in_us = 1000000 * 1;
    os_thread_sleep(sleep_in_us);
  }

  (void) os_atomic_decrement(&n_active, 1);

  printf("id = %d\n",i);
//  pbox_release(box);
  return 0;
}

void create_new_thread(){
  pthread_t threads[NUM_THREADS];
  int arg[NUM_THREADS];
  int i;
  pthread_mutex_init(&mutex, NULL);

  for(i = 0; i<NUM_THREADS;i++) {
    arg[i] = i;
  }
  for (i = 0; i < NUM_THREADS; i++) {
    pthread_create (&threads[i], NULL, do_handle_one_connection, &arg[i]);
  }

  for (i = 0; i < NUM_THREADS; i++)
  {
    pthread_join (threads[i], NULL);
  }
  pthread_mutex_destroy (&mutex);
}

int mysqld_main() {
  create_new_thread();
  return 0;
}
