# Makefile — KRIES Kernel Module Build System
#
# The kernel build system (kbuild) requires a specific Makefile structure.
# We delegate the actual build to the kernel's own Makefile infrastructure.
#
# `obj-m` tells kbuild: build this as a loadable module (.ko file)
# `$(KDIR)` points to the running kernel's build headers/source
# `$(PWD)` is the current directory containing our source

# All object files that make up the kries module.
# kbuild links these together into a single kries.ko file.
# Add new .o entries here as each phase introduces a new .c file.
kries-objs := kries.o kries_process.o kries_modules.o kries_proc.o kries_detect.o

# obj-m tells kbuild: build 'kries' as a loadable module (.ko)
# Note: the name here must match the kries-objs variable prefix above.
obj-m += kries.o

# Path to the running kernel's build directory.
# This contains the Makefile and headers needed to build modules
# against the currently running kernel version.
KDIR := /lib/modules/$(shell uname -r)/build

# Current working directory (where our source files live)
PWD := $(shell pwd)

# Default target: build the module
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

# Clean target: remove all build artifacts
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
