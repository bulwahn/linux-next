.. SPDX-License-Identifier: GPL-2.0+

=============
CFS Overview
=============

Linux 2.6.23 introduced a modular scheduler core and a Completely Fair
Scheduler (CFS) implemented as a scheduling module. A brief overview of the
CFS design is provided in :doc:`sched-design-CFS`

In addition there have been many improvements to the CFS, a few of which are

Tracking available capacity
---------------------------
Scale CPU capacity mechanism for CFS so it knows how much CPU capacity is left
for its use after higher priority sched classes (RT, DL), IRQs and
'Thermal Pressure' have reduced the 'original' CPU capacity.
Thermal pressure on a CPU means the maximum possible capacity is
unavailable due to thermal events.

NUMA balancing
--------------
Attempt to migrate tasks to the NUMA Node where the frequently accessed memory
pages belongs. The scheduler gets information about memory placement through the
paging mechanism. Scheduler periodically scans the virtual memory of the tasks
and make them inaccessible by changing the memory protection. The flag
MM_CP_PROT_NUMA indicates this purpose. When the task attempts to access
the memory again a page fault occurs. Scheduler traps the fault and increments
the counters in a task specific array corresponding to the NUMA node id.
There array is divided in to four regions: faults_memory, faults_cpu,
faults_memory_buffer and faults_cpu_buffer, where faults_memory is the
exponential decaying average of faults on a per-node basis. The 'preferred
node' is found by looping through the array and finding the node with the
highest number of faults. Migration to the preferred node is done periodically
by either swapping two tasks tasks between their respective CPUs or
just moving a task to its preferred node CPU. It the migration or move fails
it will be retried.

Energy Aware Scheduling
-----------------------
For asymmetric CPU capacity topologies, an Energy Model is used to figure out
which of the CPU candidates is the most energy-efficient. Capacity is the
amount of work which a CPU can perform at its highest frequency which is
calculated by the Per-Entity Load Tracking (PELT) mechanism.
EAS is described at :doc:`sched-energy`

Capacity Aware Scheduling
--------------------------
Migrate a task to a CPU which meets its compute demand. In asymmetric CPU
capacity topologies CFS scheduler frequently updates the 'Misfit' status of
tasks and migrate them to CPU's of higher capacity. Also during wakeups the
a CPU with sufficient capacity is found for executing the task. CAS is
described at :doc:`sched-capacity`






