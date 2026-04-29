# KRIES — Kernel Runtime Integrity Enforcement System

A Linux loadable kernel module (LKM) that monitors process lifecycle activity, detects unauthorized debugging, observes kernel module loading, and reports suspicious behaviour via kernel logs and the `/proc` interface.

**Language:** C | **Target:** Ubuntu Linux 5.x+ | **Version:** 0.7

---

## Features

| Phase | Feature |
|---|---|
| 1 | Loadable kernel module skeleton |
| 2 | Structured logging (`KRIES_INFO`, `KRIES_WARN`, `KRIES_ALERT`) |
| 3 | Process enumeration — PID, name, PPID for every running process |
| 4 | Debug detection — flags processes under `ptrace` (GDB, strace) |
| 5 | Kernel module enumeration — name and state of all loaded modules |
| 6 | `/proc/kries` interface — live report readable from user space |
| 7 | Detection engine — rule-based scanning with structured alert output |

---

## Quick Start

```bash
# 1. Install build dependencies (Ubuntu)
sudo apt install -y build-essential linux-headers-$(uname -r)

# 2. Build
make

# 3. Load
sudo insmod kries.ko

# 4. Read output
sudo dmesg | grep KRIES
cat /proc/kries

# 5. Unload
sudo rmmod kries
```

> ⚠️ Must be run on a Linux machine. macOS is not supported for execution (only editing).

---

## File Structure

```
KRIES/
├── kries_main.c        — Module entry point (init/exit)
├── kries_log.h         — Logging macros
├── kries_process.c/h   — Process enumeration + ptrace detection
├── kries_modules.c/h   — Kernel module enumeration
├── kries_proc.c/h      — /proc/kries interface
├── kries_detect.c/h    — Detection engine (rule functions + scan)
├── Makefile
└── docs/
    ├── KRIES_Technical_Paper.md   — System design & architecture
    └── KRIES_Usage_Guide.md       — Build, usage & testing guide
```

---

## Documentation

| Document | Description |
|---|---|
| [`docs/KRIES_Usage_Guide.md`](docs/KRIES_Usage_Guide.md) | How to build, load, use, and test KRIES — includes 7 concrete test cases |
| [`docs/KRIES_Technical_Paper.md`](docs/KRIES_Technical_Paper.md) | Full technical paper — architecture, threat model, kernel concepts, limitations |

---

## Alert Output Format

```
[KRIES][ALERT] type=PTRACE_DETECTED   pid=2847    name=sleep   ppid=1801  ptrace_flags=0x1
[KRIES][ALERT] type=SUSPICIOUS_MODULE name=hide_eth0  state=LIVE  reason=name_prefix:hide_
[KRIES][ALERT] Scan complete — 2 alert(s) generated. Review above.
```

---

## License

GPL-2.0 — required for access to GPL-only exported kernel symbols.
