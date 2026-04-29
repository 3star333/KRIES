/*
 * kries_process.c — KRIES Process Monitor (Phase 3 + 4)
 *
 * Phase 3: Iterates over all running processes and logs PID, name, PPID.
 * Phase 4: Adds is_debugged() — detects processes under ptrace control.
 *
 * Key concepts used here:
 *
 *   task_struct   — The kernel's per-process data structure. Every process
 *                   and thread has exactly one. It lives in kernel memory.
 *
 *   for_each_process(p) — A macro that walks the task_struct list, visiting
 *                   one thread-group leader per process (i.e. one entry per
 *                   process, not per thread). Defined in linux/sched/signal.h.
 *
 *   RCU           — Read-Copy-Update. The process list is RCU-protected.
 *                   We must hold the RCU read lock while iterating to prevent
 *                   use-after-free if a process exits mid-scan. The lock is
 *                   extremely cheap — it disables preemption, nothing more.
 *
 *   task->ptrace  — A bitmask of ptrace-related flags in task_struct.
 *                   PT_PTRACED (0x1) is set by the kernel whenever a tracer
 *                   attaches to this process via ptrace(2). Reading this flag
 *                   from kernel space is reliable — user space cannot forge it.
 *
 *   task->comm    — A 16-byte (TASK_COMM_LEN) char array holding the
 *                   truncated executable name. Not the full command line.
 *
 *   task->pid     — The process ID (PID) visible to user space via getpid().
 *
 *   task->parent  — Pointer to the parent task_struct. Accessing ->pid on
 *                   it gives the PPID. Safe to read under RCU.
 */

#include <linux/sched.h>          /* task_struct, for_each_process() */
#include <linux/sched/signal.h>   /* for_each_process() on newer kernels */
#include <linux/rcupdate.h>       /* rcu_read_lock(), rcu_read_unlock() */
#include <linux/ptrace.h>         /* PT_PTRACED flag definition */
#include <linux/init.h>           /* __init / __exit markers */

#include "kries_log.h"            /* KRIES_INFO, KRIES_WARN, KRIES_ALERT */
#include "kries_process.h"

/* ------------------------------------------------------------------ */
/* is_debugged                                                         */
/*                                                                     */
/* Checks whether a process is currently under ptrace (debugger)      */
/* control by inspecting the PT_PTRACED flag in task->ptrace.         */
/*                                                                     */
/* task->ptrace is a bitmask. PT_PTRACED == 0x00000001.               */
/* The kernel sets this flag in __ptrace_link() when a tracer          */
/* attaches, and clears it in __ptrace_unlink() when it detaches.     */
/*                                                                     */
/* This cannot be spoofed from user space — task_struct lives in      */
/* kernel memory, inaccessible to user processes.                     */
/*                                                                     */
/* Returns: 1 if traced, 0 if not.                                    */
/* ------------------------------------------------------------------ */
int is_debugged(struct task_struct *task)
{
    /*
     * PT_PTRACED is defined in <linux/ptrace.h>.
     * Bitwise AND isolates just that flag.
     * Any non-zero result means the flag is set → process is traced.
     */
    return (task->ptrace & PT_PTRACED) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* kries_scan_processes                                                */
/*                                                                     */
/* Walks the kernel process list and logs each process's identity.    */
/* Phase 4: also calls is_debugged() and fires KRIES_ALERT if traced. */
/* Called once from kries_init() in kries.c.                          */
/* ------------------------------------------------------------------ */
void kries_scan_processes(void)
{
    struct task_struct *task;   /* pointer to the current task in iteration */
    int count = 0;              /* how many processes we found */

    KRIES_INFO("--- Process Scan Start ---");

    /*
     * rcu_read_lock() enters an RCU read-side critical section.
     *
     * This is not a traditional mutex — it does not sleep or spin.
     * It simply prevents the current CPU from being preempted, which
     * is enough to guarantee that RCU-protected pointers (like the
     * entries in the task list) won't be freed beneath us.
     *
     * Rule: never sleep, call schedule(), or block inside an RCU
     * read-side critical section.
     */
    rcu_read_lock();

    /*
     * for_each_process(task) expands to a for-loop that walks the
     * circular doubly-linked list of task_structs.
     *
     * It starts at init_task (PID 1's parent, the idle process) and
     * visits every process exactly once. Threads within a process are
     * NOT individually visited — only the thread group leader is.
     */
    for_each_process(task) {
        /*
         * Log every process at INFO level — PID, name, PPID.
         * %-6d / %-16s: left-aligned with padding for readable columns.
         */
        KRIES_INFO("pid=%-6d  name=%-16s  ppid=%d",
                   task->pid,
                   task->comm,
                   task->parent->pid);

        /*
         * Phase 4: Debug detection.
         *
         * is_debugged() checks task->ptrace & PT_PTRACED.
         * If set, a tracer (GDB, strace, etc.) is attached to this process.
         *
         * We use KRIES_ALERT here because an unexpected traced process is
         * a high-confidence indicator of unauthorized debugging activity.
         *
         * Note: legitimate debuggers (developers running GDB) will also
         * trigger this. A production deployment would maintain a whitelist
         * of allowed tracers. For KRIES, we log all cases and alert.
         */
        if (is_debugged(task)) {
            KRIES_ALERT("DEBUG_DETECTED pid=%-6d  name=%-16s  ppid=%d  ptrace_flags=0x%x",
                        task->pid,
                        task->comm,
                        task->parent->pid,
                        task->ptrace);
        }

        count++;
    }

    rcu_read_unlock(); /* Exit RCU critical section — preemption re-enabled */

    KRIES_INFO("--- Process Scan Complete: %d processes found ---", count);
}
