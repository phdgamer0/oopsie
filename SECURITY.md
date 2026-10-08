# Security Policy

## Supported Versions

This project is under active development and has no numbered releases yet.
Only the current development branch receives security fixes.

| Version / Source         | Supported          |
| ------------------------ | ------------------ |
| `linux_user` (dev)       | :white_check_mark: |
| future tagged releases   | :white_check_mark: |
| older unmaintained forks | :x:                |

## Reporting a Vulnerability

Please report suspected security issues privately rather than in the public
issue tracker.

- Email: yazdan.phdgamer0@gmail.com
- GitHub: https://github.com/phdgamer0/oopsie/issues (mark the issue "security")

Please include:

- A minimal reproduction (commands run and files/paths affected)
- The environment (OS, compiler, `LD_PRELOAD` setup, whether running from a
  monitored shell)
- Any impact assessment you can provide

## What to Expect

- Acknowledgment within 3 business days.
- Triage: if accepted, a fix is committed to the development branch as soon
  as practical; if declined, you will receive an explanation.
- Details are disclosed after a fix is available so that users can upgrade
  first.

## Security Notes

- The shim intercepts file operations via `LD_PRELOAD` and therefore trusts
  any software that can load shared libraries as the process owner. It does
  not provide sandboxing.
- Monitored events and vault copies are stored under `/tmp/oopsie/`
  (`vault.wal`, `shim.log`, and per-inode vault files). Treat any content
  written there as readable by the user that started the monitored shell.
- The vault and WAL directories are created with mode `0700`; do not weaken
  those permissions.