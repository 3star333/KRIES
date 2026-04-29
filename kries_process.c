/*
 * kries_process.c — process enumeration and ptrace detection
 *
 * Walks the kernel task_struct linked list under an RCU read lock.
 * For each process, logs its PID, name (comm), and parent PID.
 * Exposes kries_is_traced() for use by the detection engine.
 *
 * Kernel compatibility: 5.x / 6.x
 *
 * RCU note: rcu_read_lock() disables preemption on the current CPU,
 * preventing task_struct entries from being freed mid-iteration.
 * Never sleep or block inside the critical section.
 */

#include <linux/sched.h>
#include <linux/sched/signal.h>   /* for_each_process() */
#include <linux/rcupdate.h>       /* rcu_read_lock/unlock */
#include <linux/ptrace.h>         /* PT_PTRACED */

#include "kries_log.h"
#include "kries_process.h"

/*
 * kries_is_traced — inspect task->ptrace for PT_PTRACED.
 *
 * The kernel sets PT_PTRACED in __ptrace_link() when any tracer
 * (GDB, strace, etc.) attaches via the ptrace(2) syscall.
 * task_struct lives in kernel memory — this flag cannot be cleared
 * or forged from user space.
 */
int kries_is_traced(struct task_struct *task)
{
    return (task->ptrace & PT_PTRACED) ? 1 : 0;
}

/*
 * kries_scan_processes — enumerate all processes and log their identity.
 *
 * for_each_process() visits one thread-group leader per process
 * (not individual threads). The list is circular and RCU-protected.
 */
void kries_scan_processes(void)
{
    struct task_struct *task;
    int count = 0;

    KRIES_INFO("--- process scan start ---");

    rcu_read_lock();
    for_each_process(task) {
        KRIES_INFO("pid=%-6d  name=%-16s  ppid=%d",
                   task->pid,
                   task->comm,
                   task->parent->pid);
        count++;
    }
    rcu_read_unlock();

    KRIES_INFO("--- process scan complete: %d processes ---", count);
}
