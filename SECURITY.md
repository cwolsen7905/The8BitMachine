# Security Policy

## Supported Versions

Only the latest release receives security fixes.

| Version | Supported |
|---------|-----------|
| Latest  | ✓ |
| Older   | ✗ |

## Reporting a Vulnerability

Please **do not** open a public GitHub issue for security vulnerabilities.

Report privately by email to:

**Christopher W. Olsen** — cwolsen@brainchurts.com

Include:
- A description of the vulnerability and its potential impact
- Steps to reproduce (ROM file, config file, or specific input if applicable)
- Any suggested fix or patch

You can expect an acknowledgement within 48 hours and a resolution or status update within 14 days.

## Scope

This is a desktop 8-bit machine emulator. Relevant security concerns include:

- **Malicious ROM or config files** — crafted `.bin`, `.prg`, or `.json` files that cause buffer overflows, out-of-bounds reads/writes, or path traversal when loaded
- **Audio callback safety** — issues in the SID synthesis path that could cause memory corruption under the SDL audio thread

The following are **out of scope**:

- Vulnerabilities within the *emulated* machine (e.g. C64 BASIC exploits) — these are features, not bugs
- Denial-of-service via intentionally slow ROM code causing the host to spin
- Issues requiring physical access to the host machine
