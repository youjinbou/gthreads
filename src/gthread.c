#include <stdint.h>
#include <malloc.h>
#include <errno.h> /* pthreads error codes */
#include <stdlib.h>
#include <ucontext.h>
#include <assert.h>
#include "gthread.h"
#include "list.h"
#include "debug.h"

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

/* forward declarations */
typedef struct event_s event_t;
typedef struct scheduler_s scheduler_t;

/* green thread structure */
struct gthread_s {
  /* owner */
  scheduler_t *scheduler;
  /* when waiting, event which will wake the thread up */
  struct event_s *wait_event;
  /* when joinable, thread which is waiting for it */
  struct gthread_s *waiter;
  union {
    uint32_t flags;
    struct {
      uint32_t cancel:1;
      uint32_t detached:1;
      uint32_t exited:1;
      uint32_t waiting:1;
      uint32_t reserved:29;
    };
  };
  ucontext_t context;
};

void indent(int k) {
  while (k--){
    DBG(" ");
  }
}

void gthread_dump(int k, const char *msg, struct gthread_s *gthread){
  indent(k);
  DBG("%s = %p {\n", msg, gthread);
  indent(k);
  DBG(" scheduler = %p\n", gthread->scheduler);
  indent(k);
  DBG(" wait_event = %p\n", gthread->wait_event);
  indent(k);
  DBG(" waiter = %p\n", gthread->waiter);
  indent(k);
  DBG(" flags = %d\n", gthread->flags);
  indent(k);
  DBG("}\n");
}

const size_t stack_size = 16384;

/* internal events ---------------------------------------- */

typedef enum {
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
} event;

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
  void*     retval;
};

struct event_join_s {
  gthread_t gthread;
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
  int        result; /* error code if any */
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

struct expect_s {
  event_t*   event;
  gthread_t* waiter;
};

/* scheduler ---------------------------------------------- */

struct scheduler_s {

  /* circular list
   * always contains at least one element
   */
  circular_t* thread_ready;
  
  /* simple singly linked lists */
  list_t*     thread_waiting;
  list_t*     thread_exited;
  list_t*     event_list;

  /* the scheduler thread */
  struct gthread_s gthread;
};

void scheduler_dump(int k, const char *msg, scheduler_t *scheduler){
  /*
    indent(k);
    DBG("%s = %p {\n", msg, scheduler);
    indent(k);
    circular_dump(k+2, "thread_ready", scheduler->thread_ready);
    indent(k);
    list_dump(k+2, "thread_waiting", scheduler->thread_waiting);
    indent(k);
    list_dump(k+2, "event_list", scheduler->event_list);
    gthread_dump(k + 2, "gthread", &(scheduler->gthread));
    indent(k);
    DBG(" };\n");
  */
}

/* scheduler instance
 * we cannot avoid making the scheduler a (static) global variable.
 */
static scheduler_t *scheduler;

/* memory handling ---------------------------------------- */

static void *alloc(size_t size){
  void *ptr = calloc(1,size);
  if (!ptr) {
    perror("gthread");
    exit(EXIT_FAILURE);
  }
  DBG("alloc : %p [%lu]\n", ptr, size);
  return ptr;
}

/* context abstraction ------------------------------------ */

static gthread_t gthread_current();

static void gthread_callable_wrapper(gthread_t gthread, gcallable callable, void *data);

static void gthread_snapshot(gthread_t gthread){
  getcontext(&(gthread->context));
}

static void gthread_context_alloc(gthread_t gthread){
  /* context stack */
  void* stack = alloc(stack_size);
  gthread->context.uc_stack.ss_sp = stack;
  gthread->context.uc_stack.ss_size = stack_size;
  gthread->context.uc_link = &scheduler->gthread.context;
}

/* Nota: the 2 functions below initialize gthread ucontexts.
   It seems that first a getcontext must be performed prior to
   context proper creation, to properly initialize certain values 
   (fpregs) in context... */
static void gthread_bind(gthread_t gthread, gcallable callable, void *arg){
  gthread_context_alloc(gthread);
  getcontext(&(gthread->context)); /* bug? */
  makecontext(&(gthread->context), (mcallable)gthread_callable_wrapper, 3, gthread, callable, arg);
}

static void scheduler_bind(gthread_t gthread, mcallable schedule, scheduler_t *scheduler){
  gthread_context_alloc(gthread);
  gthread->context.uc_link = NULL; /* the scheduler does not exit */
  getcontext(&(gthread->context)); /* bug? */
  makecontext(&(gthread->context), schedule, 1, scheduler);
}


static void scheduler_leave(scheduler_t *scheduler, gthread_t gthread){
  DBG_FUN("start");
  /*
    scheduler_dump(0, "scheduler", scheduler);
    gthread_dump(0, "gthread", gthread);
  */
  DBG("returning to %p\n", gthread);
  if (-1 == swapcontext(&(scheduler->gthread.context), &(gthread->context))){
    perror(__func__);
    exit(EXIT_FAILURE);
  }
  DBG_FUN("end");
}

static void scheduler_enter(){
  DBG_FUN("start");
  gthread_t current = gthread_current();
  /*
    scheduler_dump(0, "scheduler", current->scheduler);
    gthread_dump(0, "gthread", current);
    current->context.uc_link = &scheduler->gthread.context;
  */
  DBG("leaving %p\n", current);
  if (-1 == swapcontext(&(current->context), &(scheduler->gthread.context))){
    perror(__func__);
    exit(EXIT_FAILURE);
  }
  DBG_FUN("end");
}

/* event queue handling ----------------------------------- */

static event_t *event_alloc(gthread_t caller, event tag){
  event_t *event = (event_t *)alloc(sizeof(event_t));
  event->tag     = tag;
  event->caller  = caller;
  return event;
}

static void event_free(event_t *event){
  free(event);
}

static void event_init(scheduler_t *scheduler){
  scheduler->event_list = list_init();
}

static void event_push(scheduler_t *scheduler, event_t *event){
  DBG_FUN("start");
  list_push(scheduler->event_list, event);
  DBG_FUN("end");
}

static event_t *event_pop(scheduler_t *scheduler){
  DBG_FUN("start");
  event_t *ev = (event_t *)list_pop(scheduler->event_list);
  if (ev == NULL)
    DBG("no events\n");
  DBG_FUN("end");
  return ev;
}

/* thread queue handling ---------------------------------- */

static void queue_init(scheduler_t *scheduler, gthread_t gthread){
  DBG_FUN("start");
  scheduler->thread_ready   = circular_init();
  scheduler->thread_waiting = list_init();
  scheduler->thread_exited  = list_init();
  circular_push(scheduler->thread_ready, gthread);
  gthread->scheduler = scheduler;
  DBG_FUN("end");
}

static void queue_create_entry(gthread_t gthread){
  DBG_FUN("start");
  DBG("pushing %p\n", gthread);
  circular_push(scheduler->thread_ready, gthread);
}

/* called when a thread is terminated */
static void queue_destroy_entry(gthread_t gthread){
  DBG_FUN("start");
  /* warning: potential infinite loop if thr is not in the list */
  circular_remove(scheduler->thread_ready, gthread);
  /* could a thread be in this list when being terminated? */
  list_remove(scheduler->thread_waiting, gthread);
  assert(0 == circular_mem(scheduler->thread_ready, gthread));
  DBG_FUN("end");
}

/* threads utils ------------------------------------------ */

static gthread_t gthread_alloc(){
  DBG_FUN("start");
  gthread_t thread = (gthread_t)alloc(sizeof(struct gthread_s));
  DBG_FUN("end");
  return thread;
}

static void gthread_free(gthread_t gthread){
  DBG_FUN("start");
  free(gthread);
  DBG_FUN("end");
}

/* create a gthread struct based on current running 
   context */
static gthread_t gthread_setup_current(){
  gthread_t caller = gthread_alloc();
  gthread_context_alloc(caller);
  gthread_snapshot(caller);
  return caller;
}

static gthread_t gthread_current(){
  gthread_t current = (gthread_t)circular_top(scheduler->thread_ready);
  return current;
}

static int gthread_is_valid(gthread_t gthread){
  /* scan the lists of existing threads */
  return circular_mem(scheduler->thread_ready, gthread) || list_mem(scheduler->thread_waiting, gthread) || list_mem(scheduler->thread_exited, gthread);
}

static void scheduler_gthread_cancel(gthread_t gthread){
  gthread->cancel = 1;
}

static void scheduler_gthread_detach(gthread_t gthread){
  gthread->detached = 1;
}

static inline int gthread_is_joinable(gthread_t gthread){
  return gthread_is_valid(gthread) && !gthread->detached;
}

static inline int gthread_is_detached(gthread_t gthread){
  return gthread_is_valid(gthread) && gthread->detached;
}

static inline int gthread_is_waited(gthread_t gthread){
  return gthread_is_valid(gthread) && gthread->waiter != NULL;
}

static inline int gthread_is_waiting(gthread_t waiter, gthread_t gthread){
  return gthread_is_valid(gthread) && gthread->wait_event != NULL;
}

static inline int gthread_has_exited(gthread_t gthread){
  return gthread->exited;
}

static void scheduler_gthread_event_wait(scheduler_t *scheduler, gthread_t gthread, event_t *event){
  gthread->wait_event = event;
  event->caller      = gthread;
  gthread->waiting = 1;
  circular_remove(scheduler->thread_ready, gthread);
  list_push(scheduler->thread_waiting, gthread);
}

static void scheduler_gthread_wakeup(scheduler_t *scheduler, gthread_t gthread){
  list_remove(scheduler->thread_waiting, gthread);
  gthread->waiting = 0;
  circular_push(scheduler->thread_ready, gthread);
}

static size_t scheduler_gthread_count(scheduler_t *scheduler){
  return circular_length(scheduler->thread_ready); // + list_length(&scheduler->thread_waiting);
} 

/* event handling ----------------------------------------- */

void scheduler_gthread_notify(scheduler_t *scheduler, gthread_t gthread, event_t *event){
  if (gthread->waiting){
    /* drop event which triggered sleep */
    event_free(gthread->wait_event);
    /* replace it with the event result */
    gthread->wait_event = event;
    scheduler_gthread_wakeup(scheduler, gthread);
  } else {
    DBG("invalid call to notify on non sleeping thread\n");
    exit(EXIT_FAILURE);
  }
}

/* API handling ------------------------------------------- */

/* helper handling gthread function return value */
static void gthread_callable_wrapper(gthread_t gthread, gcallable callable, void *data){
  /* call the function and post the result as an event */
  gthread_exit(callable(data));
}

static void scheduler_gthread_setup(scheduler_t *scheduler, gthread_t gthread, event_t *event){
  void*         data = event->create.data;
  gcallable callable = event->create.callable;
  gthread->scheduler = scheduler;
  gthread_bind(gthread, callable, data);
  *(event->create.gthread) = gthread;
}

static void scheduler_gthread_destroy(scheduler_t *scheduler, gthread_t gthread){
  DBG_FUN("start");
  DBG("gthread = %p\n", gthread);
  if (gthread->exited){
    list_remove(scheduler->thread_exited, gthread);
    gthread_free(gthread);
  } else {
    DBG("trying to destroy running thread!\n");
  }
  DBG_FUN("end");
}

/* create event */
void scheduler_gthread_create(scheduler_t *scheduler, event_t *event){
  /* create a new thread structure */
  DBG_FUN("start");
  gthread_t gthread = gthread_alloc();
  scheduler_gthread_setup(scheduler, gthread, event);
  gthread_dump(0,"created gthread", gthread);
  /* add it to the ready thread list */
  queue_create_entry(gthread);
  /* error code */
  event->result = 0;
  DBG_FUN("end");
}

/* exit event */
void scheduler_gthread_exit(scheduler_t *scheduler, event_t *event){
  gthread_t waiter;
  event->caller->exited = 1;
  /* take thread out of running queue */
  queue_destroy_entry(event->caller);
  /* move current thread to exited */
  list_push(scheduler->thread_exited, event->caller);
  if (gthread_is_detached(event->caller)){
    scheduler_gthread_destroy(scheduler, event->caller);
    event_free(event);
  } else {
    /* check if a thread is waiting for the end of current thread */
    waiter = event->caller->waiter;
    /* notify waiter of the end */
    if (waiter)
      scheduler_gthread_notify(scheduler, waiter, event);
    else /* or keep it for later */
      event->caller->wait_event = event;
  }
}

/* join event */
void scheduler_gthread_join(scheduler_t *scheduler, event_t *event){
  DBG_FUN("start");
  event->join.gthread->waiter = event->caller;
  /* put it to sleep */
  scheduler_gthread_event_wait(scheduler, event->caller, event);
  DBG_FUN("end");
}

/* scheduling --------------------------------------------- */

static void schedule(scheduler_t *scheduler){
  DBG_FUN("start");
  while (1){
    event_t *event = NULL;
    DBG("back to schedule\n");
    /* rotate thread running queue */
    DBG("rotating\n");
    circular_rotate(scheduler->thread_ready);
    /* handle events */
    DBG("thread count = %lu\n", scheduler_gthread_count(scheduler));
    event = event_pop(scheduler);
    while (NULL != event) {
      DBG("processing event\n");
      switch(event->tag){
      case THREAD_CREATE:
        scheduler_gthread_create(scheduler, event);
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
        scheduler_gthread_exit(scheduler, event);
        break;
      case THREAD_JOIN:
        scheduler_gthread_join(scheduler, event);
        break;
        /*
          case THREAD_YIELD:
          scheduler_gthread_yield(event);
          break;
        */
      case THREAD_IO:
        break;
      default:
        DBG("unknown event!");
        /* fix-me: report error */
        break;
      }
      event = event_pop(scheduler);
    } /* while */
    gthread_t current = (gthread_t)circular_top(scheduler->thread_ready);
    if (current)
      /* select next runner */
      scheduler_leave(scheduler, current);
    else 
      return;
  }
  DBG_FUN("end");
}

static void scheduler_init(scheduler_t **scheduler, gthread_t current){
  DBG_FUN("start");
  *scheduler = (scheduler_t *)alloc(sizeof(scheduler_t));
  queue_init(*scheduler, current);
  event_init(*scheduler);
  /* set scheduler thread up */
  gthread_t gthread  = &(*scheduler)->gthread;
  gthread->scheduler = *scheduler;
  scheduler_bind(gthread, schedule, *scheduler);
  DBG_FUN("end");
}

/* API ---------------------------------------------------- */

/* fix-me: add return & error values */ 
int gthread_init(){
  DBG_FUN("start");
  /* extract the caller context, create a thread out
     of it and add it to the thread list */
  gthread_t current = gthread_setup_current();
  gthread_dump(0,"gthread_init", current);
  scheduler_init(&scheduler, current);
  current->scheduler = scheduler;
  scheduler_enter();
  DBG_FUN("end");
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
  DBG_FUN("start");
  int result;
  gthread_t caller = gthread_current();
  event_t *event = event_alloc(caller, THREAD_CREATE);
  event->create.gthread  = gthread;
  event->create.callable = callable;
  event->create.data     = data;
  event_push(caller->scheduler, event);
  /* move to scheduler context */
  scheduler_enter();
  /* back to client context */
  result = event->result;
  event_free(event);
  DBG_FUN("end");
  return result;
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
  if (!gthread_is_valid(gthread))
    return ESRCH;
  scheduler_gthread_cancel(gthread);
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
  if (!gthread_is_valid(gthread))
    return ESRCH;
  if (!gthread_is_joinable(gthread))
    return EINVAL;
  scheduler_gthread_detach(gthread);
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
  gthread_t caller = gthread_current();
  event_t *event = event_alloc(caller, THREAD_EXIT);
  event->exit.retval  = retval;
  event_push(caller->scheduler, event);
  scheduler_enter();
  /* never returns */
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
  event_t *event   = NULL;
  gthread_t caller = gthread_current(); 
  if (!gthread_is_valid(gthread))
    return ESRCH;
  if (gthread_is_detached(gthread) || gthread_is_waited(gthread))
    return EINVAL;
  /* fix-me: should it check a more general wait loop (eg, A waits B, which waits C, whic tries to wait A)? */
  if (gthread_is_waiting(gthread, caller))
    return EDEADLK;
  /* current thread put in waiting mode until gthread has exited */
  if (!gthread_has_exited(gthread)){
    event = event_alloc(caller, THREAD_JOIN);
    event->join.gthread = gthread;
    event_push(caller->scheduler, event);
    scheduler_enter();
  } else {
    caller->wait_event = gthread->wait_event;
  }
  /* destroy gthread */
  scheduler_gthread_destroy(caller->scheduler, gthread);
  /* the returned event is an exit event */
  if (retval){
    *retval = caller->wait_event->exit.retval;
  }
  event_free(caller->wait_event);
  caller->wait_event = NULL;
  return 0;
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
  return gthread_current();
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
  scheduler_enter();
  return 0;
}
