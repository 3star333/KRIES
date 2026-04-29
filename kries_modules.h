/*
 * kries_modules.h — KRIES Kernel Module Monitor (Phase 5)
 *
 * Public interface for the kernel module scanning subsystem.
 * kries.c calls kries_scan_modules() from its init function.
 */

#ifndef KRIES_MODULES_H
#define KRIES_MODULES_H

/*
 * kries_scan_modules — iterate over every currently loaded kernel module
 * and log its name and state.
 *
 * Internally acquires module_mutex for the duration of iteration to
 * prevent concurrent module load/unload from corrupting the list walk.
 *
 * Must be called from a sleepable context (init/exit is fine).
 * Do NOT call from interrupt context — mutex_lock() can sleep.
 */
void kries_scan_modules(void);

#endif /* KRIES_MODULES_H */
