# KRIES — Build, Usage & Testing Guide

**Kernel Runtime Integrity Enforcement System**  
Version 1.0 | Linux Kernel 5.6+ (tested on 6.8) | Ubuntu 22.04 / 24.04

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Build](#2-build)
3. [Load & Unload](#3-load--unload)
4. [Reading Output](#4-reading-output)
5. [Using the /proc Interface](#5-using-the-proc-interface)
6. [Test Cases](#6-test-cases)
   - [Test 1 — Module loads and unloads cleanly](#test-1--module-loads-and-unloads-cleanly)
   - [Test 2 — Process list is scanned on load](#test-2--process-list-is-scanned-on-load)
   - [Test 3 — Debugged process triggers PTRACE_DETECTED alert](#test-3--debugged-process-triggers-ptrace_detected-alert)
   - [Test 4 — /proc/kries renders live report](#test-4--prockries-renders-live-report)
   - [Test 5 — /proc/kries marks traced process as TRACED](#test-5--prockries-marks-traced-process-as-traced)
   - [Test 6 — /proc/kries disappears cleanly on unload](#test-6--prockries-disappears-cleanly-on-unload)
7. [Troubleshooting](#7-troubleshooting)
8. [Quick Reference](#8-quick-reference)

---

## 1. Prerequisites

> ⚠️ **macOS users:** Kernel modules run only on Linux. Use a Ubuntu VM
> (VirtualBox, VMware, UTM, or Multipass). All commands below run inside that VM.

### Install build dependencies

```bash
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r) git
```

### Verify kernel headers are present

```bash
ls /lib/modules/$(uname -r)/build
# Should list: Makefile, include/, scripts/, etc.
```

### Check kernel version (must be 5.6 or newer)

```bash
uname -r
# Example: 6.8.0-41-generic
```

---

## 2. Build

### Clone the repository

```bash
git clone https://github.com/3star333/KRIES.git
cd KRIES
```

### Compile

```bash
make
```

#### Expected output

```
make -C /lib/modules/6.8.0-41-generic/build M=/home/user/KRIES modules
  CC [M]  /home/user/KRIES/kries_main.o
  CC [M]  /home/user/KRIES/kries_process.o
  CC [M]  /home/user/KRIES/kries_detect.o
  CC [M]  /home/user/KRIES/kries_proc.o
  LD [M]  /home/user/KRIES/kries.o
  MODPOST /home/user/KRIES/Module.symvers
  LD [M]  /home/user/KRIES/kries.ko
```

Confirm `kries.ko` was created:

```bash
ls -lh kries.ko
```

### Clean build artifacts

```bash
make clean
```

---

## 3. Load & Unload

### Load

```bash
sudo insmod kries.ko
```

### Confirm it is loaded

```bash
lsmod | grep kries
# kries    32768  0
```

### Unload

```bash
sudo rmmod kries
```

### Confirm it is gone

```bash
lsmod | grep kries
# (no output)
```

---

## 4. Reading Output

All KRIES output goes to the kernel ring buffer, readable via `dmesg`.

### See all KRIES messages

```bash
sudo dmesg | grep KRIES
```

### Follow output live (run in a separate terminal before loading)

```bash
sudo dmesg -W | grep KRIES
```

### See only alerts

```bash
sudo dmesg | grep '\[ALERT\]'
```

### Clear ring buffer for a clean test run

```bash
sudo dmesg -C
sudo insmod kries.ko
sudo dmesg | grep KRIES
```

### Log level prefixes

| Prefix | `printk` level | Meaning |
|---|---|---|
| `[KRIES]` | `KERN_INFO` | Normal operational message |
| `[KRIES][WARN]` | `KERN_WARNING` | Suspicious but non-critical |
| `[KRIES][ALERT]` | `KERN_ALERT` | Confirmed detection |

---

## 5. Using the /proc Interface

Once loaded, `/proc/kries` provides a live process integrity report.

### Read the full report

```bash
cat /proc/kries
```

### Filter for traced processes only

```bash
cat /proc/kries | grep '\[TRACED\]'
```

### Refresh the report every 2 seconds

```bash
watch -n 2 cat /proc/kries
```

### Save a timestamped snapshot

```bash
cat /proc/kries > kries_$(date +%Y%m%d_%H%M%S).txt
```

---

## 6. Test Cases

Each test follows the pattern: **Setup → Trigger → Verify**.

---

### Test 1 — Module loads and unloads cleanly

**What it validates:** The module initialises all subsystems without error
and tears down cleanly, including removing `/proc/kries`.

```bash
# Step 1: clear the ring buffer
sudo dmesg -C

# Step 2: load
sudo insmod kries.ko

# Step 3: check load messages
sudo dmesg | grep KRIES
```

**Expected load output:**
```
[KRIES] KRIES v1.0 loaded.
[KRIES] --- process scan start ---
[KRIES] pid=1       name=systemd          ppid=0
...
[KRIES] --- process scan complete: N processes ---
[KRIES] --- detection scan start ---
[KRIES] --- scan complete: no threats detected ---
[KRIES] /proc/kries created.
[KRIES] Ready. Read state: cat /proc/kries
```

```bash
# Step 4: unload
sudo rmmod kries

# Step 5: check unload messages
sudo dmesg | grep KRIES | tail -3
```

**Expected unload output:**
```
[KRIES] /proc/kries removed.
[KRIES] KRIES unloaded.
```

✅ **Pass:** All expected lines appear; no `Oops` or `BUG` in dmesg.

---

### Test 2 — Process list is scanned on load

**What it validates:** `kries_scan_processes()` correctly iterates all
running processes and logs PID, name, and PPID for each one.

```bash
# Record your shell's PID before loading
echo "Shell PID: $$"

sudo dmesg -C
sudo insmod kries.ko
sudo dmesg | grep 'pid='
```

**Expected:** A line for every running process, including your shell:

```
[KRIES] pid=1       name=systemd          ppid=0
[KRIES] pid=2       name=kthreadd         ppid=0
[KRIES] pid=1204    name=bash             ppid=1203
...
[KRIES] --- process scan complete: 87 processes ---
```

**Cross-check:** Verify your shell's PID appears:

```bash
sudo dmesg | grep "pid=$$"
```

**Compare count with ps:**

```bash
# Count unique PIDs visible to ps (should be close to KRIES count)
ps -e --no-headers | wc -l
```

✅ **Pass:** Your shell's PID is present; count roughly matches `ps`.

---

### Test 3 — Debugged process triggers PTRACE_DETECTED alert

**What it validates:** `kries_is_traced()` correctly reads `PT_PTRACED`
from `task->ptrace` and the detection engine emits a structured alert.

**Open two terminals:**

```bash
# Terminal 1 — start a background process and attach a tracer
sleep 999 &
SLEEP_PID=$!
echo "Tracing PID: $SLEEP_PID"
sudo strace -p $SLEEP_PID 2>/dev/null &
sleep 1    # give strace time to attach
```

```bash
# Terminal 2 — reload KRIES and check for the alert
sudo rmmod kries 2>/dev/null
sudo dmesg -C
sudo insmod kries.ko
sudo dmesg | grep ALERT
```

**Expected:**
```
[KRIES][ALERT] type=PTRACE_DETECTED  pid=2847    name=sleep             ppid=1801   ptrace_flags=0x1
[KRIES][ALERT] --- scan complete: 1 alert(s) ---
```

Key fields to verify:
- `type=PTRACE_DETECTED` — correct rule fired
- `pid=<N>` — matches `$SLEEP_PID`
- `ptrace_flags=0x1` — `PT_PTRACED` bit is set

**Cleanup:**
```bash
sudo kill $SLEEP_PID
```

✅ **Pass:** Alert appears with the correct PID and `ptrace_flags=0x1`.

---

### Test 4 — /proc/kries renders live report

**What it validates:** The `/proc` interface is registered correctly and
outputs a well-formed process table readable from user space.

```bash
# Module must be loaded
sudo insmod kries.ko 2>/dev/null || true

cat /proc/kries
```

**Expected output structure:**
```
KRIES — Kernel Runtime Integrity Report
========================================

PID       NAME              PPID      FLAGS
--------  ----------------  --------  -------
1         systemd           0
2         kthreadd          0
1204      bash              1203
...

========================================
```

**Verify the /proc entry exists and has correct permissions:**

```bash
ls -la /proc/kries
# -r--r--r-- 1 root root 0 Apr 28 12:00 /proc/kries
```

**Verify it is world-readable (no sudo needed):**

```bash
cat /proc/kries | head -5
```

✅ **Pass:** Report renders without error; file is readable without root.

---

### Test 5 — /proc/kries marks traced process as [TRACED]

**What it validates:** The `/proc` report correctly flags processes with
`PT_PTRACED` set with the `[TRACED]` marker in the FLAGS column.

**Setup — open two terminals:**

```bash
# Terminal 1 — create a traced process
sleep 999 &
SLEEP_PID=$!
sudo strace -p $SLEEP_PID 2>/dev/null &
sleep 1
echo "Watching PID: $SLEEP_PID"
```

```bash
# Terminal 2 — read the /proc report (module must already be loaded)
cat /proc/kries | grep '\[TRACED\]'
```

**Expected:**
```
2847      sleep             1801      [TRACED]
```

**Also verify the alert fired in dmesg:**
```bash
sudo dmesg | grep ALERT | tail -5
```

**Cleanup:**
```bash
sudo kill $SLEEP_PID
```

**Verify the [TRACED] flag disappears after the tracer is gone:**
```bash
# Wait a moment, then re-read /proc/kries
sleep 1
cat /proc/kries | grep '\[TRACED\]'
# (no output — flag cleared when strace detached)
```

✅ **Pass:** `[TRACED]` appears while strace is attached; disappears after cleanup.

> **Note:** `/proc/kries` is a live report — each `cat` triggers a fresh
> scan of the process table. The `[TRACED]` flag reflects the current
> kernel state at the moment of reading, not a cached snapshot.

---

### Test 6 — /proc/kries disappears cleanly on unload

**What it validates:** `kries_proc_exit()` is called correctly in
`kries_exit()`, removing the `/proc` entry before module memory is freed.
This is the most safety-critical cleanup step — skipping it causes a
kernel panic on the next read.

```bash
# Step 1: confirm entry exists
ls /proc/kries
# /proc/kries

# Step 2: unload
sudo rmmod kries

# Step 3: confirm entry is gone
ls /proc/kries
# ls: cannot access '/proc/kries': No such file or directory

# Step 4: attempting to read it should fail cleanly (not panic)
cat /proc/kries
# cat: /proc/kries: No such file or directory
```

**Also check dmesg confirms the removal message came before the unload message:**

```bash
sudo dmesg | grep KRIES | tail -4
```

**Expected order:**
```
[KRIES] /proc/kries removed.
[KRIES] KRIES unloaded.
```

✅ **Pass:** `/proc/kries` is gone after unload; kernel remains stable; removal
message appears before the unload message in dmesg.

---

## 7. Troubleshooting

### Build errors

| Error | Cause | Fix |
|---|---|---|
| `No rule to make target 'modules'` | Kernel headers missing | `sudo apt install linux-headers-$(uname -r)` |
| `fatal error: linux/sched.h: No such file` | Headers not found | Same as above |
| `Skipping BTF generation` | `pahole` not installed (harmless) | Ignore, or `sudo apt install dwarves` |
| `modpost: missing MODULE_LICENSE` | License macro removed | Ensure `MODULE_LICENSE("GPL")` is in `kries_main.c` |

### Load errors

| Error | Cause | Fix |
|---|---|---|
| `Invalid module format` | Compiled against wrong kernel | `make clean && make` |
| `Required key not available` | Secure Boot blocking unsigned module | Disable Secure Boot in BIOS/UEFI, or sign with MOK |
| `File exists` | Module already loaded | `sudo rmmod kries` first |
| `Unknown symbol in module` | GPL symbol without GPL license | Confirm `MODULE_LICENSE("GPL")` is present |

### Runtime issues

| Issue | Fix |
|---|---|
| No output in `dmesg` | Try `sudo dmesg` — may need root to read kernel log |
| `/proc/kries` not found | Module not loaded — `lsmod \| grep kries` |
| No `[TRACED]` in /proc despite strace running | Give strace a second to attach before reading — use `sleep 1` between attach and cat |
| Alert fires for your own GDB session | Expected — KRIES detects all traced processes |
| Kernel panic on `rmmod` | `/proc/kries` was not cleaned up — check `kries_proc_exit()` is called in `kries_exit()` |

---

## 8. Quick Reference

```bash
# Build
make

# Load
sudo insmod kries.ko

# Watch kernel log live (run first in a separate terminal)
sudo dmesg -W | grep KRIES

# Read all KRIES messages
sudo dmesg | grep KRIES

# Read only alerts
sudo dmesg | grep '\[ALERT\]'

# Read /proc report
cat /proc/kries

# Filter for traced processes in /proc
cat /proc/kries | grep '\[TRACED\]'

# Refresh /proc report every 2s
watch -n 2 cat /proc/kries

# Unload
sudo rmmod kries

# Clean build
make clean

# Full reload (clear log + reload + show output)
sudo rmmod kries 2>/dev/null; sudo dmesg -C; sudo insmod kries.ko; sudo dmesg | grep KRIES
```
