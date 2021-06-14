#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

#include "threads/fixed-point.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;
static struct list blocked_threads;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */
static uint32_t load_avg;


/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */
//start our code
#define DONATION_DEPTH 8;
//end our code

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  // MY CODE
  load_avg = 0;
  // --------------------
  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init(&blocked_threads);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */

void threads_update_statistics(bool one_second)
{
  if(thread_current() != idle_thread)
    thread_current()->recent_cpu = ADD_F_I(thread_current()->recent_cpu, 1);
  
  if(one_second)
  {
    uint32_t a = MUL_F_I(load_avg, 59);
    a = DIV_F_I(a, 60);
    
    uint32_t b = I_TO_F(list_size(&ready_list));
    b = DIV_F_I(b, 60);

    load_avg = ADD_F_F(a, b);
    
    struct list_elem * cur_iter = list_head(&all_list);
    struct thread *cur;
    while((cur_iter = list_next(cur_iter)) != list_tail(&all_list))
    {
      cur = list_entry(cur_iter, struct thread, elem);
      uint32_t recent_cpu = cur->recent_cpu;
      uint32_t load_avg_double = MUL_F_I(load_avg,2);
      cur->recent_cpu = ADD_F_I(MUL_F_F(DIV_F_F(load_avg_double, ADD_F_I(load_avg_double, 1)), recent_cpu), cur->nice_value);
    }

  }
}

void
thread_tick (void) 
{
  // MY CODE
  
  // ----------------------
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */

  // our code
  t->nice_value = thread_current()->nice_value;
  // ----------

  thread_unblock (t);

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  //list_push_back (&ready_list, &t->elem);
  list_insert_ordered (&ready_list, &t->elem, threads_sort_by_priority, NULL);
  t->status = THREAD_READY;
//start our code  
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread)
    list_insert_ordered(&ready_list, &cur->elem, threads_sort_by_priority, NULL); // TODO: check insert with prority ordered
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}
//start our code
int thread_get_donated_priority(const struct thread* a){
  return a->donated_priority;
} 
void thread_set_donated_priority(struct thread* a,int new_donated){
  a->donated_priority=new_donated;
} 
struct list thread_get_acquired_locks(const struct thread* a){
  return a->acquired_locks;
}
//to be called in acquire lock (inside sema down)
void thread_add_to_accquired_locks(struct lock* l){
  struct thread *cur = thread_current();
  list_insert_ordered(&cur->acquired_locks, &l->lock_elem, acquired_lock_sort_by_priority,NULL);//add new acquired lock in the list of the thread
  cur->donated_priority = MAX(cur->donated_priority, l->highest_donated_priority);

  // TODO: Thread switch
  //call upfdate donation

}
//to be called in release lock
void thread_remove_from_accquired_locks(struct lock* l){
  struct thread *cur = thread_current();
  ASSERT(!list_empty(&cur->acquired_locks));
  list_remove(&l->lock_elem);

   //call update donation fn 
  if(list_empty(&cur->acquired_locks))
    cur->donated_priority = cur->priority;
  else
  {
    cur->donated_priority = list_entry(list_begin(&cur->acquired_locks), struct lock, lock_elem)->highest_donated_priority;
  }
}
/*modified thread is the thread which have one of it's locks case 1 added or case 2  removed so we update the donations for both it's acquired lock 
                    case 2 remove lock:             modified thread
                                                   /         |      \
  current acquired locks :                     A           B         C       so we see which has the highest priority and give it  to modified ,
   in the removed lock itself                  removed lock
                                               /    |     \
                                              t1    t2    t3 
                       let's assume that we had a running thread which had a ccertain sight through the waiting priorities according to DONATION_DEPTH so we itterate 
                       in the new waiting list untill we reach the depth and test threads priorities aganist the lock highest priority  and update if it is higher

        case 1: add thread to lock as waiting       lock A 
                                                  /    |     \                                   
                                                t1     t2    new added thread(modified thread) 
                                            holds it  waits         waits for the lock after t2
                            so in this case we check the new added thread (modidied thread) to see if it has a higher priority of the highest priority of the lock 
                            if it's smaller so do nothing 
                            else if (it's higher than &&it's depth in the waiting <= DONATION_DEPTH) then update the highest priority variable which exist in the lock A
                             and for each thread in the waiting list update it's donated priority with the lock's priority


*/
//end our code

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  thread_current ()->priority = new_priority;
  if(!list_empty(&ready_list) && 
  list_entry(list_begin(&ready_list), struct thread, elem)->priority > new_priority)
    thread_yield();
}

// WARNING: this only works on priority schedulers
bool is_current_greatest_priority(void)
{
  if(!list_empty(&ready_list) && 
  list_entry(list_begin(&ready_list), struct thread, elem)->priority > thread_current()->priority)
    return false;
  return true;
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  if(thread_mlfqs == PRIORITY_SCHEDULER)
    return thread_current()->donated_priority;
  else
    return thread_current()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. */
  thread_current() ->nice_value = nice;
  
  thread_current() ->priority = PRI_MAX - (thread_current()->recent_cpu / 4) - (nice * 2);

  // TODO: Thread yield stuff 

  if(!list_empty(&ready_list) && 
  list_entry(list_begin(&ready_list), struct thread, elem)->priority > thread_current()->priority)
    thread_yield();


}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  return thread_current()->nice_value;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return F_TO_I_DOWN(load_avg) * 100;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  return F_TO_I_DOWN(thread_current()->recent_cpu) * 100;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;

  t->priority = priority;
  t->donated_priority = priority;


  t->magic = THREAD_MAGIC;
  t->nice_value = 0;            // TODO: Calculate priority at initialization
  t->recent_cpu = 0;
  //start our code
  list_init(&t->acquired_locks);
  t->waiting_on_lock = NULL;
  //end our code
  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);


//start our code 
//return true if awake up less than b wake up
bool threads_sort_by_wakeup_time_comp(const struct list_elem *a_elem, const struct list_elem *b_elem, void *aux UNUSED)
{
  ASSERT(a_elem != NULL && b_elem != NULL);
  struct thread *a = list_entry(a_elem, struct thread, elem);
  struct thread *b = list_entry(b_elem, struct thread, elem);
  if(a->wake_up_after_tick == b->wake_up_after_tick)
    return threads_sort_by_priority(a_elem, b_elem, NULL);
  return a->wake_up_after_tick < b->wake_up_after_tick;
}
//  return true if a _priority > b_priority 
bool threads_sort_by_priority(const struct list_elem *a_elem, const struct list_elem *b_elem, void *aux UNUSED)
{
  ASSERT(a_elem != NULL && b_elem != NULL);
  struct thread *a = list_entry(a_elem, struct thread, elem);
  struct thread *b = list_entry(b_elem, struct thread, elem);
  if(thread_mlfqs == PRIORITY_SCHEDULER)
    return (a->priority == b->priority)? true: a->priority > b->priority;
  else
    return (a->donated_priority == b->donated_priority)? true: a->donated_priority > b->donated_priority;
}
//return true if lock a highest priority is less than lock b highest priority as defined in list less fn
bool acquired_lock_sort_by_priority(const struct list_elem *a_elem, const struct list_elem *b_elem, void *aux UNUSED)
{
  ASSERT(a_elem != NULL && b_elem != NULL);
  struct lock *a = list_entry(a_elem, struct lock, lock_elem);
  struct lock *b = list_entry(b_elem, struct lock, lock_elem);
  return a->highest_donated_priority > b->highest_donated_priority;
}


void threads_wakeup_blocked(int64_t ticks)
{
  struct thread *it;
  int max_priority = PRI_MIN;
  while(!list_empty(&blocked_threads)
  && (it = list_entry(list_begin(&blocked_threads), struct thread, elem) )
  && it->wake_up_after_tick < ticks)
  {
    struct thread * t = list_entry(list_pop_front(&blocked_threads), struct thread, elem);
    thread_unblock(t);
    max_priority = MAX(max_priority, t->priority);
    printf("[********] max_priority = %d\n", max_priority);
  }
  // ASSERT(max_priority == 0);

  if(max_priority > thread_current()->priority)
    intr_yield_on_return();
}


// ana hrai7ly habetin
void thread_sleep(int64_t after)
{
  enum intr_level old_level = intr_disable ();
  struct thread *t = thread_current();
  t->wake_up_after_tick = after;

  list_insert_ordered(&blocked_threads, &t->elem, threads_sort_by_wakeup_time_comp, NULL);

  thread_block();
  intr_set_level (old_level);

}


void is_time_sliced_ended()
{
  if(thread_ticks == TIME_SLICE)
    intr_yield_on_return();
}

//<------------------------------------------some comments for requirments and visualization------------------------------------------------------------->
/*  
                    1.priority scheduling TODO(check if we need an if statment in timer interrupt to see in which mode we run (priority or advanced))
                    check for max priority in ready queue and if there exist a priority such that >current_thread (donated) priority  --> TODO(need to add donated
                    priority var in thread ) , we call thread_yield()for current

                    2.priority Donation  where to apply :in priority scheduler (when mflq==false) and around locks
                                                                 /                                              \
                                                                /                                                \
                                                               /                                                  \
                                                                                                          when we have a thread with low priority 
                                                      in priority scheduling                            holding a mutex lock  which there is a higher 
                                                      we check aganist donated                          priority waits (blocked)for that same lock with a defined 
                                                                                                        depth=8 (as documintaion) so that higher priority 
                                                                                                donates it's priority for the running one which hold the mutex

visualization:

(T1 ,p=100)-------waits for------>(T2,p=100)-------waits for ------>(t3,p=5)(running thread) 2 locks  ,100 
                                                                    D.P=[60]
                                                                 *
                                              (100>60)          /
                                                            waits for t3              (so what happen that D.p is  updated and take the higher P so t3 finish and then     
                                                            /                           t2,t1,t4. assuming that the highest proirities are 60,100)                         
                                                    (t4,p=60)

implementation approach: donate the highest priority of the waiting list on a lock to the lock itself (lock priority) and whatever thread acquire the lock it's D.P will be assigned 
                        to that prioty, once it release the lock we call a fn to updates it's D.P : the fn for the given 
                        
                        case 1:thread handling 
                         it sees what are the locks which are acquired by the thread
                        and compare thier highest priority and assign the D.P to the highest , if there is no acquired locks the D.P set back to it's original priority
                        
                        for case 2 Lock handling : 
                        if it is (2.1) accquire check  if the acquring thread priority is higher than highest priority var of the lock , if true update the highest 
                        lock var ,if false update the D.P with the highest lock , 
                        
                        (2.2)if relese lock then check for the current highest in the waiting list and update the highest in the lock if(waiting empty) the highest set to zero 

                        this is called when a thread acquire a lock or release it ,,the fn takes three paramater 1.thread pointer 2. the lock 3. the state (acquire or release)

                        TODO: implement the fn ,and add a list with acquired locks to  thread struct ,modify lock struct and add a new var highest priority 

need to know :where to call add_acquire locks and remove_acquired locks, what is the initial val of donated priority, 2. check list_insert_order sorting criteria : sort by priority now is > shouldn't be (<)? as 

typedef bool list_less_func (const struct list_elem *a,
                             const struct list_elem *b,
                             void *aux);
                             returns true if a <b
*/