/*
 * kries_proc.h — KRIES /proc interface
 */

#ifndef KRIES_PROC_H
#define KRIES_PROC_H

/* Create /proc/kries. Returns 0 on success, -ENOMEM on failure. */
int  kries_proc_init(void);

/* Remove /proc/kries. Must be called before module unload. */
void kries_proc_exit(void);

#endif /* KRIES_PROC_H */
