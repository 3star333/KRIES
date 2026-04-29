/*
 * kries_log.h — KRIES Logging Infrastructure
 *
 * Phase 2: Unified logging macros for the KRIES kernel module.
 *
 * All output from KRIES goes through these macros. This ensures:
 *   - Consistent [KRIES] prefix on every message
 *   - Automatic newline — callers never need to add \n
 *   - A single place to change log format for the entire project
 *   - Clear severity levels that map directly to printk levels
 *
 * Usage:
 *   KRIES_INFO("Module started, pid=%d", current->pid);
 *   KRIES_WARN("Unexpected state in process %s", task->comm);
 *   KRIES_ALERT("Debug detected: pid=%d name=%s", pid, name);
 */

#ifndef KRIES_LOG_H
#define KRIES_LOG_H

#include <linux/kernel.h>  /* KERN_INFO, KERN_WARNING, KERN_ALERT, printk() */

/* ------------------------------------------------------------------ */
/* KRIES_INFO — routine operational messages                           */
/*                                                                     */
/* Maps to KERN_INFO (log level 6).                                    */
/* Use for: module startup/shutdown, scan results, status updates.     */
/* ------------------------------------------------------------------ */
#define KRIES_INFO(fmt, ...)  \
    printk(KERN_INFO "[KRIES] " fmt "\n", ##__VA_ARGS__)

/* ------------------------------------------------------------------ */
/* KRIES_WARN — suspicious but non-critical events                     */
/*                                                                     */
/* Maps to KERN_WARNING (log level 4).                                 */
/* Use for: unexpected process states, unusual module names, anomalies */
/* that warrant attention but don't confirm a threat.                  */
/* ------------------------------------------------------------------ */
#define KRIES_WARN(fmt, ...)  \
    printk(KERN_WARNING "[KRIES][WARN] " fmt "\n", ##__VA_ARGS__)

/* ------------------------------------------------------------------ */
/* KRIES_ALERT — high-severity detections requiring immediate action   */
/*                                                                     */
/* Maps to KERN_ALERT (log level 1 — just below KERN_EMERG).          */
/* Use for: confirmed ptrace detection, unauthorized module load,      */
/* any event that indicates active compromise or policy violation.     */
/* ------------------------------------------------------------------ */
#define KRIES_ALERT(fmt, ...) \
    printk(KERN_ALERT "[KRIES][ALERT] " fmt "\n", ##__VA_ARGS__)

#endif /* KRIES_LOG_H */
