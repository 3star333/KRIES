/*
 * kries.c — KRIES: Kernel Runtime Integrity Enforcement System
 *
 * Phase 7: Detection engine added — kries_run_scan() applies rule
 * functions across all processes and modules, emitting structured alerts.
 *
 * This file is the entry point for the KRIES kernel module.
 * It demonstrates the bare minimum structure required for any
 * loadable kernel module (LKM) on Linux.
 */

#include <linux/init.h>    /* module_init(), module_exit() macros */
#include <linux/module.h>  /* MODULE_LICENSE, MODULE_AUTHOR, etc. */
#include <linux/kernel.h>  /* printk(), KERN_INFO, etc. */
#include "kries_log.h"      /* KRIES_INFO, KRIES_WARN, KRIES_ALERT */
#include "kries_process.h"  /* kries_scan_processes()              */
#include "kries_modules.h"  /* kries_scan_modules()                */
#include "kries_proc.h"     /* kries_proc_init/exit()              */
#include "kries_detect.h"   /* kries_run_scan()                    */

/* ------------------------------------------------------------------ */
/* Module metadata — shows up in modinfo and /sys/module/kries/        */
/* ------------------------------------------------------------------ */
MODULE_LICENSE("GPL");                       /* Required for access to GPL-only kernel symbols */
MODULE_AUTHOR("KRIES Project");
MODULE_DESCRIPTION("Kernel Runtime Integrity Enforcement System");
MODULE_VERSION("0.7");

/* ------------------------------------------------------------------ */
/* kries_init — called when the module is loaded via insmod            */
/* ------------------------------------------------------------------ */
static int __init kries_init(void)
{
    /*
     * printk() writes to the kernel ring buffer.
     * KERN_INFO is the log level (informational).
     * Use `dmesg | tail` to read it from user space.
     *
     * Format: printk(KERN_LEVEL "message\n");
     * Note: no comma between log level and string — they are concatenated.
     */
    KRIES_INFO("Module loaded successfully.");
    KRIES_INFO("Kernel Runtime Integrity Enforcement System v0.7");
    KRIES_INFO("Ready.");

    /* Phase 3+4: informational process scan — log all PIDs/names */
    kries_scan_processes();

    /* Phase 5: informational module scan — log all loaded modules */
    kries_scan_modules();

    /* Phase 7: run the detection engine — applies all rules, emits alerts */
    kries_run_scan();

    /* Phase 6: create /proc/kries — returns non-zero if it fails */
    if (kries_proc_init() != 0)
        return -ENOMEM;

    /* Return 0 to signal successful initialization.
     * A non-zero return value causes insmod to fail and unloads the module. */
    return 0;
}

/* ------------------------------------------------------------------ */
/* kries_exit — called when the module is removed via rmmod            */
/* ------------------------------------------------------------------ */
static void __exit kries_exit(void)
{
    /* Phase 6: remove /proc/kries BEFORE the module memory is freed.
     * Order matters — proc entry must go first. */
    kries_proc_exit();
    KRIES_INFO("Module unloaded. Goodbye.");
}

/* ------------------------------------------------------------------ */
/* Register init and exit functions with the kernel                    */
/* ------------------------------------------------------------------ */
module_init(kries_init);  /* Tells the kernel: call kries_init on load   */
module_exit(kries_exit);  /* Tells the kernel: call kries_exit on unload */
