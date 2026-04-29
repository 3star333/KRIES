/*
 * kries_detect.c — KRIES Detection Engine (Phase 7)
 *
 * Combines all previous monitoring capabilities into a single rule-based
 * scan that produces structured, parseable alert log lines.
 *
 * Design:
 *   - Each threat type is encoded as an independent rule function.
 *   - Rules take the smallest possible input (one task or one module).
 *   - kries_run_scan() drives both walks, calls every rule, and emits
 *     a structured alert line for every positive match.
 *   - Alert lines use key=value format so they are grep/awk/splunk-friendly.
 *
 * Alert format:
 *   [KRIES][ALERT] type=<TYPE>  <key>=<val> [<key>=<val> ...]
 *
 * Alert types defined here:
 *   PTRACE_DETECTED     — process is under active debugger control
 *   SUSPICIOUS_MODULE   — module name or state matches a threat heuristic
 *
 * Adding a new rule:
 *   1. Write a rule_*() function that takes a task or module pointer.
 *   2. Call it inside scan_processes() or scan_modules() below.
 *   3. Add a corresponding emit_alert_*() call on match.
 *   That's it — no other files need to change.
 */

#include <linux/sched.h>          /* task_struct, for_each_process */
#include <linux/sched/signal.h>   /* for_each_process on newer kernels */
#include <linux/rcupdate.h>       /* rcu_read_lock/unlock */
#include <linux/ptrace.h>         /* PT_PTRACED */
#include <linux/module.h>         /* struct module, THIS_MODULE, module_mutex */
#include <linux/list.h>           /* list_for_each_entry */
#include <linux/mutex.h>          /* mutex_lock/unlock */
#include <linux/string.h>         /* strncmp() */

#include "kries_log.h"
#include "kries_detect.h"

/* ================================================================== */
/* RULE IMPLEMENTATIONS                                                */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* rule_is_debugged                                                    */
/*                                                                     */
/* Detects active ptrace tracing by reading the PT_PTRACED bit from   */
/* task->ptrace. This flag is set by the kernel in __ptrace_link()    */
/* when a tracer attaches, and cleared in __ptrace_unlink() when it   */
/* detaches. It lives in kernel memory — not forgeable from user space.*/
/* ------------------------------------------------------------------ */
int rule_is_debugged(struct task_struct *task)
{
    return (task->ptrace & PT_PTRACED) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* rule_is_suspicious_module                                           */
/*                                                                     */
/* Applies name-based and state-based heuristics to flag modules that */
/* exhibit common rootkit characteristics.                            */
/*                                                                     */
/* Name heuristics — known rootkit naming patterns:                   */
/*   "hide_"  prefixed modules are a textbook rootkit convention      */
/*   "hook_"  suggests the module intends to intercept kernel calls   */
/*   "root"   is a common rootkit prefix (rootkit, rootme, roothide…) */
/*                                                                     */
/* State heuristic:                                                    */
/*   A module observed in UNFORMED state during a scan may be a       */
/*   partially-loaded or deliberately stalled module.                 */
/*                                                                     */
/* Note: these heuristics are illustrative. A production system would */
/* use a cryptographic allowlist of approved module names/hashes.     */
/* ------------------------------------------------------------------ */
int rule_is_suspicious_module(struct module *mod)
{
    /* Name-based heuristics — strncmp avoids reading past the buffer */
    if (strncmp(mod->name, "hide_", 5) == 0) return 1;
    if (strncmp(mod->name, "hook_", 5) == 0) return 1;
    if (strncmp(mod->name, "root",  4) == 0) return 1;

    /* State heuristic — UNFORMED during a live scan is abnormal */
    if (mod->state == MODULE_STATE_UNFORMED) return 1;

    return 0;
}

/* ================================================================== */
/* ALERT EMITTERS                                                      */
/*                                                                     */
/* Separated from the rule functions so the format of every alert     */
/* type is defined in exactly one place. Change the format here and   */
/* it propagates everywhere the alert is generated.                   */
/* ================================================================== */

static void emit_alert_ptrace(struct task_struct *task)
{
    /*
     * Structured alert line. Fields:
     *   type        — alert category (for log parsing/filtering)
     *   pid         — numeric process ID
     *   name        — process comm (executable name, max 15 chars)
     *   ppid        — parent PID (context for process tree analysis)
     *   ptrace_flags— raw hex value of task->ptrace (for forensics)
     */
    KRIES_ALERT("type=PTRACE_DETECTED   pid=%-6d  name=%-16s  ppid=%-6d  ptrace_flags=0x%x",
                task->pid,
                task->comm,
                task->parent->pid,
                task->ptrace);
}

static void emit_alert_module(struct module *mod, const char *reason)
{
    /*
     * Structured alert line. Fields:
     *   type    — alert category
     *   name    — module name (from struct module.name)
     *   state   — human-readable module lifecycle state
     *   reason  — which heuristic triggered (name_prefix or bad_state)
     */
    KRIES_ALERT("type=SUSPICIOUS_MODULE  name=%-24s  state=%-10s  reason=%s",
                mod->name,
                mod->state == MODULE_STATE_LIVE     ? "LIVE"     :
                mod->state == MODULE_STATE_COMING   ? "COMING"   :
                mod->state == MODULE_STATE_GOING    ? "GOING"    :
                mod->state == MODULE_STATE_UNFORMED ? "UNFORMED" : "UNKNOWN",
                reason);
}

/* ================================================================== */
/* INTERNAL SCAN DRIVERS                                               */
/* ================================================================== */

/*
 * scan_processes — walk all task_structs, apply process rules.
 * Returns the number of alerts generated.
 */
static int scan_processes(void)
{
    struct task_struct *task;
    int alerts = 0;

    rcu_read_lock();

    for_each_process(task) {
        if (rule_is_debugged(task)) {
            emit_alert_ptrace(task);
            alerts++;
        }
        /*
         * Future process rules go here:
         *
         *   if (rule_zombie_too_long(task)) { ... alerts++; }
         *   if (rule_uid_escalation(task))  { ... alerts++; }
         */
    }

    rcu_read_unlock();
    return alerts;
}

/*
 * scan_modules — walk all loaded modules, apply module rules.
 * Returns the number of alerts generated.
 */
static int scan_modules(void)
{
    struct module *mod;
    int alerts = 0;

    mutex_lock(&module_mutex);

    list_for_each_entry(mod, THIS_MODULE->list.prev, list) {
        if (mod == THIS_MODULE)
            continue;   /* never flag KRIES itself */

        if (rule_is_suspicious_module(mod)) {
            /*
             * Determine which heuristic matched so the alert is actionable.
             * The first match wins — order reflects severity priority.
             */
            const char *reason =
                (strncmp(mod->name, "hide_", 5) == 0) ? "name_prefix:hide_"  :
                (strncmp(mod->name, "hook_", 5) == 0) ? "name_prefix:hook_"  :
                (strncmp(mod->name, "root",  4) == 0) ? "name_prefix:root"   :
                (mod->state == MODULE_STATE_UNFORMED)  ? "bad_state:UNFORMED" :
                                                         "unknown";

            emit_alert_module(mod, reason);
            alerts++;
        }
        /*
         * Future module rules go here:
         *
         *   if (rule_module_not_signed(mod)) { ... alerts++; }
         *   if (rule_module_unknown_section(mod)) { ... alerts++; }
         */
    }

    mutex_unlock(&module_mutex);
    return alerts;
}

/* ================================================================== */
/* PUBLIC ENTRY POINT                                                  */
/* ================================================================== */

/*
 * kries_run_scan — execute all detection rules.
 *
 * Called from kries_init() in kries.c. Can also be wired to a timer
 * or kernel thread in a future phase for periodic rescanning.
 *
 * Returns total alert count across all rules and all subsystems.
 */
int kries_run_scan(void)
{
    int alerts = 0;

    KRIES_INFO("========== Detection Scan Start ==========");

    alerts += scan_processes();
    alerts += scan_modules();

    if (alerts == 0) {
        KRIES_INFO("Scan complete — no threats detected.");
    } else {
        /*
         * KERN_ALERT summary line — makes the total visible even if
         * individual alert lines scroll off a short dmesg buffer.
         */
        KRIES_ALERT("Scan complete — %d alert(s) generated. Review above.", alerts);
    }

    KRIES_INFO("========== Detection Scan End ============");

    return alerts;
}
