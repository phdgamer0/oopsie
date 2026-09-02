# Oopsie - Linux Kernel (Ring-0) Undo Engine

This branch contains the Ring-0 (Kernel Space) implementation of the `oopsie` undo utility.

## Architecture

This approach operates at raw CPU speeds by hooking directly into the Linux Virtual File System (VFS). It avoids user-space context switches entirely.

### Folder Structure

```
oopsie/
├── Kbuild                 # Linux Kernel build configuration
├── Makefile               # Wrappers for building and inserting the module (insmod/rmmod)
├── include/
│   ├── oopsie_kernel.h    # Kernel-space structs, per-CPU macros, and VFS hooks
│   └── oopsie_wal.h       # Shared WAL structs (used by both kernel and user-space)
└── src/
    ├── kernel_mod.c       # Main LKM entry point, VFS/eBPF hook registration
    ├── oopsiefs.c         # Stacked filesystem implementation
    └── main.c             # User-space CLI to read WAL and trigger undo ioctls
```

## Setup & Deployment
(To be implemented)

*Note: This branch runs with root privileges and interacts directly with kernel memory.*
