/*
 * kries_detect.c — KRIES detection engine
 *
 * Applies rule functions against all running processes and emits
 * structured alert log lines for any positive matches.
 *
 * Alert format (key=value, grep/splunk-friendly):
 *   [KRIES][ALERT] type=<TYPE> pid=<N> name=<comm> ppid=<N> [extra fields]
 *
 * Adding a new rule:
 *   1. Write a static rule_*() function below.
 *   2. Call it inside kries_run_scan()'s for_each_process loop.
 *   3. Increment alerts and call KRIES_ALERT on match.
 *   No other files need to change.
 *
 * Kernel compatibility: 5.x / 6.x
 */

#include <linux/sched.h>
#include <linux/sched/signal.h>   /* for_each_process() */
#include <linux/rcupdate.h>       /* rcu_read_lock/unlock */
#include <linux/ptrace.h>         /* PT_PTRACED */

#include "kries_log.h"
#include "kries_process.h"        /* kries_is_traced() */
#include "kries_detect.h"

/*
 * rule_ptrace — detect processes under active debugger control.
 *
 * PT_PTRACED (bit 0 of task->ptrace) is set by the kernel whenever a
 * tracer attaches via ptrace(2). Reading it from kernel space is
 * authoritative — user space cannot clear or forge it.
 *
 * False positives: any legitimate GDB/strace session will fire this.
 * A production deployment would whitelist known-good tracers by UID
 * or by comparing the tracer's credentials. KRIES logs all cases.
 */
static int rule_ptrace(struct task_struct *task)
{
    return kries_is_traced(task);
}

/*
 * kries_run_scan — main detection loop.
 *
 * Iterates every process under RCU read lock, applies all rules,
 * and emits one KRIES_ALERT line per positive match.
 * Prints a summary line regardless of result.
 */
int kries_run_scan(void)
{
    struct task_struct *task;
    int alerts = 0;

    KRIES_INFO("--- detection scan start ---");

    rcu_read_lock();
    for_each_process(task) {

        /* Rule: ptrace / debugger attachment */
        if (rule_ptrace(task)) {
            KRIES_ALERT("type=PTRACE_DETECTED  pid=%-6d  name=%-16s  ppid=%-6d  ptrace_flags=0x%x",
                        task->pid,
                        task->comm,
                        task->parent->pid,
                        task->ptrace);
            alerts++;
        }

        /*
         * Add future rules here, e.g.:
         *   if (rule_uid_zero_unexpected(task)) { ... alerts++; }
         */
    }
    rcu_read_unlock();

    if (alerts == 0)
        KRIES_INFO("--- scan complete: no threats detected ---");
    else
        KRIES_ALERT("--- scan complete: %d alert(s) ---", alerts);

    return alerts;
}
