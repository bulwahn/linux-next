.. SPDX-License-Identifier: GPL-2.0+

==========================
Process context switching
==========================

Context Switching
-----------------

Context switching, the switching from a running task to another,
is done by the context_switch() function defined in kernel/sched.c.
It is called by __schedule() when a new process has been selected to run.
The execution flow is as follows:

* prepare_task_switch() performs necessary kernel preparations for the
  context switch and then calls prepare_arch_switch() for architecture
  specific context switch preparation. This call must be paired with a
  subsequent finish_task_switch() after the context switch. The various
  steps are:

  - Prepare kcov for context switch. Context switch does switch_mm() to the
    next task's mm, then switch_to() that new task. This means vmalloc'd
    regions which had previously been faulted in can transiently disappear in
    the context of the prev task. Functions instrumented by KCOV may try to
    access a vmalloc'd kcov_area during this window, and result in a recursive
    fault. This is avoided by setting a new flag: KCOV_IN_CTXSW in kcov_mode
    prior to switching the mm, and cleared once the new task is live.
  - Update sched_info statistics for both the prev and next tasks.
  - Handle perf subsystem context switch from previous task to next.
    The various steps are:

    * Remove perf events for the task being context-switched out.
    * Stop each perf event and update the event value in event->count.
    * Call the context switch callback for PMU with flag indicating
      schedule out.
    * Create a PERF_RECORD_MISC_SWITCH_OUT perf event.
    * Context switch the perf event contexts between the current and next tasks.
    * Schedule out current cgroup events if cgroup perf events exist on the
      CPU.

  - Set TIF_NOTIFY_RESUME flag on the current thread for the Restartable
    sequence mechanism. Restartable sequences allow user-space to perform
    update operations on per-cpu data without requiring heavy-weight atomic
    operations.
  - Fire preempt notifiers. A task can request the scheduler to notify it
    whenever it is preempted or scheduled back in. This allows the task to
    swap any special-purpose registers like the FPU or Intel's VT registers.
  - Claim the next task as running to prevent load balancing run on it.

* arch_start_context_switch() batches the reload of page tables and other
  process state with the actual context switch code for paravirtualized
  guests.

* Transfer the real and anonymous address spaces between the switching tasks.
  Four possible transfer types are:

  * kernel task switching to another kernel task
  * user task switching to a kernel task
  * kernel task switching to user task
  * user task switching to user task

  For a kernel task switching to kernel task enter_lazy_tlb() is called
  which is an architecture specific implementation to handle a context
  without an mm. Architectures implement lazy tricks to minimize TLB
  flushes here. The active address space from the previous task is
  borrowed (transferred) to the next task.

  For a user task switching to kernel task it will have a real address
  space and so its anonymous users counter is incremented. This makes
  sure that the address space will not get freed even after the previous
  task exits.

  For a user task switching to user task the architecture specific
  switch_mm_irqs_off() or switch_mm() functions are called.  The main
  functionality of these calls is to switch the address space between
  the user space processes.  This includes switching the page table pointers
  either via retrieved valid ASID for the process or page mapping in the TLB.

  For a kernel task switching to a user task, switch_mm_irqs_off()
  replaces the address space of prev kernel task with the next from the user
  task. Same as for exiting process in this case, the context_switch()
  function saves the pointer to the memory descriptor used by prev in the
  runqueue’s prev_mm field and resets prev task active address space.

* prepare_lock_switch() releases lockdep of the runqueue lock to handle
  the special case of the scheduler context switch where the runqueue lock
  will be released by the next task.

* Architecture specific implementation of switch_to() switches the
  register state and the stack. This involves saving and restoring stack
  information and the processor registers and any other
  architecture-specific state that must be managed and restored on a
  per-process basis.

* finish_task_switch() performs the final steps of the context switch:

  - Emit a warning if the preempt count is corrupted and set the preempt count
    to FORK_PREEMPT_COUNT.
  - Reset the pointer to the memory descriptor used by prev which was set in
    context_switch().
  - Store the state of the previous task to handle the possibility of a DEAD
    task.
  - Do virtual CPU time accounting for the previous task.
  - Handle perf subsystem context switch from previous task to current:

    - Add perf events for the current task.
    - Schedule in current cgroup events if cgroup perf events exist on the
      CPU.
    - Context switch the perf event contexts between the prev and current
      tasks.
    - Clear the PERF_RECORD_MISC_SWITCH_OUT perf event.
    - Call the context switch callback for PMU with flag indicating
      schedule in.
  - Free the task for load balancing run on it.
  - Unlock the rq lock.
  - Clear the KCOV_IN_CTXSW in kcov_mode which was set in prepare_task_switch
    now that the new task is live.
  - Fire preempt notifiers to notify about task scheduled back in.
  - If the prev task state indicated that it was dead, the corresponding
    scheduler class task_dead hook is called. Function-return probe
    instances associated with the task are removed and put back on the
    free list. Stack for the task is freed and drop the RCU references.
  - Evaluate the need for No idle tick due to the context switch and do the
    idle tick if needed.

