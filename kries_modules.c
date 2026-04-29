/*
 * kries_modules.c — KRIES Kernel Module Monitor (Phase 5)
 *
 * Iterates over all currently loaded kernel modules using the kernel's
 * internal module linked list and logs each module's name and state.
 *
 * Key concepts used here:
 *
 *   struct module     — The kernel's per-module data structure. Every loaded
 *                       .ko file has exactly one. It contains the module's
 *                       name, state, reference count, and symbol tables.
 *                       Defined in <linux/module.h>.
 *
 *   THIS_MODULE       — A macro that expands to a pointer to the current
 *                       module's own `struct module`. We use it to skip
 *                       KRIES itself from the output, since KRIES is loaded
 *                       and therefore appears in its own scan results.
 *
 *   modules (list)    — The kernel maintains all loaded modules in a
 *                       doubly-linked list. Each struct module contains a
 *                       `list` field of type `struct list_head` which links
 *                       it into this global list.
 *
 *   list_for_each_entry() — A macro from <linux/list.h> that walks a
 *                       list_head linked list, casting each node back to
 *                       the containing struct (struct module here).
 *
 *   module_mutex      — A kernel mutex that protects the module list against
 *                       concurrent modifications (e.g. another insmod running
 *                       while we are iterating). We must hold it for the
 *                       entire traversal. Unlike RCU, mutex_lock() CAN sleep,
 *                       so this must not be called from interrupt context.
 *
 *   module->state     — An enum (module_state) indicating the module's
 *                       current lifecycle stage. See state table below.
 *
 * Module states (enum module_state):
 *   MODULE_STATE_LIVE     (0) — loaded and running normally
 *   MODULE_STATE_COMING   (1) — currently being loaded (init running)
 *   MODULE_STATE_GOING    (2) — currently being unloaded (exit running)
 *   MODULE_STATE_UNFORMED (3) — partially initialized, not yet live
 */

#include <linux/module.h>    /* struct module, THIS_MODULE, module_mutex */
#include <linux/list.h>      /* list_for_each_entry() */
#include <linux/mutex.h>     /* mutex_lock(), mutex_unlock() */

#include "kries_log.h"       /* KRIES_INFO, KRIES_WARN, KRIES_ALERT */
#include "kries_modules.h"

/* ------------------------------------------------------------------ */
/* state_name — human-readable label for enum module_state            */
/*                                                                     */
/* Keeps the log output readable without magic numbers.               */
/* ------------------------------------------------------------------ */
static const char *state_name(enum module_state state)
{
    switch (state) {
    case MODULE_STATE_LIVE:     return "LIVE";
    case MODULE_STATE_COMING:   return "COMING";
    case MODULE_STATE_GOING:    return "GOING";
    case MODULE_STATE_UNFORMED: return "UNFORMED";
    default:                    return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/* kries_scan_modules                                                  */
/*                                                                     */
/* Acquires module_mutex, walks the module list, and logs each        */
/* module's name and state. Skips KRIES itself (THIS_MODULE).         */
/* ------------------------------------------------------------------ */
void kries_scan_modules(void)
{
    struct module *mod;   /* pointer to each module during iteration */
    int count = 0;        /* total modules found (excluding KRIES)   */

    KRIES_INFO("--- Module Scan Start ---");

    /*
     * mutex_lock() acquires module_mutex, blocking if another thread
     * currently holds it (e.g. an in-progress insmod or rmmod).
     *
     * This is a sleeping lock — the kernel will deschedule this thread
     * until the mutex is available. That is why this function must only
     * be called from a sleepable context (process context, not IRQ).
     */
    mutex_lock(&module_mutex);

    /*
     * list_for_each_entry(pos, head, member) walks a list_head list.
     *
     *   pos    — loop variable: pointer to struct module for each iteration
     *   head   — the list root. THIS_MODULE->list points into the global
     *            module list; we use it as our starting anchor.
     *   member — the name of the list_head field inside struct module
     *
     * The macro resolves each list node back to its enclosing struct module
     * via container_of(). This is the standard Linux kernel list pattern.
     */
    list_for_each_entry(mod, THIS_MODULE->list.prev, list) {

        /*
         * Skip KRIES itself. Since we are a loaded module, we appear in
         * our own list. Reporting ourselves would be misleading noise.
         *
         * THIS_MODULE is a pointer to our own struct module.
         * Comparing pointers directly is the correct identity check.
         */
        if (mod == THIS_MODULE)
            continue;

        /*
         * Log the module name and its current lifecycle state.
         * A state other than LIVE during a scan is suspicious —
         * it may indicate a module being loaded or hidden mid-operation.
         */
        if (mod->state != MODULE_STATE_LIVE) {
            /*
             * A non-LIVE module found during a scan is unusual.
             * COMING/GOING states should be transient and brief.
             * Seeing UNFORMED here may indicate a partial/failed load.
             */
            KRIES_WARN("name=%-24s  state=%s (non-live)",
                       mod->name,
                       state_name(mod->state));
        } else {
            KRIES_INFO("name=%-24s  state=%s",
                       mod->name,
                       state_name(mod->state));
        }

        count++;
    }

    mutex_unlock(&module_mutex); /* Always release — even on early return */

    KRIES_INFO("--- Module Scan Complete: %d modules found ---", count);
}
