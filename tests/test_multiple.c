#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include "gthread.h"

struct th1_s {
  int counter;
};

struct th2_s {
  struct th1_s data[100];
  float average;
} th2;

void *run1(void *data){
  struct th1_s *th1 = (struct th1_s *)data;
  th1->counter++;
  gthread_exit(NULL);
  /* unreachable */
  return NULL;
}

void *run2(void *data){
  struct th2_s *th2 = (struct th2_s *)data;
  int i;
  gthread_t threads[100];
  for (i = 0; i < 100; i++){
    th2->data[i].counter = i;
    gthread_create(threads+i, run1, th2->data+i);
  }
  for (i = 0; i < 100; i++){
    gthread_join(threads[i], NULL);
    th2->average += th2->data[i].counter;
  }
  th2->average /= 100;
  return NULL;
}

int main(){
  gthread_t thread;
  gthread_create(&thread, run2, (void *)&th2);
  gthread_join(thread, NULL);
  printf("result : %f\n", th2.average);
  return 0;
}
