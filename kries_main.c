/*
 * kries_main.c — KRIES: Kernel Runtime Integrity Enforcement System
 *
 * Entry point for the KRIES loadable kernel module.
 * Initialises all subsystems and registers /proc/kries on load.
 * Tears down cleanly on unload.
 *
 * Compatible: Linux kernel 5.6+ (tested on 6.8)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "kries_log.h"
#include "kries_process.h"
#include "kries_detect.h"
#include "kries_proc.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("KRIES Project");
MODULE_DESCRIPTION("Kernel Runtime Integrity Enforcement System");
MODULE_VERSION("1.0");

static int __init kries_init(void)
{
    KRIES_INFO("KRIES v1.0 loaded.");

    /* Enumerate all running processes and log PID / name / PPID */
    kries_scan_processes();

    /* Run detection rules — emits KRIES_ALERT for any findings */
    kries_run_scan();

    /* Register /proc/kries — readable from user space with cat */
    if (kries_proc_init() != 0)
        return -ENOMEM;

    KRIES_INFO("Ready. Read state: cat /proc/kries");
    return 0;
}

static void __exit kries_exit(void)
{
    /*
     * /proc entry MUST be removed before the module is freed.
     * Leaving it registered would make any subsequent read call
     * a function pointer that no longer exists — kernel panic.
     */
    kries_proc_exit();
    KRIES_INFO("KRIES unloaded.");
}

module_init(kries_init);
module_exit(kries_exit);
