/*
 * kries_log.h — KRIES unified logging macros
 *
 * All output from KRIES goes through these three macros.
 * The [KRIES] prefix makes every message grep-able in dmesg.
 *
 * Usage:
 *   KRIES_INFO("scan started, pid=%d", current->pid);
 *   KRIES_WARN("unexpected state: %s", task->comm);
 *   KRIES_ALERT("type=PTRACE_DETECTED pid=%d", task->pid);
 */

#ifndef KRIES_LOG_H
#define KRIES_LOG_H

#include <linux/kernel.h>

/* Routine operational messages */
#define KRIES_INFO(fmt, ...)  \
    printk(KERN_INFO    "[KRIES] "        fmt "\n", ##__VA_ARGS__)

/* Suspicious but non-critical events */
#define KRIES_WARN(fmt, ...)  \
    printk(KERN_WARNING "[KRIES][WARN] "  fmt "\n", ##__VA_ARGS__)

/* High-severity detections */
#define KRIES_ALERT(fmt, ...) \
    printk(KERN_ALERT   "[KRIES][ALERT] " fmt "\n", ##__VA_ARGS__)

#endif /* KRIES_LOG_H */
