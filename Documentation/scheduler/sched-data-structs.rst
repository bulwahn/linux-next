.. SPDX-License-Identifier: GPL-2.0+

=========================
Scheduler Data Structures
=========================

The main parts of the Linux scheduler are:

Runqueue
~~~~~~~~

struct rq is the central data structure of process scheduling. It keeps track
of tasks that are in a runnable state assigned for a particular processor.
Each CPU has its own run queue and stored in a per CPU array::

    DEFINE_PER_CPU(struct rq, runqueues);

Access to the queue requires locking and lock acquire operations must be
ordered by ascending runqueue. Macros for accessing and locking the runqueue
are provided in::

    kernel/sched/sched.h

The runqueue contains scheduling class specific queues and several scheduling
statistics.

Scheduling entity
~~~~~~~~~~~~~~~~~
Scheduler uses scheduling entities which contain sufficient information to
actually accomplish the scheduling job of a task or a task-group. The
scheduling entity may be a group of tasks or a single task. Every task is
associated with a sched_entity structure. CFS adds support for nesting of
tasks and task groups. Each scheduling entity may be run from its parents
runqueue. The scheduler traverses the sched_entity hierarchy to pick the
next task to run on the CPU. The entity gets picked up from the cfs_rq on
which it is queued and its time slice is divided among all the tasks on its my_q.

Scheduler classes
~~~~~~~~~~~~~~~~~
It is an extensible hierarchy of scheduler modules. The modules encapsulate
scheduling policy details. They are called from the core code which is
independent. Scheduling classes are implemented through the sched_class
structure. The five scheduling classes are stop_sched_class, dl_sched_class,
rt_sched_class, fair_sched_class and idle_sched_class. The important methods
of scheduler class are:

  - sched_class::enqueue_task()
  - sched_class::dequeue_task()
    These functions are used to put and remove tasks from the runqueue
    respectively to change a property of a task. This is referred to as
    change pattern. Change is defined as the following sequence of calls:

      - dequeue_task()
      - put_prev_task()
      - change a property
      - enqueue_task()
      - set_next_task()

    The enqueue_task function takes the runqueue, the task which needs to
    be enqueued/dequeued and a bit mask of flags as parameters. The main
    purpose of the flags is to describe why the enqueue or dequeue is being
    called. The different flags used are described in ::

        kernel/sched/sched.h

    Some places where the enqueue_task and dequeue_task are called for
    changing task properties are:

      - When migrating a task from one CPU's runqueue to another.
      - When changing a tasks CPU affinity.
      - When changing the priority of a task.
      - When changing the nice value of the task.
      - When changing the scheduling policy and/or RT priority of a thread.

  - sched_class::pick_next_task()
    Called by the scheduler to pick the next best task to run. The scheduler
    iterates through the corresponding functions of the scheduler classes
    in priority order to pick up the next best task to run. Since tasks
    belonging to the idle class and fair class are frequent, the scheduler
    optimizes the picking of next task to call the pick_next_task_fair()
    if the previous task was of the similar scheduling class.

  - sched_class::put_prev_task()
    Called by the scheduler when a running task is being taken off a CPU.
    The behavior of this function depends on individual scheduling classes.
    In CFS class this function is used to put the currently running task back
    into the CFS RB tree. When a task is running it is dequeued from the tree.
    This is to prevent redundant enqueue's and dequeue's for updating its
    vruntime. vruntime of tasks on the tree needs to be updated by update_curr()
    to keep the tree in sync. In SCHED_DEADLINE and RT classes additional tree
    is maintained to push tasks from the current CPU to another CPU where the
    task can preempt and start executing. Task will be added to this queue
    if it is present on the scheduling class rq and the task has affinity
    to more than one CPU. set_next_task() is called on the task returned from
    this function.

  - sched_class::set_next_task()
    Pairs with the put_prev_task(), this function is called when the next
    task is set to run on the CPU. This function is called in all the places
    where put_prev_task is called to complete the 'change pattern'. In case
    of CFS scheduling class, it will set current scheduling entity to the
    picked task and accounts bandwidth usage on the cfs_rq. In addition it
    will also remove the current entity from the CFS runqueue for the vruntime
    update optimization, opposite to what was done in put_prev_task.
    For the SCHED_DEADLINE and RT classes it will remove the task from the
    tree of pushable tasks trigger the balance callback to push another task
    which is non running on the current CPU for execution on another CPU.

      - dequeue the picked task from the tree of pushable tasks.
      - update the load average in case the previous task belonged to another
        class.
      - queues the function to push tasks from current runqueue to other CPUs
        which can preempt and start execution. Balance callback list is used.

  - sched_class::task_tick()
    Called from scheduler_tick(), hrtick() and sched_tick_remote() to update
    the current task statistics and load averages. Also restarting the high
    resolution tick timer is done if high resolution timers are enabled.
    scheduler_tick() runs at 1/HZ and is called from the timer interrupt
    handler of the Kernel internal timers.
    hrtick() is called from high resolution timers to deliver an accurate
    preemption tick as the regular scheduler tick that runs at 1/HZ can be
    too coarse when nice levels are used.
    sched_tick_remote() gets called by the offloaded residual 1Hz scheduler
    tick. In order to reduce interruptions to bare metal tasks, it is possible
    to outsource these scheduler ticks to the global workqueue so that a
    housekeeping CPU handles those remotely.

  - sched_class::select_task_rq()
    Called by scheduler to get the CPU to assign a task to and migrating
    tasks between CPUs. Flags describe the reason the function was called.
    Called by try_to_wake_up() with SD_BALANCE_WAKE flag which wakes up a
    sleeping task.
    Called by wake_up_new_task() with SD_BALANCE_FORK flag which wakes up a
    newly forked task.
    Called by sched_exec() with SD_BALANCE_EXEC which is called from execv
    syscall.
    SCHED_DEADLINE class decides the CPU on which the task should be woken
    up based on the deadline. RT class decides based on the RT priority. Fair
    scheduling class balances load by selecting the idlest CPU in the
    idlest group, or under certain conditions an idle sibling CPU if the
    domain has SD_WAKE_AFFINE set.

  - sched_class::balance()
    Called by pick_next_task() from scheduler to enable scheduling classes
    to pull tasks from runqueues of other CPUs for balancing task execution
    between the CPUs.

  - sched_class::task_fork()
    Called from sched_fork() of scheduler which assigns a task to a CPU.
    Fair scheduling class updates runqueue clock, runtime statistics and
    vruntime for the scheduling entity.

  - sched_class::yield_task()
    Called from SYSCALL sched_yield to yield the CPU to other tasks.
    SCHED_DEADLINE class forces the runtime of the task to zero using a special
    flag and dequeues the task from its trees. RT class requeues the task
    entities to the end of the run list. Fair scheduling class implements
    the buddy mechanism. This allows skipping onto the next highest priority
    scheduling entity at every level in the CFS tree, unless doing so would
    introduce gross unfairness in CPU time distribution.

  - sched_class::check_preempt_curr()
    Check whether the task that woke up should preempt the currently
    running task. Called by scheduler

      - when moving queued task to new runqueue
      - ttwu()
      - when waking up newly created task for the first time.

    SCHED_DEADLINE class compares the deadlines of the tasks and calls
    scheduler function resched_curr() if the preemption is needed. In case
    the deadlines are equal, migratability of the tasks is used a criteria
    for preemption.
    RT class behaves the same except it uses RT priority for comparison.
    Fair class sets the buddy hints before calling resched_curr() to preempt.
