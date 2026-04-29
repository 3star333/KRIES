/*
 * kries_detect.h — KRIES detection engine interface
 */

#ifndef KRIES_DETECT_H
#define KRIES_DETECT_H

/*
 * kries_run_scan — apply all detection rules against all running processes.
 * Emits a KRIES_ALERT for every rule match.
 * Returns the total number of alerts generated (0 = nothing detected).
 */
int kries_run_scan(void);

#endif /* KRIES_DETECT_H */
