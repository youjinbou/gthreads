#ifndef _PTHREAD_COMPAT_H_
#define _PTHREAD_COMPAT_H_

#include "green_thread.h"

#define pthread_t               gthread_t
#define pthread_init            gthread_init
#define pthread_create(a,b,c,d) gthread_create(a,c,d)
#define pthread_cancel          gthread_cancel
#define pthread_detach          gthread_detach
#define pthread_equal           gthread_equal
#define pthread_exit            gthread_exit
#define pthread_join            gthread_join
#define pthread_self            gthread_self
#define pthread_yield           gthread_yield

#endif /* _PTHREAD_COMPAT_H_ */
