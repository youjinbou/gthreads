#ifndef _GREEN_THREAD_H_
#define _GREEN_THREAD_H_

typedef struct gthread_s *gthread_t;

int        gthread_init(void);
int        gthread_create(gthread_t *, void *(*)(void *), void *);
int        gthread_cancel(gthread_t);
int        gthread_detach(gthread_t);
int        gthread_equal(gthread_t, gthread_t);
void       gthread_exit(void *);
int        gthread_join(gthread_t, void **);
gthread_t  gthread_self(void);
int        gthread_yield(void);

#endif /* _GREEN_THREAD_H_ */
