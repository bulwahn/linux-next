.. SPDX-License-Identifier: GPL-2.0+

====================
Scheduler overview
====================

Linux kernel implements priority-based scheduling. More than one process are
allowed to run at any given time and each process is allowed to run as if it
were the only process on the system. The process scheduler coordinates which
process runs when. In that context, it has the following tasks:

  - share CPUs equally among all currently running processes.
  - pick appropriate process to run next if required, considering scheduling
    class/policy and process priorities.
  - balance processes between multiple CPUs in SMP systems.

The scheduler attempts to be responsive for I/O bound processes and efficient
for CPU bound processes. The scheduler uses different scheduling policies
for real time and normal processes based on their respective policy
enumerations. Scheduler adds support for each policy through scheduling class
implementations for each. The five scheduling classes which scheduler provides
are:

  - stop_sched_class:
    It is a per-cpu maximum priority CPU monopolization mechanism. It is
    exposed as a SCHED_FIFO task ('migration/X') with static priority of 99
    in the user space. This is done to make it compatible with user space and
    thus to avoid growing the ABI. It is used by one CPU to stop another
    in order to run a specific function, so it is only available on SMP
    systems. This class is used by the scheduler for task migration between
    CPUs.

  - dl_sched_class:
    Implements the SCHED_DEADLINE scheduling policy. It has static priority
    of -1 in kernel space. This policy schedules each task according to the
    task's deadline. The task with the earliest deadline will be served first.

  - rt_sched_class:
    Implements the SCHED_RR and SCHED_FIFO policies. Real time static
    priorities range from 1(low)..99 in the user space. (priority is inverted
    in kernel space). It is the only scheduling class that makes use of the
    static priority of the task. SCHED_FIFO is a simple scheduling algorithm
    without time slicing. A SCHED_FIFO thread runs until either it is blocked
    by an I/O request, it is preempted by a higher priority thread, or it
    calls sched_yield(). SCHED_RR is a simple enhancement of SCHED_FIFO where
    a thread is allowed to run only for a maximum time quantum.

  - fair_sched_class:
    Implements the SCHED_NORMAL SCHED_BATCH and SCHED_IDLE  policies. Static
    priority is always 0 in the user space. A dynamic priority based on
    'nice' value is used to schedule these tasks. This priority increases each
    time the the task  is scheduled to run but denied to run by scheduler.
    This ensures fair scheduling between these tasks. Nice value is an
    attribute which can be set by the user to influence scheduler to favour
    a particular task. SCHED_BATCH is similar to SCHED_NORMAL with the
    difference that the policy causes the scheduler to assume that the task
    is CPU-intensive. SCHED_IDLE policy also has static priority 0. Nice
    value has no effect on this policy. Weight mapping is not done, instead
    weight is set at a constant minimal weight WEIGHT_IDLEPRIO. Used to
    run tasks at extremely low priority.

  - idle_sched_class:
    Priority for idle task is irrelevant. This class is not related to
    SCHED_IDLE policy. Idle tasks run when there are no other runnable tasks
    on a CPU. The execute the idle loop which is responsible to put a CPU
    in one of its idle states.


Process Management
==================

Each process in the system is represented by struct task_struct. When a
process/thread is created, the kernel allocates a new task_struct for it.
The kernel then stores this task_struct in an RCU list. Macro next_task()
allows a process to obtain its next task and for_each_process() macro enables
traversal of the list.

Frequently used fields of the task struct are:

 - state: The running state of the task. The possible states are:

    - TASK_RUNNING: The task is currently running or in a run queue waiting
      to run.
    - TASK_INTERRUPTIBLE: The task is sleeping waiting for some event to occur.
      This task can be interrupted by signals. On waking up the task transitions
      to TASK_RUNNING.
    - TASK_UNINTERRUPTIBLE: Similar to TASK_INTERRUPTIBLE but does not wake
      up on signals. Needs an explicit wake-up call to be woken up. Contributes
      to loadavg.
    - __TASK_TRACED: Task is being traced by another task like a debugger.
    - __TASK_STOPPED: Task execution has stopped and not eligible to run.
      SIGSTOP, SIGTSTP etc causes this state.  The task can be continued by
      the signal SIGCONT.
    - TASK_PARKED: State to support kthread parking/unparking.
    - TASK_DEAD: If a task dies, then it sets TASK_DEAD in tsk->state and calls
      schedule one last time. The task will be never ran again.
    - TASK_WAKEKILL: It works like TASK_UNINTERRUPTIBLE with the bonus that it
      can respond to fatal signals.
    - TASK_WAKING: To handle concurrent waking of the same task for SMP.
      Indicates that someone is already waking the task.
    - TASK_NOLOAD: To be used along with TASK_UNINTERRUPTIBLE to indicate
      an idle task which does not contribute to loadavg.
    - TASK_NEW: Set during fork(), to guarantee that no one will run the task,
      a signal or any other wake event cannot wake it up and insert it on
      the runqueue.

 - exit_state : The exiting state of the task. The possible states are:

    - EXIT_ZOMBIE: The task is terminated and waiting for parent to collect
      the exit information of the task.
    - EXIT_DEAD: After collecting the exit information the task is put to
      this state and removed from the system.

 - static_prio: Used by the fair scheduling class to encode the nice level.
   It does not have any effect on the SCHED_DEADLINE, SCHED_FIFO or SCHED_RR
   policy tasks.

 - prio: The value of this field is used to:

    - distinguish scheduling classes.
    - in the RR/FIFO static priority scheduler.

 - normal_prio: Expected priority of a task. The value of static_prio
   and normal_prio are the same for non-real-time processes. For real time
   processes value of prio is used.

 - rt_priority: Field used to set priority of real time tasks. Not used by the
   rt_sched_class.

 - sched_class: Pointer to sched_class structure of the policy that the task
   belongs to.

 - sched_entity: Pointer to sched_entity CFS structure.

 - policy: scheduling policy of the task. See above.

 - nr_cpus_allowed: Hamming weight of the bitmask retrieved from cpumask pointer.

New tasks are created using the fork() system call which is described
at manpage `FORK(2)` or the clone system call described at manpage `CLONE(2)`.
Users can create threads within a process to achieve parallelism. Threads
share address space, open files and other resources of the process. Threads
are created like normal tasks with their unique task_struct, but clone()
is provided with flags that enable the sharing of resources such as address
space ::

	clone(CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND, 0);

The scheduler schedules task_structs so from scheduler perspective there is
no difference between threads and processes. Threads are created using
the system call pthread_create described at its manpage `PTHREAD_CREATE(3)`
POSIX threads creation is described at its manpage `PTHREADS(7)`

The Scheduler Entry Point
=========================

The main scheduler entry point is an architecture independent schedule()
function defined in kernel/sched/core.c. Its objective is to find a process in
the runqueue list and then assign the CPU to it. It is invoked, directly
or in a lazy (deferred) way from many different places in the kernel. A lazy
invocation does not call the function by its name, but gives the kernel a
hint by setting a flag TIF_NEED_RESCHED. The flag is a message to the kernel
that the scheduler should be invoked as soon as possible because another
process deserves to run. The flag should not be modified directly.

Following are some places that notify the kernel to schedule which can be
classified based on the type of operations:

  - Blocking operations: Suspends the current task and directly call into
    the scheduler to find something else to do. Some blocking operations are:

      - mutex_lock()
      - wait_event()
      - do_exit()
      - preempt_schedule_irq()

  - Co-operative or voluntary preemptions: Allows another task to run at that
    point subject to preemption model. Voluntary preemption model can be
    set through the kernel config option: CONFIG_PREEMPT_VOLUNTARY. The
    operations are:

      - cond_resched()
      - cond_resched_lock()
      - yield()
      - preempt_enable()

  - Involuntary preemption: Marks TIF_NEED_RESCHED and wait for action
    depending on preemption model. Involuntary preemption operations are:

      - scheduler_tick()
      - wake_up_process()

Calling functions mentioned above leads to a call to __schedule(). Note
that preemption must be disabled before it is called and enabled after
the call using preempt_disable and preempt_enable functions family.


The steps during invocation are:
--------------------------------
1. Disable preemption to avoid another task preempting the scheduling
   thread itself.
2. Retrieve the runqueue of current processor and its lock is obtained to
   allow only one thread to modify the runqueue at a time.
3. The state of the previously executed task when the schedule()
   was called is examined. If it is not runnable and has not been
   preempted in kernel mode, it is removed from the runqueue. If the
   previous task has non-blocked pending signals, its state is set to
   TASK_RUNNING and left in the runqueue.
4. Scheduler classes are iterated and the corresponding class hook to
   pick the next suitable task to be scheduled on the CPU is called.
   Since most tasks are handled by the sched_fair class, a shortcut to this
   class is implemented in the beginning of the function.
5. TIF_NEED_RESCHED and architecture specific need_resched flags are cleared.
6. If the scheduler class picks a different task from what was running
   before, a context switch is performed by calling context_switch().
   Internally, context_switch() switches to the new task's memory map and
   swaps the register state and stack. If scheduler class picked the same
   task as the previous task, no task switch is performed and the current
   task keeps running.
7. Balance callback list is processed. Each scheduling class can migrate tasks
   between CPUs to balance load. These load balancing operations are queued
   on a Balance callback list which get executed when balance_callback() is
   called.
8. The runqueue is unlocked and preemption is re-enabled. In case
   preemption was requested during the time in which it was disabled,
   schedule() is run again right away.

Scheduler State Transition
==========================

A very high level scheduler state transition flow with a few states can
be depicted as follows. ::

                                       *
                                       |
                                       | task
                                       | forks
                                       v
                        +------------------------------+
                        |           TASK_NEW           |
                        |        (Ready to run)        |
                        +------------------------------+
                                       |
                                       |
                                       v
                     +------------------------------------+
                     |            TASK_RUNNING            |
   +---------------> |           (Ready to run)           | <--+
   |                 +------------------------------------+    |
   |                   |                                       |
   |                   | schedule() calls context_switch()     | task is preempted
   |                   v                                       |
   |                 +------------------------------------+    |
   |                 |            TASK_RUNNING            |    |
   |                 |             (Running)              | ---+
   | event occurred  +------------------------------------+
   |                   |
   |                   | task needs to wait for event
   |                   v
   |                 +------------------------------------+
   |                 |         TASK_INTERRUPTIBLE         |
   |                 |        TASK_UNINTERRUPTIBLE        |
   +-----------------|           TASK_WAKEKILL            |
                     +------------------------------------+
                                       |
                                       | task exits via do_exit()
                                       v
                        +------------------------------+
                        |          TASK_DEAD           |
                        |         EXIT_ZOMBIE          |
                        +------------------------------+


Scheduler provides trace events tracing all major events of the scheduler.
The trace events are defined in ::

  include/trace/events/sched.h

Using these trace events it is possible to model the scheduler state transition
in an automata model. The following journal paper discusses such modeling:

Daniel B. de Oliveira, Rômulo S. de Oliveira, Tommaso Cucinotta, **A thread
synchronization model for the PREEMPT_RT Linux kernel**, *Journal of Systems
Architecture*, Volume 107, 2020, 101729, ISSN 1383-7621,
https://doi.org/10.1016/j.sysarc.2020.101729.

To model the scheduler efficiently the system was divided in to generators
and specifications. Some of the generators used were "need_resched",
"sleepable" and "runnable", "thread_context" and "scheduling context".
The specifications are the necessary and sufficient conditions to call
the scheduler. New trace events were added to specify the generators
and specifications. In case a kernel event referred to more than one
event, extra fields of the kernel event was used to distinguish between
automation events. The final model was generated from parallel composition
of all generators and specifications which composed of 34 events,
12 generators and 33 specifications. This resulted in 9017 states, and
20103 transitions.
