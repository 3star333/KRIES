/*
 * kries_proc.c — KRIES /proc/kries interface
 *
 * Creates a read-only file at /proc/kries that streams a live process
 * integrity report to any user-space reader (e.g. `cat /proc/kries`).
 *
 * Uses the seq_file API (kernel 3.10+) which handles buffer management
 * and safe re-reading automatically. All output goes through seq_printf().
 *
 * Kernel compatibility: 5.6+ (uses proc_ops, introduced in 5.6).
 * For kernels < 5.6 replace proc_ops with file_operations and rename
 * the .proc_* fields to drop the "proc_" prefix.
 */

#include <linux/proc_fs.h>        /* proc_create(), proc_remove() */
#include <linux/seq_file.h>       /* seq_file, seq_printf(), single_open() */
#include <linux/sched.h>          /* task_struct */
#include <linux/sched/signal.h>   /* for_each_process() */
#include <linux/rcupdate.h>       /* rcu_read_lock/unlock */
#include <linux/ptrace.h>         /* PT_PTRACED */

#include "kries_log.h"
#include "kries_process.h"        /* kries_is_traced() */
#include "kries_proc.h"

#define KRIES_PROC_NAME "kries"

/*
 * kries_proc_show — writes the report into the seq_file buffer.
 *
 * Called by the kernel when user space reads /proc/kries.
 * seq_printf() is the kernel equivalent of fprintf() for /proc files.
 * Return 0 on success; seq_file sends the buffer to user space.
 */
static int kries_proc_show(struct seq_file *sf, void *unused)
{
    struct task_struct *task;

    seq_printf(sf, "KRIES — Kernel Runtime Integrity Report\n");
    seq_printf(sf, "========================================\n\n");

    seq_printf(sf, "%-8s  %-16s  %-8s  %s\n", "PID", "NAME", "PPID", "FLAGS");
    seq_printf(sf, "--------  ----------------  --------  -------\n");

    rcu_read_lock();
    for_each_process(task) {
        seq_printf(sf, "%-8d  %-16s  %-8d  %s\n",
                   task->pid,
                   task->comm,
                   task->parent->pid,
                   kries_is_traced(task) ? "[TRACED]" : "");
    }
    rcu_read_unlock();

    seq_printf(sf, "\n========================================\n");
    return 0;
}

/*
 * kries_proc_open — called on open(). Connects kries_proc_show to the
 * seq_file machinery via single_open(). single_open() is the right
 * helper when the entire output is generated in one pass.
 */
static int kries_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, kries_proc_show, NULL);
}

/*
 * kries_proc_fops — file operations for /proc/kries.
 *
 * proc_ops replaces file_operations for /proc files as of kernel 5.6.
 * seq_read, seq_lseek, single_release are all kernel-provided helpers.
 */
static const struct proc_ops kries_proc_fops = {
    .proc_open    = kries_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static struct proc_dir_entry *kries_proc_entry;

int kries_proc_init(void)
{
    kries_proc_entry = proc_create(KRIES_PROC_NAME, 0444, NULL, &kries_proc_fops);
    if (!kries_proc_entry) {
        KRIES_WARN("failed to create /proc/%s", KRIES_PROC_NAME);
        return -ENOMEM;
    }
    KRIES_INFO("/proc/%s created.", KRIES_PROC_NAME);
    return 0;
}

void kries_proc_exit(void)
{
    proc_remove(kries_proc_entry);
    KRIES_INFO("/proc/%s removed.", KRIES_PROC_NAME);
}
