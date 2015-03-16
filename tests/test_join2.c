#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include "gthread.h"

/* ------- */

void *hello(void *_){
  printf("hello...");
  return (void *)1;
}

int main(){
  gthread_t thread;
  int r;
  gthread_init();
  gthread_create(&thread, hello, NULL);
  fprintf(stderr, "thread = %p\n", (void *)thread);
  r = gthread_join(thread, NULL);
  printf(" world!\n");
  switch (r) {
  case 0:
    printf("thread joined\n");
    break;
  case ESRCH:
    printf("invalid thread id?\n");
    break;
  case EINVAL:
    printf("thread cannot be joined?\n");
    break;
  case EDEADLK:
    printf("possible deadlock?\n");
    break;
  default:
    printf("what?\n");
  }
  gthread_exit(NULL);
  return 0;
}
