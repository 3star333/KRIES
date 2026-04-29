/*
 * kries_detect.h — KRIES Detection Engine (Phase 7)
 *
 * Public interface for the rule-based detection subsystem.
 *
 * The detection engine applies a set of rule functions against all
 * running processes and loaded modules, generating structured alerts
 * for anything that matches a threat signature.
 *
 * Individual rule functions (is_debugged, is_suspicious_module) are
 * also exposed here so kries_proc.c can call them directly when
 * building the /proc/kries report.
 */

#ifndef KRIES_DETECT_H
#define KRIES_DETECT_H

#include <linux/sched.h>   /* task_struct */
#include <linux/module.h>  /* struct module */

/* ------------------------------------------------------------------ */
/* Individual rule functions                                           */
/*                                                                     */
/* Each returns 1 (match / threat detected) or 0 (clean).            */
/* These are the atomic building blocks of the detection engine.      */
/* ------------------------------------------------------------------ */

/*
 * rule_is_debugged — returns 1 if the process has an active ptrace tracer.
 * Inspects task->ptrace & PT_PTRACED.
 */
int rule_is_debugged(struct task_struct *task);

/*
 * rule_is_suspicious_module — returns 1 if the module is considered
 * suspicious based on its name prefix or lifecycle state.
 *
 * Current heuristics:
 *   - Name starts with "hide_"  → classic rootkit naming convention
 *   - Name starts with "hook_"  → suggests syscall/function hooking
 *   - Name starts with "root"   → rootkit-style name
 *   - State is not MODULE_STATE_LIVE → caught mid-load or abnormal
 */
int rule_is_suspicious_module(struct module *mod);

/* ------------------------------------------------------------------ */
/* Central scan entry point                                            */
/* ------------------------------------------------------------------ */

/*
 * kries_run_scan — execute all detection rules against all processes
 * and modules. Logs a structured KRIES_ALERT for every rule match.
 *
 * This is the single function kries.c calls. It internally drives
 * both the process walk and the module walk, applying every rule.
 *
 * Returns the total number of alerts generated (0 = system clean).
 */
int kries_run_scan(void);

#endif /* KRIES_DETECT_H */
