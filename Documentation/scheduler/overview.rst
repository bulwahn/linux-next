.. SPDX-License-Identifier: GPL-2.0+

====================
Scheduler overview
====================

Linux kernel implements priority-based scheduling. More than one process are
allowed to run at any given time and each process is allowed to run as if it
were the only process on the system. The process scheduler coordinates which
process runs when. In that context, it has the following tasks:

* share CPU cores equally among all currently running processes.
* pick appropriate process to run next if required, considering scheduling
  class/policy and process priorities.
* balance processes between multiple cores in SMP systems.

The scheduler attempts to be responsive for I/O bound processes and efficient
for CPU bound processes. The scheduler also applies different scheduling
policies for real time and normal processes based on their respective
priorities. Higher priorities in the kernel have a numerical smaller
value. Real time priorities range from 1 (highest) – 99 whereas normal
priorities range from 100 – 139 (lowest). Scheduler implements many scheduling
classes which encapsulate a particular scheduling policy. Each scheduling
policy implements scheduler handling of tasks that belong to a particular
priority. SCHED_FIFO and SCHED_RR policies handle real time priorities tasks
while SCHED_NORMAL and SCHED_BATCH policies handle tasks with normal priorities.
SCHED_IDLE is also a normal scheduling policy when means its priority can
be set between 100 – 139 range too but they are treated as priority 139.
Their priority doesn't matter since they get minimal weight WEIGHT_IDLEPRI=3.
SCHED_DEADLINE policy tasks have negative priorities, reflecting
the fact that any of them has higher priority than RT and NORMAL/BATCH tasks.

And then there are the maintenance scheduler classes: idle sched class and
stop sched class. Idle class doesn't manage any user tasks and so doesn't
implement a policy. Its idle tasks 'swapper/X' has priority 120 and and aren't
visible to user space. Idle tasks are responsible for by putting the CPUs
into deep idle states when there is no work to do.
Stop sched class is also used internally by the kernel doesn't implement any
scheduling policy. Stopper tasks 'migration/X' disguise as as a SCHED_FIFO
task with priority 139. Stopper tasks are a mechanism to force a CPU to stop
running everything else and perform a specific task. As this is the
highest-priority class, it can preempt everything else and nothing ever
preempts it. It is used by one CPU to stop another in order to run a specific
function, so it is only available on SMP systems. This class is used by the
kernel for task migration.


Process Management
==================

Each process in the system is represented by struct task_struct. When a
process/thread is created, the kernel allocates a new task_struct for it.
The kernel then stores this task_struct in an RCU list. Macro next_task()
allows a process to obtain its next task and for_each_process() macro enables
traversal of the list.

Frequently used fields of the task struct are:

*state:* The running state of the task. The possible states are:

* TASK_RUNNING: The task is currently running or in a run queue waiting
  to run.
* TASK_INTERRUPTIBLE: The task is sleeping waiting for some event to occur.
  This task can be interrupted by signals. On waking up the task transitions
  to TASK_RUNNING.
* TASK_UNINTERRUPTIBLE: Similar to TASK_INTERRUPTIBLE but does not wake
  up on signals. Needs an explicit wake-up call to be woken up. Contributes
  to loadavg.
* __TASK_TRACED: Task is being traced by another task like a debugger.
* __TASK_STOPPED: Task execution has stopped and not eligible to run.
  SIGSTOP, SIGTSTP etc causes this state.  The task can be continued by
  the signal SIGCONT.
* TASK_PARKED: State to support kthread parking/unparking.
* TASK_DEAD: If a task dies, then it sets TASK_DEAD in tsk->state and calls
  schedule one last time. The schedule call will never return.
* TASK_WAKEKILL: It works like TASK_UNINTERRUPTIBLE with the bonus that it
  can respond to fatal signals.
* TASK_WAKING: To handle concurrent waking of the same task for SMP.
  Indicates that someone is already waking the task.
* TASK_NOLOAD: To be used along with TASK_UNINTERRUPTIBLE to indicate
  an idle task which does not contribute to loadavg.
* TASK_NEW: Set during fork(), to guarantee that no one will run the task,
  a signal or any other wake event cannot wake it up and insert it on
  the runqueue.

*exit_state* : The exiting state of the task. The possible states are:

* EXIT_ZOMBIE: The task is terminated and waiting for parent to collect
  the exit information of the task.
* EXIT_DEAD: After collecting the exit information the task is put to
  this state and removed from the system.

*static_prio:* Nice value of a task. The value of this field does
 not change.  Value ranges from -20 to 19. This value is mapped to nice
 value and used in the scheduler.

*prio:* Dynamic priority of a task. Previously a function of static
 priority and tasks interactivity. Value not used by CFS scheduler but used
 by the RT scheduler. Might be boosted by interactivity modifiers. Changes
 upon fork, setprio syscalls, and whenever the interactivity estimator
 recalculates.

*normal_prio:* Expected priority of a task. The value of static_prio
 and normal_prio are the same for non-real-time processes. For real time
 processes value of prio is used.

*rt_priority:* Field used by real time tasks. Real time tasks are
 prioritized based on this value.

*sched_class:* Pointer to sched_class CFS structure.

*sched_entity:* Pointer to sched_entity CFS structure.

*policy:* Value for scheduling policy. The possible values are:

* SCHED_NORMAL: Regular tasks use this policy.
* SCHED_BATCH: Tasks which need to run longer without preemption
  use this policy. Suitable for batch jobs.
* SCHED_IDLE: Policy used by background tasks.
* SCHED_FIFO & SCHED_RR: These policies for real time tasks. Handled by
  real time scheduler.
* SCHED_DEADLINE: Tasks which are activated on a periodic or sporadic fashion
  use this policy. This policy implements the Earliest Deadline First (EDF)
  scheduling algorithm. This policy is explained in detail in the
  :doc:`sched-deadline` documentation.

*nr_cpus_allowed:* Bit field containing tasks affinity towards a set of
 CPU cores.  Set using sched_setaffinity() system call.

New processes are created using the fork() system call which is described
at manpage :manpage:`FORK(2)` or the clone system call described at
:manpage:`CLONE(2)`.
Users can create threads within a process to achieve parallelism. Threads
share address space, open files and other resources of the process. Threads
are created like normal tasks with their unique task_struct, but clone()
is provided with flags that enable the sharing of resources such as address
space ::

	clone(CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND, 0);

The scheduler schedules task_structs so from scheduler perspective there is
no difference between threads and processes. Threads are created using
the system call pthread_create described at :manpage:`PTHREAD_CREATE(3)`
POSIX threads creation is described at :manpage:`PTHREADS(7)`

The Scheduler Entry Point
=========================

The main scheduler entry point is an architecture independent schedule()
function defined in kernel/sched/core.c. Its objective is to find a process in
the runqueue list and then assign the CPU to it. It is invoked, directly
or in a lazy (deferred) way from many different places in the kernel. A lazy
invocation does not call the function by its name, but gives the kernel a
hint by setting a flag TIF_NEED_RESCHED. The flag is a message to the kernel
that the scheduler should be invoked as soon as possible because another
process deserves to run.

Following are some places that notify the kernel to schedule:

* scheduler_tick()

* Running task goes to sleep state : Right before a task goes to sleep,
  schedule() will be called to pick the next task to run and the change
  its state to either TASK_INTERRUPTIBLE or TASK_UNINTERRUPTIBLE. For
  instance, prepare_to_wait() is one of the functions that makes the
  task go to the sleep state.

* try_to_wake_up()

* yield()

* wait_event()

* cond_resched() : It gives the scheduler a chance to run a higher-priority
  process.

* cond_resched_lock() : If a reschedule is pending, drop the given lock,
  call schedule, and on return reacquire the lock.

* do_task_dead()

* preempt_schedule() : The function checks whether local interrupts are
  enabled and the preempt_count field of current is zero; if both
  conditions are true, it invokes schedule() to select another process
  to run.

* preempt_schedule_irq()

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
