/*
 * kries_process.h — KRIES process monitor interface
 */

#ifndef KRIES_PROCESS_H
#define KRIES_PROCESS_H

#include <linux/sched.h>

/*
 * kries_scan_processes — walk all task_structs and log PID, name, PPID.
 * Must be called from process context (sleepable). Uses RCU read lock.
 */
void kries_scan_processes(void);

/*
 * kries_is_traced — returns 1 if task has an active ptrace tracer, 0 if not.
 * Checks task->ptrace & PT_PTRACED. Safe to call under RCU read lock.
 */
int kries_is_traced(struct task_struct *task);

#endif /* KRIES_PROCESS_H */
