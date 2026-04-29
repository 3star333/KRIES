/*
 * kries_proc.c — KRIES /proc Interface (Phase 6)
 *
 * Creates a read-only file at /proc/kries that streams a snapshot of
 * system state to any user-space process that opens it (e.g. `cat`).
 *
 * Key concepts used here:
 *
 *   /proc filesystem  — A virtual (memory-only) filesystem that exposes
 *                       kernel state to user space. Every file under /proc
 *                       is backed by kernel callbacks, not disk data.
 *                       Reading /proc/kries calls our show() function.
 *
 *   seq_file          — The modern kernel API for /proc files that output
 *                       more than a trivial amount of data. It handles:
 *                         - buffer allocation and resizing automatically
 *                         - pagination (if output spans multiple read() calls)
 *                         - safe re-reading if the user seeks back to offset 0
 *                       Always prefer seq_file over raw read_proc callbacks.
 *
 *   single_open()     — A seq_file helper for simple files whose entire
 *                       content is generated in one pass. We provide one
 *                       function (kries_proc_show) that writes everything,
 *                       and single_open() wraps it in the seq_file machinery.
 *
 *   seq_printf()      — printf-style output into a seq_file buffer.
 *                       Equivalent to fprintf() but for kernel /proc files.
 *
 *   proc_create()     — Registers a new entry in the /proc filesystem.
 *                       Takes the filename, permissions, parent directory
 *                       (NULL = root of /proc), and file_operations struct.
 *
 *   proc_remove()     — Removes a /proc entry. MUST be called in module
 *                       exit to avoid dangling pointers in the proc tree.
 *
 *   file_operations   — A struct of function pointers that define how a
 *                       file behaves: open, read, release, write, etc.
 *                       For /proc files backed by seq_file, open/read/
 *                       release are filled in with seq_file helpers.
 */

#include <linux/proc_fs.h>   /* proc_create(), proc_remove(), struct proc_dir_entry */
#include <linux/seq_file.h>  /* seq_file, seq_printf(), single_open(), single_release() */
#include <linux/module.h>    /* struct module, THIS_MODULE, module_mutex */
#include <linux/sched.h>     /* task_struct, for_each_process() */
#include <linux/sched/signal.h> /* for_each_process() on newer kernels */
#include <linux/rcupdate.h>  /* rcu_read_lock/unlock */
#include <linux/ptrace.h>    /* PT_PTRACED */
#include <linux/list.h>      /* list_for_each_entry() */
#include <linux/mutex.h>     /* mutex_lock/unlock */

#include "kries_log.h"
#include "kries_proc.h"

/* The name of the /proc entry — readable at /proc/kries */
#define KRIES_PROC_NAME "kries"

/* ------------------------------------------------------------------ */
/* kries_proc_show                                                     */
/*                                                                     */
/* This is the core callback. The kernel calls it whenever user space  */
/* reads /proc/kries (e.g. via `cat /proc/kries`).                    */
/*                                                                     */
/* We use seq_printf() to write formatted output into the seq_file    */
/* buffer. The kernel takes care of copying it to user space.         */
/* ------------------------------------------------------------------ */
static int kries_proc_show(struct seq_file *sf, void *unused)
{
    struct task_struct *task;
    struct module *mod;

    /* ---- Header ---- */
    seq_printf(sf, "================================================\n");
    seq_printf(sf, "  KRIES - Kernel Runtime Integrity Report\n");
    seq_printf(sf, "================================================\n\n");

    /* ----------------------------------------------------------------
     * SECTION 1: Process List
     * Walk task_struct list under RCU read lock.
     * Mark processes being debugged with [TRACED].
     * ---------------------------------------------------------------- */
    seq_printf(sf, "[PROCESSES]\n");
    seq_printf(sf, "%-8s %-16s %-8s %s\n", "PID", "NAME", "PPID", "FLAGS");
    seq_printf(sf, "-------- ---------------- -------- --------\n");

    rcu_read_lock();
    for_each_process(task) {
        /*
         * Check PT_PTRACED — same logic as kries_process.c is_debugged().
         * We inline it here to keep the /proc output self-contained.
         */
        const char *flag = (task->ptrace & PT_PTRACED) ? "[TRACED]" : "";

        seq_printf(sf, "%-8d %-16s %-8d %s\n",
                   task->pid,
                   task->comm,
                   task->parent->pid,
                   flag);
    }
    rcu_read_unlock();

    seq_printf(sf, "\n");

    /* ----------------------------------------------------------------
     * SECTION 2: Loaded Kernel Modules
     * Walk module list under module_mutex.
     * Mark non-LIVE modules with [!].
     * ---------------------------------------------------------------- */
    seq_printf(sf, "[KERNEL MODULES]\n");
    seq_printf(sf, "%-24s %s\n", "NAME", "STATE");
    seq_printf(sf, "------------------------ ---------\n");

    mutex_lock(&module_mutex);
    list_for_each_entry(mod, &THIS_MODULE->list, list) {
        /* &THIS_MODULE->list as head naturally excludes KRIES itself */

        seq_printf(sf, "%-24s %s%s\n",
                   mod->name,
                   /* state string */
                   mod->state == MODULE_STATE_LIVE     ? "LIVE"     :
                   mod->state == MODULE_STATE_COMING   ? "COMING"   :
                   mod->state == MODULE_STATE_GOING    ? "GOING"    :
                   mod->state == MODULE_STATE_UNFORMED ? "UNFORMED" : "UNKNOWN",
                   /* flag non-live states prominently */
                   mod->state != MODULE_STATE_LIVE     ? " [!]"     : "");
    }
    mutex_unlock(&module_mutex);

    seq_printf(sf, "\n================================================\n");
    seq_printf(sf, "  End of Report\n");
    seq_printf(sf, "================================================\n");

    return 0; /* 0 = success; seq_file will handle sending buffer to user */
}

/* ------------------------------------------------------------------ */
/* kries_proc_open                                                     */
/*                                                                     */
/* Called when user space opens /proc/kries (e.g. open() syscall).   */
/* single_open() connects our show function to the seq_file           */
/* machinery. It allocates the seq_file state and stores the pointer  */
/* to kries_proc_show for later invocation during read().             */
/* ------------------------------------------------------------------ */
static int kries_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, kries_proc_show, NULL);
}

/* ------------------------------------------------------------------ */
/* kries_proc_fops — file operations for /proc/kries                  */
/*                                                                     */
/* This struct tells the kernel which functions to call for each      */
/* file operation on /proc/kries.                                     */
/*                                                                     */
/*   .open    → allocate seq_file state, link to kries_proc_show      */
/*   .read    → seq_read() handles copying buffered data to user space */
/*   .llseek  → seq_lseek() handles seeking (e.g. `cat` seeking to 0) */
/*   .release → single_release() frees the seq_file state             */
/*                                                                     */
/* seq_read, seq_lseek, single_release are all provided by the kernel */
/* — we do not need to implement them ourselves.                      */
/* ------------------------------------------------------------------ */
static const struct proc_ops kries_proc_fops = {
    .proc_open    = kries_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* Saved pointer to our proc entry — needed to remove it in exit */
static struct proc_dir_entry *kries_proc_entry;

/* ------------------------------------------------------------------ */
/* kries_proc_init — create /proc/kries                               */
/* ------------------------------------------------------------------ */
int kries_proc_init(void)
{
    /*
     * proc_create() arguments:
     *   name   — filename under /proc
     *   mode   — permissions (0444 = read-only for all users)
     *   parent — NULL = root of /proc
     *   fops   — our file_operations / proc_ops struct
     *
     * Returns NULL on failure (e.g. out of memory, duplicate name).
     */
    kries_proc_entry = proc_create(KRIES_PROC_NAME, 0444, NULL, &kries_proc_fops);

    if (!kries_proc_entry) {
        KRIES_WARN("Failed to create /proc/%s", KRIES_PROC_NAME);
        return -ENOMEM;
    }

    KRIES_INFO("/proc/%s created — read with: cat /proc/%s",
               KRIES_PROC_NAME, KRIES_PROC_NAME);
    return 0;
}

/* ------------------------------------------------------------------ */
/* kries_proc_exit — remove /proc/kries                               */
/*                                                                     */
/* This MUST be called before the module unloads. If /proc/kries is  */
/* left registered after kries.ko is removed, any subsequent read    */
/* will call kries_proc_show() — which no longer exists in memory.   */
/* The result is a kernel panic (null/invalid pointer dereference).  */
/* ------------------------------------------------------------------ */
void kries_proc_exit(void)
{
    proc_remove(kries_proc_entry);
    KRIES_INFO("/proc/%s removed.", KRIES_PROC_NAME);
}
