#include <stdint.h>
#include <malloc.h>
#include <errno.h> /* pthreads error codes */
#include <ucontext.h>
#include "green_thread.h"
#include "list.h"
#include "event.h"

/*
  green threads: userland implementation of multithreading.
  not real system threads, no preemption, a scheduler must be triggered manually
  to swap threads (yield).
  => there's a queue of threads, and the scheduler picks up the next available
     (ie. ready) thread and swap contexts.
  => a blocking system call blocks all the threads. How about wrapping blocking 
     POSIX IO syscalls with aio calls, and pass io results back from the scheduler
     to the caller? 

  The scheduler must be initialized somehow (can we add a hook to the process 
  startup, library loading?)

  issue:
  what to do in a real multithreading setup?
  - context may not be resumed by different threads
    => either: 
    * each system threads has its own gthread instances, and a special migration 
      API needs to be defined
    * gthreads are shared, and each gets tagged as running/sleeping to avoid 
      parallel executions + locks everywhere!

 */

typedef void *(*gcallable)(void*);
typedef void (*mcallable)();

/* green thread structure */
struct gthread_s {
  ucontext_t context;
  /* when waiting, event which will wake the thread up */
  struct event_s *wait_event;
  /* when joinable, thread which is waiting for it */
  struct gthread_s *waiter;
  /* thread exit result, either by return or by calling gthread_exit */
  void *result;
  union {
    uint32_t flags;
    struct {
      uint32_t cancel:1;
      uint32_t waiting:1;
      uint32_t detached:1;
      uint32_t exited:1;
      uint32_t starting:1;
      uint32_t reserved:27;
    };
  };
};

const size_t stack_size = 16384;

/* internal events ---------------------------------------- */

enum event {
  THREAD_CREATE,
  THREAD_CANCEL,
  /*
  THREAD_DETACH,
  THREAD_EQUAL,
  */
  THREAD_EXIT,
  THREAD_JOIN,
  /*
  THREAD_YIELD,
  */
  THREAD_IO,
};

struct event_create_s {
  gthread_t  *gthread;
  gcallable   callable;
  void       *data;
};

struct event_cancel_s {
  gthread_t *gthread;
  int        ret;
};

/*
struct event_detach_s {
  struct gthread_s *gthread;
  int       ret;
};

struct event_equal_s {
  struct gthread_s *gthread1;
  struct gthread_s *gthread2;
  int       ret;
};
*/

struct event_exit_s {
  void     *retval;
};

struct event_join_s {
  gthread_t gthread;
  void    **retval;
};

/*
struct event_yield_s {
};
*/

struct event_io_s {
};

struct event_s {
  event      tag;
  gthread_t  caller;
  void      *result;
  union {
    struct event_create_s create;
    struct event_cancel_s cancel;
    /*
    struct event_detach_s detach;
    struct event_equal_s  equal;
    */
    struct event_exit_s   exit;
    struct event_join_s   join;
    /*
    struct event_yield_s  yield;
    */
    struct event_io_s     io;
  };
};

typedef struct event_s event_t;

/* scheduler ---------------------------------------------- */

typedef struct scheduler_s {
  /* guarded singly linked list */
  list_t           event_list;

  /* circular list
   * ready list is not guarded, but it always contains at least 
   * one element
   * the first entry is the running thread
   */
  list_t           thread_ready;

  /* guarded singly linked list */
  list_t           thread_waiting;

  struct gthread_s thread;
} scheduler_t;

scheduler_t scheduler;

/* event queue handling ----------------------------------- */

static event_t *event_alloc(gthread_t caller, event tag){
  event_t *event = (event_t *)calloc(1,sizeof(event_t));
  event->tag     = tag;
  event->caller  = caller;
  return event;
}

static void event_push(event_t *event){
  list_push(&(scheduler.event_list), event);
}

static event_t *event_pop(){
  return (event_t *)list_pop(&(scheduler.event_list));
}

/* thread queue handling ---------------------------------- */

static void queue_init(gthread_t thr){
  node_t *first = list_node_alloc();
  first->data = (void *)thr;
  first->next = first;
  scheduler.thread_ready.first = first;
}

static void queue_create_entry(gthread_t thr){
  list_push(&(scheduler.thread_ready), (void *)thr);
}

static void queue_destroy_entry(gthread_t thr){
  /* warning: potential infinite loop if thr is not in
     the list */
  list_remove(&(scheduler.thread_ready), (void *)thr);
}

/* threads utils ------------------------------------------ */

static gthread_t _gthread_alloc(){
  return (gthread_t)calloc(1, sizeof(gthread_s));
}

static void _gthread_free(gthread_t thr){
  free(thr);
}

static int _gthread_ready(gthread_t thr){
  return !thr->waiting && !thr->exited;
}

/* create a gthread struct based on current running 
   context */
static gthread_t _gthread_setup_current(){
  gthread_t caller = _gthread_alloc();
  getcontext(&(caller->context));
  return caller;
}

static void _gthread_setup(gthread_t thread, gthread_t caller){
  thread->context.uc_stack.ss_sp = (char*)calloc(1,stack_size);
  thread->context.uc_stack.ss_size = stack_size;
  ucontext_t *ctx = NULL;
  if (caller){
    ctx = &(caller->context);
  }
  thread->context.uc_link = ctx;
}

static gthread_t _gthread_current(){
  return (gthread_t)scheduler.thread_ready.first->data;
}

static int _gthread_is_valid(gthread_t gthread){
  /* scan the lists of existing threads */
  node_t *it = scheduler.thread_ready.first;
  do {
    if ((gthread_t)it->data == gthread)
      return 1;
    it = it->next;
  } while (it != scheduler.thread_ready.first);
  it = scheduler.thread_waiting.first;
  do {
    if ((gthread_t)it->data == gthread)
      return 1;
    it = it->next;
  } while (it != scheduler.thread_waiting.first);
  return 0;
}

static void _gthread_cancel(gthread_t gthread){
  gthread->cancel = 1;
}

static void _gthread_detach(gthread_t gthread){
  gthread->detached = 1;
}

static inline int _gthread_is_joinable(gthread_t gthread){
  return _gthread_is_valid(gthread) && !gthread->detached;
}

static inline int _gthread_is_detached(gthread_t gthread){
  return _gthread_is_valid(gthread) && gthread->detached;
}

static inline int _gthread_is_waited(gthread_t gthread){
  return _gthread_is_valid(gthread) && gthread->waiter != NULL;
}

static inline int _gthread_is_waiting(gthread_t waiter, gthread_t gthread){
  return _gthread_is_valid(gthread) && gthread->wait_event != NULL;
}

/* helper handling gthread function return value */
static void _gthread_callable_wrapper(gthread_t thr, gcallable fun, void *data){
  /* call the function and get the result */
  thr->result = fun(data);
}

/* API handling ------------------------------------------- */

void scheduler_gthread_create(event_t *event){
  /* create a new thread structure */
  gthread_t thread = _gthread_alloc();
  _gthread_setup(thread, event->caller);
  /* add it to the thread_ready */

}

void scheduler_gthread_exit(event_t *event){
  /* check if a thread is waiting for the end of current thread */
  /* destroy current thread */
  /* notify it of the end */
}

void scheduler_gthread_join(event_t *event){
}

/* scheduling --------------------------------------------- */

static void schedule(){
  while (1){
    /* rotate thread running queue */
    
    /* handle events */
    event_t *event = NULL;
    while (NULL != (event = event_pop())) {
      switch(event->tag){
      case THREAD_CREATE:
        scheduler_gthread_create(event);
        break;
        /*
      case THREAD_CANCEL:
        scheduler_gthread_cancel(event);
        break;
      case THREAD_DETACH:
        scheduler_gthread_detach(event);
        break;
      case THREAD_EQUAL:
        scheduler_gthread_equal(event);
        break;
        */
      case THREAD_EXIT:
        scheduler_gthread_exit(event);
        break;
      case THREAD_JOIN:
        scheduler_gthread_join(event);
        break;
        /*
      case THREAD_YIELD:
        scheduler_gthread_yield(event);
        break;
        */
      case THREAD_IO:
        break;
      default:
        /* fix-me: report error */
        break;
      }
    }
    /* select next runner */
    node_t *it = scheduler.thread_ready.first;
    gthread_t thr = (gthread_t)it->data;
    swapcontext(&(scheduler.thread.context), &(thr->context)); 
  }
}

static void scheduler_spawn(){
  /* create the scheduler context */
  _gthread_setup(&scheduler.thread,NULL);
  makecontext(&scheduler.thread.context, schedule, 0);
}

static void scheduler_preempt(){
  swapcontext(&(_gthread_current())->context,&(scheduler.thread.context));
}



/* API ---------------------------------------------------- */


/* fix-me: add return & error values */ 
int gthread_init(){
  /* extract the caller context, create a thread out
     of it and add it to the thread list */
  queue_init(_gthread_setup_current());
  scheduler_spawn();
  return 0;
}

/**
 * see pthread_create(3):
 *
 * RETURN VALUE
 *      On success, pthread_create() returns 0; on error, it returns an error number,
 *      and the contents of *thread are undefined.
 *
 * ERRORS
 *     EAGAIN Insufficient resources to create another thread, or a system-imposed limit
 *            on the number of threads was encountered.  The latter case may occur in two
 *            ways: the RLIMIT_NPROC soft  resource  limit  (set  via setrlimit(2)), which
 *            limits the number of process for a real user ID, was reached; or the kernel's
 *            system-wide limit on the number of threads, /proc/sys/kernel/threads-max,
 *            was reached.
 *
 *     EINVAL Invalid settings in attr.
 *
 *     EPERM  No permission to set the scheduling policy and parameters specified in attr.
 *
 **/
int gthread_create(gthread_t *gthread, gcallable callable, void *data){
  /* client context */
  event_t *event = event_alloc(_gthread_current(), THREAD_CREATE);
  event->create.gthread  = gthread;
  event->create.callable = callable;
  event->create.data     = data;
  event_push(event);
  /* move to scheduler context */
  scheduler_preempt();
  /* back to client context */
  return (int)(intptr_t)(event->result);
}

/**
 * see pthread_cancel(3):
 *
 * RETURN VALUE
 *      On success, pthread_cancel() returns 0; on error, it returns a nonzero error
 *      number.
 *
 * ERRORS
 *      ESRCH  No thread with the ID thread could be found.
 *
 **/
int gthread_cancel(gthread_t gthread){
  if (!_gthread_is_valid(gthread))
    return ESRCH;
  _gthread_cancel(gthread);
  return 0;
}

/**
 * see pthread_detach(3):
 *
 * RETURN VALUE
 *    On success, pthread_detach() returns 0; on error, it returns an error number.
 *
 * ERRORS
 *     EINVAL thread is not a joinable thread.
 *
 *     ESRCH  No thread with the ID thread could be found.
 *
 **/
int gthread_detach(gthread_t gthread){
  if (!_gthread_is_valid(gthread))
    return ESRCH;
  if (!_gthread_is_joinable(gthread))
    return EINVAL;
  _gthread_detach(gthread);
  return 0;
}

/**
 * see pthread_equal(3):
 *
 * RETURN VALUE
 *      If the two thread IDs are equal, pthread_equal() returns a nonzero value;
 *      otherwise, it returns 0.
 *
 * ERRORS
 *    This function always succeeds.
 *
 **/
int gthread_equal(gthread_t gthr1, gthread_t gthr2){
  return gthr1 == gthr2; /* fix-me, use an id */
}

void gthread_exit(void *retval){
  event_t *event = event_alloc(_gthread_current(), THREAD_EXIT);
  event->exit.retval  = retval;
  event_push(event);
  scheduler_preempt();
}

/**
 * see pthread_join(3):
 *
 *  RETURN VALUE
 *         On success, pthread_join() returns 0; on error, it returns an error number.
 *
 *  ERRORS
 *         EDEADLK
 *                A deadlock was detected (e.g., two threads tried to join with each other); 
 *                or thread specifies the calling thread.
 *
 *         EINVAL thread is not a joinable thread.
 *
 *         EINVAL Another thread is already waiting to join with this thread.
 *
 *         ESRCH  No thread with the ID thread could be found.
 *
 **/
int gthread_join(gthread_t gthread, void **retval){
  if (!_gthread_is_valid(gthread))
    return ESRCH;
  if (_gthread_is_detached(gthread) || _gthread_is_waited(gthread))
    return EINVAL;
  if (_gthread_is_waiting(gthread, _gthread_current()))
    return EDEADLK;
  /* current thread put in waiting mode until gthr
     has exited */
  event_t *event = event_alloc(_gthread_current(), THREAD_JOIN);
  event->join.gthread = gthread;
  event->join.retval  = retval;
  event_push(event);
  scheduler_preempt();
  return (int)(intptr_t)event->result;
}

/**
 * see pthread_self(3):
 *
 * RETURN VALUE
 *       This function always succeeds, returning the calling thread's ID.
 *
 * ERRORS
 *       This function always succeeds.
 *
 **/
gthread_t gthread_self(void){
  return _gthread_current();
}

/**
 * see pthread_yield(3):
 *
 * RETURN VALUE
 *       On success, pthread_yield() returns 0; on error, it returns an error number.
 *
 * ERRORS
 *       On Linux, this call always succeeds (but portable and future-proof applications
 *       should nevertheless handle a possible error return).
 *
 **/
int gthread_yield(void){
  scheduler_preempt();
  return 0;
}
