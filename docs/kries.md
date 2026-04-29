---
title: "KRIES: Kernel Runtime Integrity Enforcement System"
author: "3star333"
date: "April 28, 2026"
geometry: margin=1in
fontsize: 12pt
linestretch: 2
header-includes:
  - \usepackage{times}
  - \usepackage{setspace}
---

# Abstract

KRIES (Kernel Runtime Integrity Enforcement System) is a loadable Linux kernel module (LKM) that performs runtime integrity monitoring from within the kernel itself. The system enumerates all running processes, detects unauthorized debugging via inspection of the `ptrace` flag in each process's `task_struct`, and exposes a live integrity report through the `/proc` virtual filesystem. Implemented in C and targeting Ubuntu Linux kernel 6.8, KRIES demonstrates that meaningful host-based security monitoring is achievable using stable, well-documented kernel APIs without invasive hooking or policy enforcement. This paper describes the system design, kernel mechanisms used, implementation decisions, and the inherent limitations of same-privilege monitoring.

---

# 1. Introduction

User-space security tools share a fundamental weakness: they are processes. An attacker operating at kernel privilege can manipulate or hide from any monitor that runs above them. Antivirus software, host-based intrusion detection systems, and endpoint agents are all subject to this constraint. When a threat operates at ring 0, the only reliable vantage point for detection is ring 0 itself.

Linux loadable kernel modules (LKMs) provide a mechanism for extending the kernel at runtime without recompilation or reboot. A security module loaded as an LKM executes at the same privilege level as the kernel, shares the kernel's address space, and can read data structures — such as the process table and debugging state — that user-space tools can only observe through mediated interfaces.

KRIES exploits this position to implement two core monitoring capabilities:

1. **Process enumeration** — reading the kernel's internal process list directly, bypassing any user-space mediation.
2. **Ptrace detection** — inspecting the `PT_PTRACED` flag in each process's `task_struct` to identify processes currently under debugger control.

The findings are surfaced via `/proc/kries`, a virtual file that any user-space tool can read with a simple `cat` command.

## 1.1 Goals

- Implement a working LKM that loads and unloads cleanly on kernel 6.8.
- Enumerate all running processes using kernel-internal data structures.
- Detect active `ptrace` attachment on any process.
- Expose results to user space through the `/proc` filesystem.
- Emit structured, parseable alert messages to the kernel log.

## 1.2 Non-Goals

- KRIES does not enforce policy. It does not kill, suspend, or modify any process.
- KRIES does not use kernel function hooking (kprobes, ftrace) or the Linux Security Module (LSM) framework.
- KRIES is a monitoring and demonstration tool, not a production security product.

---

# 2. Background

## 2.1 Loadable Kernel Modules

The Linux kernel supports runtime extension through loadable kernel modules — ELF object files compiled against the running kernel's headers and linked into the kernel image at load time via `insmod`. Modules share the kernel's virtual address space and execute at CPU ring 0, the highest privilege level. They are removed cleanly via `rmmod`, which calls the module's registered exit function before freeing its memory.

Every module must declare two callbacks:

- `module_init(fn)` — called when the module loads.
- `module_exit(fn)` — called when the module is removed.

The `MODULE_LICENSE("GPL")` declaration is required for any module that accesses GPL-only exported kernel symbols. Without it, the kernel refuses to resolve those symbols at load time.

## 2.2 The task_struct and Process Table

Every process and thread in Linux is represented by a `task_struct` — a large kernel data structure defined in `<linux/sched.h>`. It contains the process's PID, name (`comm`, a 16-byte array), credentials, memory descriptor, scheduling state, and debugging flags. All `task_struct` instances are linked in a circular doubly-linked list traversable via the `for_each_process()` macro, which visits one thread-group leader per process.

The process list is protected by RCU (Read-Copy-Update). Readers must acquire an RCU read-side lock (`rcu_read_lock()`) for the duration of any traversal. This lock is extremely lightweight — it disables preemption on the current CPU rather than acquiring a traditional mutex — but it prohibits sleeping or blocking within the critical section.

## 2.3 ptrace and the PT_PTRACED Flag

`ptrace(2)` is the Linux system call underlying all user-space debuggers including GDB and strace. When a tracer attaches to a target process, the kernel sets the `PT_PTRACED` bit (value `0x1`) in the target's `task_struct.ptrace` field via `__ptrace_link()`. This flag is cleared by `__ptrace_unlink()` when the tracer detaches.

Because `task_struct` resides in kernel memory, the `ptrace` field cannot be read, modified, or cleared by any user-space process. Reading it from kernel space therefore provides a tamper-resistant detection signal: if `PT_PTRACED` is set, a tracer is attached.

## 2.4 The /proc Virtual Filesystem

`/proc` is a memory-only virtual filesystem that exposes kernel state to user space. Each entry under `/proc` is backed by kernel callbacks rather than disk data. The `seq_file` API, introduced in kernel 3.10, provides a safe and memory-efficient mechanism for implementing readable `/proc` files. It handles buffer allocation, pagination, and re-reading automatically. The `proc_ops` structure (replacing `file_operations` for `/proc` files as of kernel 5.6) registers the open, read, seek, and release callbacks for a given entry.

---

# 3. System Architecture

KRIES is composed of four source modules with clearly separated responsibilities:

| File | Responsibility |
|---|---|
| `kries_main.c` | Module entry and exit; orchestrates subsystem initialisation |
| `kries_process.c` | Process enumeration; `kries_is_traced()` detection primitive |
| `kries_detect.c` | Detection engine; applies rules, emits structured alerts |
| `kries_proc.c` | `/proc/kries` interface; live report for user-space readers |
| `kries_log.h` | Unified `printk` wrappers: `KRIES_INFO`, `KRIES_WARN`, `KRIES_ALERT` |

The execution flow on module load is linear:

```
insmod kries.ko
    └── kries_init()
            ├── kries_scan_processes()   // enumerate + log all processes
            ├── kries_run_scan()         // apply detection rules, emit alerts
            └── kries_proc_init()        // register /proc/kries
```

On unload:

```
rmmod kries
    └── kries_exit()
            ├── kries_proc_exit()        // remove /proc/kries FIRST
            └── log unload message
```

The `/proc` entry must be removed before the module is freed. Failing to do so leaves a function pointer registered in the proc tree that points to freed memory; the next `cat /proc/kries` would cause a kernel panic.

## 3.1 Logging Infrastructure

All KRIES output passes through three macros defined in `kries_log.h`:

```c
KRIES_INFO(fmt, ...)   // KERN_INFO    — [KRIES]
KRIES_WARN(fmt, ...)   // KERN_WARNING — [KRIES][WARN]
KRIES_ALERT(fmt, ...)  // KERN_ALERT   — [KRIES][ALERT]
```

The `[KRIES]` prefix on every line makes all output trivially grep-able in `dmesg`. Alert lines use `key=value` format so they are parseable by log aggregation tools without post-processing.

---

# 4. Implementation

## 4.1 Process Enumeration

`kries_scan_processes()` in `kries_process.c` iterates over the kernel process list:

```c
rcu_read_lock();
for_each_process(task) {
    KRIES_INFO("pid=%-6d  name=%-16s  ppid=%d",
               task->pid, task->comm, task->parent->pid);
}
rcu_read_unlock();
```

`for_each_process()` is a macro that walks the circular `task_struct` list. `task->comm` is a 16-byte null-terminated character array holding the truncated executable name. `task->parent->pid` is safe to dereference under the RCU read lock because a process's parent pointer remains valid for the lifetime of both processes.

## 4.2 ptrace Detection

`kries_is_traced()` implements the detection primitive:

```c
int kries_is_traced(struct task_struct *task)
{
    return (task->ptrace & PT_PTRACED) ? 1 : 0;
}
```

This single bitwise AND is the complete detection mechanism. `PT_PTRACED` is defined as `0x1` in `<linux/ptrace.h>`. Any non-zero result confirms an active tracer.

## 4.3 Detection Engine

`kries_run_scan()` in `kries_detect.c` drives the detection loop:

```c
rcu_read_lock();
for_each_process(task) {
    if (rule_ptrace(task)) {
        KRIES_ALERT("type=PTRACE_DETECTED  pid=%-6d  name=%-16s  "
                    "ppid=%-6d  ptrace_flags=0x%x",
                    task->pid, task->comm,
                    task->parent->pid, task->ptrace);
        alerts++;
    }
}
rcu_read_unlock();
```

The engine is designed for extension: adding a new rule requires writing one `static int rule_*(struct task_struct *)` function and calling it inside the loop. No other files need to change.

## 4.4 /proc Interface

`kries_proc_init()` registers `/proc/kries` using `proc_create()` with `0444` permissions (world-readable). The `seq_file` callback `kries_proc_show()` iterates the process list and writes a formatted table:

```
KRIES — Kernel Runtime Integrity Report
========================================

PID       NAME              PPID      FLAGS
--------  ----------------  --------  -------
1         systemd           0
2         kthreadd          0
1204      bash              1203
2847      sleep             1201      [TRACED]
```

Processes with `PT_PTRACED` set are marked `[TRACED]`. The report is generated fresh on every `read()` call — there is no caching.

---

# 5. Security Considerations

## 5.1 Privilege Requirements

Loading KRIES requires `CAP_SYS_MODULE`. This is an appropriate gate — a module with ring-0 access to all process state must not be loadable by unprivileged users.

## 5.2 Secure Boot

On systems with UEFI Secure Boot enabled, unsigned kernel modules are rejected at load time. KRIES as built is unsigned. For deployment, the module must be signed with a Machine Owner Key (MOK) enrolled via `mokutil`, or Secure Boot must be disabled. The `sign-file` tool included with kernel headers can sign `kries.ko` with a local key.

## 5.3 Read-Only Design

KRIES does not write to any kernel data structure. It is a passive observer. This eliminates an entire class of potential bugs — any code that writes to shared kernel state must handle concurrent access, ordering guarantees, and rollback on error. KRIES avoids all of this by never modifying anything it reads.

## 5.4 /proc Permissions

`/proc/kries` is created world-readable (`0444`). This makes the process table — including which processes are being debugged — visible to any local user. In a hardened deployment this should be `0400` (root-readable only) to prevent information disclosure.

---

# 6. Limitations

## 6.1 Same-Privilege Monitoring

KRIES and any kernel rootkit it might detect operate at the same privilege level. A sufficiently capable rootkit can clear the `PT_PTRACED` flag before KRIES reads it, manipulate the process list to hide entries, or hook the functions KRIES calls to return falsified data. This is a fundamental limitation of in-kernel monitoring: the monitor and the threat share the same trust boundary. Hardware-assisted solutions (hypervisor-based monitoring, remote attestation) address this at the cost of significantly greater complexity.

## 6.2 Snapshot Detection

KRIES performs a point-in-time scan at module load. Threats that appear, act, and disappear between scans are not detected. Continuous monitoring would require a persistent kernel thread (`kthread_create()`) or a periodic timer (`timer_list`), introducing CPU overhead and scheduling complexity that are outside the scope of this implementation.

## 6.3 False Positives

Every process under a legitimate debugger — a developer running GDB, a tracing tool using strace — will trigger the `PTRACE_DETECTED` alert. KRIES logs all cases without filtering. A production deployment would require a whitelist of authorised tracer credentials or process names.

## 6.4 API Stability

`task_struct` is an internal kernel data structure. Its layout can change between kernel versions. KRIES must be recompiled against the headers for each target kernel. Fields referenced here (`ptrace`, `comm`, `pid`, `parent`) have been stable across the 5.x and 6.x series but carry no long-term stability guarantee.

---

# 7. Conclusion

KRIES demonstrates that host-based kernel integrity monitoring is achievable within a few hundred lines of C using stable, well-documented Linux kernel APIs. By operating at ring 0, KRIES reads process state that is authoritative and inaccessible to user-space monitors. The `PT_PTRACED` detection mechanism is reliable, lightweight, and resistant to user-space manipulation. The `/proc` interface provides a zero-dependency reporting channel readable by any existing monitoring pipeline.

The system is deliberately constrained in scope. It does not enforce policy, does not hook kernel functions, and does not attempt to detect kernel-level rootkits that share its privilege level. These constraints make it safe, auditable, and a sound foundation for further research into kernel security instrumentation.

---

# References

1. Corbet, J., Rubini, A., & Kroah-Hartman, G. (2005). *Linux Device Drivers, 3rd Edition*. O'Reilly Media. https://lwn.net/Kernel/LDD3/

2. Love, R. (2010). *Linux Kernel Development, 3rd Edition*. Addison-Wesley Professional.

3. The Linux Kernel documentation project. *The Linux Kernel API*. https://www.kernel.org/doc/html/latest/

4. McKenney, P. E. (2007). *What is RCU, Fundamentally?* LWN.net. https://lwn.net/Articles/262464/

5. Kerrisk, M. (2010). *The Linux Programming Interface*. No Starch Press.

6. Linux Kernel source — `include/linux/sched.h`, `include/linux/ptrace.h`, `include/linux/proc_fs.h`. https://elixir.bootlin.com/linux/v6.8/source

7. The Linux kernel contributors. *ptrace(2) man page*. https://man7.org/linux/man-pages/man2/ptrace.2.html
