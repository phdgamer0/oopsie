# Oopsie - Linux User (Ring-3) Undo Engine

This branch contains the Ring-3 (User Space) implementation of the `oopsie` undo utility.

> IMPORTANT!
>
> This tool is still under development and may not be stable

## Architecture

This approach is highly deployable (requires no root access) and relies on `LD_PRELOAD` to intercept libc calls before they hit the kernel. It uses `ioctl(FICLONE)` for zero-byte zero-copy metadata cloning and a memory-mapped binary Write-Ahead Log (WAL) to completely avoid standard file parsing bloat.

### Folder Structure

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

## Setup & Deployment

Clone and Build:

```bash
git clone https://github.com/phdgamer0/oopsie.git
cd oopsie && make
```

*Note: This branch runs entirely in user-space.*
