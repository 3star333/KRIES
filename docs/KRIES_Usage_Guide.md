# KRIES — Build, Usage & Testing Guide

**Kernel Runtime Integrity Enforcement System**  
Version 0.7 | Linux Kernel 5.x+ | Ubuntu 20.04 / 22.04 / 24.04

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Build](#2-build)
3. [Load & Unload](#3-load--unload)
4. [Reading Output](#4-reading-output)
5. [Using the /proc Interface](#5-using-the-proc-interface)
6. [Testing Each Feature](#6-testing-each-feature)
   - [Test 1 — Module loads and unloads cleanly](#test-1--module-loads-and-unloads-cleanly)
   - [Test 2 — Process list is scanned](#test-2--process-list-is-scanned)
   - [Test 3 — Debugged process is detected](#test-3--debugged-process-is-detected)
   - [Test 4 — Kernel module list is scanned](#test-4--kernel-module-list-is-scanned)
   - [Test 5 — Suspicious module is flagged](#test-5--suspicious-module-is-flagged)
   - [Test 6 — /proc/kries is readable](#test-6--prockries-is-readable)
   - [Test 7 — Detection engine generates structured alerts](#test-7--detection-engine-generates-structured-alerts)
7. [Troubleshooting](#7-troubleshooting)
8. [Quick Reference](#8-quick-reference)

---

## 1. Prerequisites

> ⚠️ **macOS users:** Kernel modules run only on Linux. Use a Ubuntu VM (VirtualBox, VMware, UTM, or Multipass). All commands below are run inside that VM.

### Install build dependencies

```bash
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r) git
```

### Verify kernel headers are present

```bash
ls /lib/modules/$(uname -r)/build
# Should list: Makefile, include/, scripts/, etc.
# If the directory is missing, re-run the apt install above.
```

### Check your kernel version (must be 5.x or newer)

```bash
uname -r
# Example output: 5.15.0-91-generic
```

---

## 2. Build

### Clone the repository

```bash
git clone https://github.com/3star333/KRIES.git
cd KRIES
```

### Compile the module

```bash
make
```

#### Expected output

```
make -C /lib/modules/5.15.0-91-generic/build M=/home/user/KRIES modules
make[1]: Entering directory '/usr/src/linux-headers-5.15.0-91-generic'
  CC [M]  /home/user/KRIES/kries.o
  CC [M]  /home/user/KRIES/kries_process.o
  CC [M]  /home/user/KRIES/kries_modules.o
  CC [M]  /home/user/KRIES/kries_proc.o
  CC [M]  /home/user/KRIES/kries_detect.o
  LD [M]  /home/user/KRIES/kries.o
  MODPOST /home/user/KRIES/Module.symvers
  CC [M]  /home/user/KRIES/kries.mod.o
  LD [M]  /home/user/KRIES/kries.ko
make[1]: Leaving directory '/usr/src/linux-headers-5.15.0-91-generic'
```

The file you care about is **`kries.ko`**. Confirm it was created:

```bash
ls -lh kries.ko
# -rw-r--r-- 1 user user 47K Apr 28 12:00 kries.ko
```

### Clean build artifacts

```bash
make clean
```

---

## 3. Load & Unload

### Load the module

```bash
sudo insmod kries.ko
```

### Confirm it is loaded

```bash
lsmod | grep kries
# kries    32768  0
```

### Unload the module

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

All KRIES output goes to the **kernel ring buffer**, readable via `dmesg`.

### See all KRIES messages since last load

```bash
sudo dmesg | grep KRIES
```

### Follow output in real time (useful during testing)

```bash
sudo dmesg -W | grep KRIES
# Leave this running in one terminal, run insmod in another.
```

### See only alerts

```bash
sudo dmesg | grep '\[ALERT\]'
```

### Clear the ring buffer before a test (cleaner output)

```bash
sudo dmesg -C
sudo insmod kries.ko
sudo dmesg | grep KRIES
```

### Log level prefixes

| Prefix | Meaning |
|---|---|
| `[KRIES]` | Informational — normal operation |
| `[KRIES][WARN]` | Suspicious — warrants attention |
| `[KRIES][ALERT]` | High severity — confirmed detection |

---

## 5. Using the /proc Interface

Once the module is loaded, `/proc/kries` provides a live snapshot report readable by any user.

### Read the full report

```bash
cat /proc/kries
```

### Filter for traced processes only

```bash
cat /proc/kries | grep TRACED
```

### Filter for non-LIVE modules

```bash
cat /proc/kries | grep '\[!\]'
```

### Watch the report refresh (every 2 seconds)

```bash
watch -n 2 cat /proc/kries
```

### Save a snapshot to a file

```bash
cat /proc/kries > kries_snapshot_$(date +%Y%m%d_%H%M%S).txt
```

---

## 6. Testing Each Feature

Each test follows the same structure:
1. **Setup** — create the condition to detect
2. **Trigger** — load or re-check KRIES
3. **Verify** — confirm expected output appears

---

### Test 1 — Module loads and unloads cleanly

**What it tests:** Phase 1 (module skeleton) + Phase 2 (logging macros)

```bash
# Clear buffer for clean output
sudo dmesg -C

# Load
sudo insmod kries.ko

# Check load messages
sudo dmesg | grep KRIES
```

**Expected:**
```
[KRIES] Module loaded successfully.
[KRIES] Kernel Runtime Integrity Enforcement System v0.7
[KRIES] Ready.
```

```bash
# Unload
sudo rmmod kries

# Check unload message
sudo dmesg | grep KRIES | tail -3
```

**Expected:**
```
[KRIES] /proc/kries removed.
[KRIES] Module unloaded. Goodbye.
```

✅ **Pass:** Both messages appear with the `[KRIES]` prefix.

---

### Test 2 — Process list is scanned

**What it tests:** Phase 3 (process monitoring)

```bash
sudo dmesg -C
sudo insmod kries.ko
sudo dmesg | grep KRIES | grep "pid="
```

**Expected (sample — your PIDs will differ):**
```
[KRIES] pid=1       name=systemd          ppid=0
[KRIES] pid=2       name=kthreadd         ppid=0
[KRIES] pid=892     name=sshd             ppid=1
[KRIES] pid=1204    name=bash             ppid=1203
...
[KRIES] --- Process Scan Complete: 87 processes found ---
```

**Verify a specific known process appears:**
```bash
# Check that your shell's PID is in the scan
echo "My shell PID: $$"
sudo dmesg | grep "pid=$$"
```

✅ **Pass:** Your shell's PID appears in the scan output.

---

### Test 3 — Debugged process is detected

**What it tests:** Phase 4 (debug detection) + Phase 7 (detection engine alert)

**Setup — open two terminals:**

```bash
# Terminal 1: start a traceable process
sleep 999 &
SLEEP_PID=$!
echo "Sleep PID: $SLEEP_PID"

# Attach strace to it (this sets PT_PTRACED on the sleep process)
sudo strace -p $SLEEP_PID &
```

```bash
# Terminal 2: reload KRIES and check for the alert
sudo rmmod kries 2>/dev/null; sudo dmesg -C
sudo insmod kries.ko
sudo dmesg | grep ALERT
```

**Expected:**
```
[KRIES][ALERT] type=PTRACE_DETECTED   pid=2847    name=sleep             ppid=1801   ptrace_flags=0x1
[KRIES][ALERT] Scan complete — 1 alert(s) generated. Review above.
```

Also verify it appears in the `/proc` report:
```bash
cat /proc/kries | grep TRACED
# sleep            2847     1801     [TRACED]
```

**Cleanup:**
```bash
sudo kill $SLEEP_PID
```

✅ **Pass:** `PTRACE_DETECTED` alert appears with the correct PID and `ptrace_flags=0x1`.

---

### Test 4 — Kernel module list is scanned

**What it tests:** Phase 5 (module monitoring)

```bash
sudo dmesg -C
sudo insmod kries.ko
sudo dmesg | grep KRIES | grep "name="
```

**Expected (sample):**
```
[KRIES] name=nfnetlink               state=LIVE
[KRIES] name=xfrm_user               state=LIVE
[KRIES] name=ext4                    state=LIVE
...
[KRIES] --- Module Scan Complete: 64 modules found ---
```

**Cross-check with lsmod:**
```bash
# Count modules lsmod sees (minus header line)
lsmod | tail -n +2 | wc -l

# Should be close to the number KRIES reported
# (Small difference is expected: KRIES excludes itself)
```

✅ **Pass:** Module names in `dmesg` output match entries in `lsmod`.

---

### Test 5 — Suspicious module is flagged

**What it tests:** Phase 7 `rule_is_suspicious_module()` — name heuristic

This test creates a **dummy** module with a suspicious name to trigger the detection rule. No actual malicious code is involved.

**Create the test module:**

```bash
mkdir /tmp/hide_test && cd /tmp/hide_test

cat > hide_test.c << 'EOF'
#include <linux/module.h>
#include <linux/kernel.h>
MODULE_LICENSE("GPL");
static int __init hide_test_init(void) { return 0; }
static void __exit hide_test_exit(void) {}
module_init(hide_test_init);
module_exit(hide_test_exit);
EOF

cat > Makefile << 'EOF'
obj-m += hide_test.o
KDIR := /lib/modules/$(shell uname -r)/build
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
EOF

make
```

**Load the suspicious module, then run KRIES:**

```bash
sudo insmod hide_test.ko
lsmod | grep hide_test   # Confirm it's loaded

cd ~/KRIES
sudo rmmod kries 2>/dev/null; sudo dmesg -C
sudo insmod kries.ko
sudo dmesg | grep ALERT
```

**Expected:**
```
[KRIES][ALERT] type=SUSPICIOUS_MODULE  name=hide_test                   state=LIVE        reason=name_prefix:hide_
[KRIES][ALERT] Scan complete — 1 alert(s) generated. Review above.
```

**Cleanup:**
```bash
sudo rmmod hide_test
cd ~
rm -rf /tmp/hide_test
```

✅ **Pass:** `SUSPICIOUS_MODULE` alert fires with `reason=name_prefix:hide_`.

---

### Test 6 — /proc/kries is readable

**What it tests:** Phase 6 (/proc interface)

```bash
sudo insmod kries.ko   # skip if already loaded

# Basic read
cat /proc/kries
```

**Expected structure:**
```
================================================
  KRIES - Kernel Runtime Integrity Report
================================================

[PROCESSES]
PID      NAME             PPID     FLAGS
-------- ---------------- -------- --------
1        systemd          0
2        kthreadd         0
...

[KERNEL MODULES]
NAME                     STATE
------------------------ ---------
ext4                     LIVE
dm_mod                   LIVE
...

================================================
  End of Report
================================================
```

**Verify the entry exists in /proc:**
```bash
ls -la /proc/kries
# -r--r--r-- 1 root root 0 Apr 28 12:00 /proc/kries
```

**Verify it disappears after unload:**
```bash
sudo rmmod kries
cat /proc/kries
# cat: /proc/kries: No such file or directory
```

✅ **Pass:** Report renders correctly and the entry vanishes cleanly on unload.

---

### Test 7 — Detection engine generates structured alerts

**What it tests:** Phase 7 (detection engine) — combined scenario

This test combines Tests 3 and 5 to confirm the engine handles multiple simultaneous detections and produces the correct total count.

```bash
# Setup: load suspicious module + trace a process
mkdir /tmp/hook_test && cd /tmp/hook_test
cat > hook_test.c << 'EOF'
#include <linux/module.h>
MODULE_LICENSE("GPL");
static int __init hi(void) { return 0; }
static void __exit bye(void) {}
module_init(hi); module_exit(bye);
EOF
cat > Makefile << 'EOF'
obj-m += hook_test.o
KDIR := /lib/modules/$(shell uname -r)/build
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
EOF
make && sudo insmod hook_test.ko

# Trace a process
sleep 999 &
SLEEP_PID=$!
sudo strace -p $SLEEP_PID &

# Run KRIES scan
cd ~/KRIES
sudo rmmod kries 2>/dev/null; sudo dmesg -C
sudo insmod kries.ko
sudo dmesg | grep -E 'ALERT|Scan complete'
```

**Expected:**
```
[KRIES][ALERT] type=PTRACE_DETECTED    pid=XXXX  name=sleep  ...
[KRIES][ALERT] type=SUSPICIOUS_MODULE  name=hook_test  ...  reason=name_prefix:hook_
[KRIES][ALERT] Scan complete — 2 alert(s) generated. Review above.
```

**Cleanup:**
```bash
sudo kill $SLEEP_PID
sudo rmmod hook_test
rm -rf /tmp/hook_test
```

✅ **Pass:** Both alert types appear and the summary line shows `2 alert(s)`.

---

## 7. Troubleshooting

### Build errors

| Error | Cause | Fix |
|---|---|---|
| `No rule to make target 'modules'` | Kernel headers missing | `sudo apt install linux-headers-$(uname -r)` |
| `fatal error: linux/module.h: No such file` | Headers not found | Same as above |
| `modpost: missing MODULE_LICENSE` | Removed license declaration | Ensure `MODULE_LICENSE("GPL")` is in `kries.c` |
| `Skipping BTF generation` | pahole not installed (harmless warning) | Ignore, or `sudo apt install dwarves` |

### Load errors

| Error | Cause | Fix |
|---|---|---|
| `insmod: ERROR: could not insert module kries.ko: Invalid module format` | Module compiled for different kernel | Recompile: `make clean && make` |
| `Required key not available` | Secure Boot blocking unsigned modules | Disable Secure Boot in BIOS/UEFI, or sign the module with MOK |
| `insmod: ERROR: could not insert module: File exists` | Module already loaded | `sudo rmmod kries` first |
| `Unknown symbol in module` | GPL symbol used without GPL license | Confirm `MODULE_LICENSE("GPL")` is present |

### Runtime issues

| Issue | Fix |
|---|---|
| No output in `dmesg` | Run `sudo dmesg` — you may need root; also try `sudo dmesg -T` |
| `/proc/kries` not found | Module not loaded — check `lsmod \| grep kries` |
| Kernel panic on `rmmod` | `/proc/kries` not cleaned up — check `kries_proc_exit()` is called in `kries_exit()` |
| Alert fires for every GDB session | Expected — KRIES detects all traced processes; whitelist legitimate debuggers if needed |
| Process count seems low | `for_each_process` visits thread group leaders only, not individual threads |

---

## 8. Quick Reference

```bash
# Build
make

# Load
sudo insmod kries.ko

# Read kernel log output
sudo dmesg | grep KRIES

# Read only alerts
sudo dmesg | grep '\[ALERT\]'

# Read /proc report
cat /proc/kries

# Watch /proc report live
watch -n 2 cat /proc/kries

# Unload
sudo rmmod kries

# Clean build artifacts
make clean

# Full reload (clean dmesg + reload)
sudo rmmod kries 2>/dev/null; sudo dmesg -C && sudo insmod kries.ko && sudo dmesg | grep KRIES
```
