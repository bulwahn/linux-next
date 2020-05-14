.. SPDX-License-Identifier: GPL-2.0+

=============
CFS Overview
=============

Linux 2.6.23 introduced a modular scheduler core and a Completely Fair
Scheduler (CFS) implemented as a scheduling module. A brief overview of the
CFS design is provided in :doc:`sched-design-CFS`

In addition there have been many improvements to the CFS, a few of which are

**Thermal Pressure**:
Scale CPU capacity mechanism for CFS so it knows how much CPU capacity is left
for its use after higher priority sched classes (RT, DL), IRQs and
'Thermal Pressure' have reduced the 'original' CPU capacity.
Thermal pressure on a CPU means the maximum possible capacity is
unavailable due to thermal events.

** Optimizations to NUMA balancing**:
When gathering NUMA statistics, information about whether a core is Idle
is also cached. In case of an imbalance, instead of doing a second scan of
the node runqueues, the idle core is used as the migration target. When
doing so multiple tasks can attempt to select an idle CPU but fail, because
a NUMA balance is active on that CPU. In this case an alternative idle CPU
scanned. Another optimization is to terminate the search for swap candidate
when a reasonable one is found instead of searching all the CPUs on the
target domain.

**Asymmetric CPU capacity wakeup scan**:
Previous assumption that CPU capacities within an SD_SHARE_PKG_RESOURCES
domain (sd_llc) are homogeneous didn't hold for newer generations of big.LITTLE
systems (DynamIQ) which can accommodate CPUs of different compute capacity
within a single LLC domain. A new idle sibling helper function was added
which took CPU capacity into account. The policy is to pick the first idle
CPU which is big enough for the task (task_util * margin < cpu_capacity).
If no idle CPU is big enough, the idle CPU with the highest capacity is
picked.

**Optimized idle core selection**:
Skipped looping through all the threads of a core to evaluate if the
core is idle or not. If a thread of a core is not idle, evaluation of
other threads of the core can be skipped.

**Load balance aggressively for SCHED_IDLE CPUs**:
Newly-woken task is preferred to be  enqueued on a SCHED_IDLE CPU instead
of other busy or idle CPUs. Also load balancer is made to migrate tasks more
aggressively to a SCHED_IDLE CPU. Fair scheduler now does the next
load balance soon after the last non-SCHED_IDLE task is dequeued from a
runqueue, i.e. making the CPU SCHED_IDLE. Also the the busy_factor
used with the balance interval to prevent frequent load balancing
is ignored for such CPU's.

**Load balancing algorithm Reworked**:
Some heuristics in the load balancing algorithm became meaningless because
of the rework of the scheduler's metrics like the introduction of PELT.
Those heuristics were removed. The new load balancing algorithm also fixes
several pending wrong tasks placement

 * the 1 task per CPU case with asymmetric system
 * the case of CFS task preempted by other class
 * the case of tasks not evenly spread on groups with spare capacity

Also the load balance decisions have been consolidated in the 3 separate
functions.
* update_sd_pick_busiest() select the busiest sched_group.
* find_busiest_group() checks if there is an imbalance between local and
busiest group.
* calculate_imbalance() decides what have to be moved.

**Energy-aware wake-ups speeded up**:
Algorithmic complexity of the EAS was reduced from O(n^2) to O(n).
Previous algorithm resulted in prohibitively high wake-up latencies on
systems with complex energy models, such as systems with per-CPU DVFS.
The EAS wake-up path was re-factored to compute the energy 'delta' on a
per-performance domain basis, rather than the whole system.

**Selection of an energy-efficient CPU on task wake-up**:
An Energy efficient CPU is found by estimating the impact on system-level
active energy resulting from the placement of the task on the CPU with the
highest spare capacity in each performance domain. Energy Model (EM) is
used for this. This strategy spreads tasks in a performance domain and avoids overly
aggressive task packing. The best CPU energy-wise is then selected if it
saves a large enough amount of energy with respect to prev_cpu.

**Consider misfit tasks when load-balancing**:
A task which ends up on a CPU which doesn't suit its compute demand is
identified as a misfit task in asymmetric CPU capacity systems. These
'misfit' tasks are migrated to CPUs with higher compute capacity to ensure
better throughput. A new group_type: group_misfit_task is added and indicates this
scenario. Tweaks to the load-balance code are done to make the migrations
happen. Misfit balancing is done between a source group of lower per-CPU
capacity and destination group of higher compute capacity. Otherwise, misfit
balancing is ignored.


**Make schedstats a runtime tunable that is disabled by default**:
A kernel command-line and sysctl tunable was added to enable or disable
schedstats on demand (when it's built in). It is disabled by default.
The benefits are dependent on how scheduler-intensive the workload is.


