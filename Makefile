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
#
# IMPORTANT: The main source file is kries_main.c (not kries.c).
# kries.c cannot be used because kbuild reserves kries.o as the name
# of the final linked module object — using it as a source object too
# causes a silent build collision where the composite object overwrites
# the compiled source object, resulting in "Invalid module format" on insmod.
kries-objs := kries_main.o kries_process.o kries_detect.o kries_proc.o

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
