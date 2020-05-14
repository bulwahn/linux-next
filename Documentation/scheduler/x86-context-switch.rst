.. SPDX-License-Identifier: GPL-2.0+

X86 Context Switch
------------------

The x86 architecture context switching logic is as follows.
After the switching of MM in the scheduler context_switch() calls the x86
implementation of :c:macro:`switch_to()`. For x86 arch it is located at ::

    arch/x86/include/asm/switch_to.h

Since kernel 4.9, switch_to() has been split into two parts: a
`prepare_switch_to()` macro and the inline assembly implementation of
__switch_to_asm() in the assembly files ::

    arch/x86/entry/entry_64.S
    arch/x86/entry/entry_32.S

prepare_switch_to() handles the case when stack uses virtual memory. This
is configured at build time and is enabled in most modern distributions.
This function accesses the stack pointer to prevent a double fault.
Switching to a stack that has top-level paging entry that is not
present in the current MM will result in a page fault which will be promoted
to double fault and the result is a panic. So it is necessary to probe the
stack now so that the vmalloc_fault can fix the page tables.

The main steps of the inline assembly function __switch_to_asm() are:

* store the callee saved registers to the old stack which will be switched
  away from.
* swap the stack pointers between the old and the new task.
* move the stack canary value to the current CPU's interrupt stack
* if return trampoline is enabled, overwrite all entries in the RSB on
  exiting a guest, to prevent malicious branch target predictions from
  affecting the host kernel.
* restore all registers from the new stack previously pushed in reverse
  order.
* jump to a C implementation of __switch_to(). The sources are located in::

      arch/x86/kernel/process_64.c
      arch/x86/kernel/process_32.c


The main steps of the C function __switch_to() which is effectively
the new task running are as follows:

* retrieve the thread :c:type:`struct thread_struct <thread_struct>`
  and fpu :c:type:`struct fpu <fpu>` structs from the next and previous
  tasks.
* get the current CPU TSS :c:type:`struct tss_struct <tss_struct>`.
* save the current FPU state while on the old task.
* store the FS and GS segment registers before changing the thread local
  storage.
* reload the GDT for the new tasks TLS.
  Following is effectively arch_end_context_switch().
* save the ES and DS segments of the previous task and load the same from
  the nest task.
* load the FS and GS segment registers.
* update the current task of the CPU.
* update the top of stack pointer for the CPU for entry trampoline.
* initialize FPU state for next task.
* set sp0 to point to the entry trampoline stack.
* call _switch_to_xtra() to handle debug registers, I/O
  bitmaps and speculation mitigation.
* write the task's CLOSid/RMID to IA32_PQR_MSR.
