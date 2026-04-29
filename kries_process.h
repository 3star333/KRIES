/*
 * kries_process.h — KRIES Process Monitor (Phase 3 + 4)
 *
 * Public interface for the process scanning subsystem.
 * kries.c calls kries_scan_processes() from its init function.
 */

#ifndef KRIES_PROCESS_H
#define KRIES_PROCESS_H

#include <linux/sched.h>   /* task_struct */

/*
 * kries_scan_processes — iterate over every running process and log:
 *   - PID  (process ID)
 *   - name (task->comm, up to 16 chars)
 *   - PPID (parent process ID)
 *   - debug status (Phase 4: warns if the process is being traced)
 *
 * Must be called from a sleepable context (init/exit is fine).
 * Internally holds the RCU read lock for the duration of iteration.
 */
void kries_scan_processes(void);

/*
 * is_debugged — inspect a single task_struct for active ptrace tracing.
 *
 * Returns 1 if the process is currently being traced (debugged), 0 if not.
 *
 * How it works:
 *   The kernel sets the PT_PTRACED flag in task->ptrace whenever a tracer
 *   (e.g. GDB, strace) attaches to a process via the ptrace(2) syscall.
 *   This flag lives in kernel memory and cannot be cleared from user space,
 *   making it a reliable detection signal when read from kernel code.
 *
 * Caller must hold the RCU read lock (already the case inside
 * kries_scan_processes, which is the only intended caller).
 */
int is_debugged(struct task_struct *task);

#endif /* KRIES_PROCESS_H */
