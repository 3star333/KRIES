/*
 * kries_proc.h — KRIES /proc Interface (Phase 6)
 *
 * Public interface for the /proc filesystem integration.
 *
 * kries_proc_init() creates /proc/kries on module load.
 * kries_proc_exit() removes it on module unload.
 *
 * Failing to call kries_proc_exit() before unloading the module
 * leaves a dangling /proc entry — any subsequent read of /proc/kries
 * would dereference freed module memory and cause a kernel panic.
 */

#ifndef KRIES_PROC_H
#define KRIES_PROC_H

/*
 * kries_proc_init — register /proc/kries with the kernel.
 * Returns 0 on success, -ENOMEM if proc_create() fails.
 * Call from kries_init() in kries.c.
 */
int  kries_proc_init(void);

/*
 * kries_proc_exit — remove /proc/kries from the proc tree.
 * Always call from kries_exit() in kries.c, unconditionally.
 */
void kries_proc_exit(void);

#endif /* KRIES_PROC_H */
