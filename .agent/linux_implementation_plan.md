# Oopsie Linux Implementation Plan: Ring-3 vs Ring-0

This document explicitly defines the structure and step-by-step daily plan for building the `oopsie` undo utility in pure C, focusing on the Linux ecosystem. No other files or folders will be created outside of these specified boundaries.

---

## Route 1: Ring-3 (User Space) Path
This approach is wildly deployable, does not require `root` to run, and relies on `LD_PRELOAD` to intercept libc calls before they hit the kernel. 

### Folder and File Structure
We will strictly adhere to the following architecture:

```
oopsie/
├── CMakeLists.txt         # Build system configuration (compiles CLI and shim)
├── Makefile               # Convenience wrappers (e.g., make build, make clean)
├── include/
│   ├── oopsie_wal.h       # Binary WAL structures and memory mapping macros
│   ├── oopsie_utils.h     # Cross-platform utility definitions and constants
│   └── linuxHeader.h      # Linux-specific Ring-3 definitions and #defines
└── src/
    ├── main.c             # CLI entry point (undoing actions, parsing the WAL)
    ├── oopsie_wal.c       # WAL operations (mmap allocation, struct appending)
    ├── oopsie_utils.c     # General utility implementations (path caching, fd management)
    └── shim.c             # LD_PRELOAD interceptor hooking libc (open, unlink, rename)
```

**File Responsibilities:**
- **`CMakeLists.txt` / `Makefile`**: Used to compile the `shim.so` shared library and the `oopsie` executable.
- **`oopsie_wal.h` / `oopsie_wal.c`**: Defines the packed `OopsieRecord` struct. Handles the memory mapping of the `.wal` file, calculating pointer offsets, and appending records in O(1) time without string parsing.
- **`linuxHeader.h`**: Stores Linux-specific includes (`<sys/ioctl.h>`, `<linux/fs.h>`) and constants.
- **`shim.c`**: The core workhorse. Hijacks functions like `unlink()`. When a deletion is detected, it executes an `ioctl(FICLONE)` zero-copy backup, logs the event to the WAL via `oopsie_wal.c`, and then allows the original `unlink()` to proceed.
- **`main.c`**: The user interface. It reads the memory-mapped WAL backward to display history. When an undo is requested, it executes the restore safely using `renameat2` and `fdatasync`.
- **`oopsie_utils.c`**: Caches directory file descriptors (`O_PATH`) to eliminate absolute string path parsing during execution.

### Step-by-Step Implementation (1 Day per Step)

* **Day 1: Project Skeleton & The Binary WAL**
  * Establish the folder structure and `CMakeLists.txt`.
  * Define the packed `OopsieRecord` struct in `oopsie_wal.h`.
  * Write `oopsie_wal.c` to `mmap` a `.wal` file into memory and write a function to append a struct to it using pure pointer arithmetic.
* **Day 2: The LD_PRELOAD Shim Foundation**
  * Create `shim.c`. Implement `dlsym(RTLD_NEXT, ...)` wrappers to store the real addresses of `unlink`, `open`, etc.
  * Build `shim.so`. Test it by intercepting a standard `rm` command, printing a debug log, and letting the file delete.
* **Day 3: Zero-Copy Cloning (FICLONE)**
  * Update `shim.c`'s `unlink` hook. Before letting the deletion happen, execute `ioctl(FICLONE)` to create a 0-byte instantaneous metadata clone in the hidden backup directory.
  * Implement the `copy_file_range` fallback for filesystems (like ext4) that do not support cloning.
  * Wire the shim to append the backup metadata to the WAL upon success.
* **Day 4: The CLI Undo Engine (Safety & Atomicity)**
  * Write `main.c` to parse the `mmap`'d WAL backwards and calculate the current filesystem state.
  * Implement the restore function: rename the backup to a `temp_file`, call `fdatasync()`, then `rename()` it atomically to the original path.
  * Lock down the tool against symlink attacks using `openat` with `O_NOFOLLOW | O_CLOEXEC`.
* **Day 5: High-Speed Path Resolution (io_uring)**
  * Move path resolution to `oopsie_utils.c`. Open a file descriptor to the backup directory once at startup.
  * Refactor cloning to use `linkat` and relative file descriptors instead of passing absolute string paths to the kernel.
  * (Optional) Implement `io_uring` to batch hardlink creation during massive folder deletions like `rm -rf node_modules`.

---

## Route 2: Ring-0 (Kernel Space) Path
This approach requires `root` privileges. It abandons user space entirely, avoiding context switches, and executes at raw CPU speeds by hooking directly into the Virtual File System (VFS).

### Folder and File Structure
We will strictly adhere to the following architecture for the kernel module:

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

**File Responsibilities:**
- **`Kbuild` / `Makefile`**: Compiles the `.ko` (Kernel Object) driver file.
- **`oopsie_kernel.h`**: Defines the `__percpu` variables used for lockless ring buffers.
- **`oopsie_wal.h`**: The exact same binary WAL format as Ring-3, allowing the user-space CLI to read the kernel's memory output.
- **`kernel_mod.c`**: The kernel module initialization. It allocates kernel memory, registers the driver, and exposes the ring buffer memory to user space via `debugfs` or a character device.
- **`oopsiefs.c`**: Implements a custom stacked filesystem. Instead of hooking global commands, it acts as a passthrough filesystem that receives raw I/O requests directly from the kernel, logs them, and passes them to the underlying disk (like `overlayfs`).
- **`main.c`**: A tiny Ring-3 binary. It simply `mmap`s the kernel's exposed ring buffer to show the user the history, and sends `ioctl` commands down to the kernel to perform an undo.

### Step-by-Step Implementation (1 Day per Step)

* **Day 1: Kernel Environment & Stacked FS Skeleton**
  * Set up `Kbuild` and compile a basic Loadable Kernel Module (LKM) that prints to `dmesg`.
  * Create `oopsiefs.c`. Define basic `file_system_type`, `super_operations`, and `inode_operations`.
  * Implement `mount -t oopsiefs` so that it mounts cleanly but simply passes all operations transparently down to the lower filesystem (ext4/xfs) without doing anything else.
* **Day 2: Per-CPU Lockless Ring Buffers**
  * In `kernel_mod.c`, allocate `__percpu` variables to hold lockless ring buffers for each CPU core. This prevents cacheline bouncing.
  * Expose this kernel memory to user-space via a character device (`/dev/oopsie_wal`) so `main.c` can `mmap` it.
  * Implement the `OopsieRecord` appending logic in kernel space using fast `MOV` instructions to the CPU's local buffer.
* **Day 3: Intercepting I/O & Zero-Copy in the Kernel**
  * Modify `oopsiefs.c`'s `unlink` and `write` handlers.
  * Before passing the `unlink` down to the lower filesystem, increment the inode reference count directly in kernel memory (creating a hardlink internally) or duplicate the block allocation extents.
  * Append the metadata to the current CPU's ring buffer.
* **Day 4: NVMe Polling (Hardware Interrupt Bypass)**
  * For operations that require actual byte copying (if zero-copy isn't available), bypass the kernel's block layer sleep/interrupt cycle.
  * Implement a direct busy-wait loop that polls the NVMe Completion Queue (CQ) hardware register directly. This eliminates the 3-6 microsecond latency of CPU hardware interrupts.
* **Day 5: The Ring-3 CLI & IOCTL Integration**
  * Write `main.c` (User Space). It opens `/dev/oopsie_wal` and `mmap`s it to display the `undo list`.
  * Implement a custom `ioctl` command in `kernel_mod.c`. When the user types `undo`, `main.c` calls this `ioctl`, telling the kernel driver to execute the atomic restore internally, entirely avoiding user-space `rename` syscalls.
