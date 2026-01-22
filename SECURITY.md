# Security Policy

## Supported Versions

This project is currently in **active development**.

Security updates are provided only for the **latest release on the `main` branch**.

| Version | Supported |
|--------|-----------|
| Latest `main` | ✅ |
| Older releases | ❌ |

---

## Reporting a Vulnerability

If you discover a security vulnerability in **Linux Process Analyzer**, please report it responsibly.

### Please DO NOT:
- Open a public GitHub issue for security vulnerabilities
- Post details publicly before a fix is available

### How to report

Send a private report including:
- A clear description of the vulnerability
- Steps to reproduce (if applicable)
- Potential impact
- Your environment (Linux distro, kernel version, etc.)

📧 **Contact**:  
Open a **private GitHub security advisory** (preferred), or contact the maintainer directly.

---

## Scope

This security policy applies to:
- The `process_analyzer` binary
- Snapshot logging (text / jsonl)
- `metrics.json` generation
- Configuration file handling
- Plugin API (when available)

Out of scope:
- Third-party libraries included in `third_party/`
- User-provided scripts or external tooling
- Misuse of the tool with elevated privileges

---

## Security Considerations

This tool:
- Reads data from `/proc`
- Does **not** require root privileges
- Does **not** open network sockets
- Does **not** execute external commands

Nevertheless, care is taken to:
- Avoid buffer overflows
- Validate all parsed numeric input
- Prevent integer overflows in time and rate calculations
- Handle malformed `/proc` entries gracefully

---

## Vulnerability Handling Process

1. Vulnerability report received
2. Issue reproduced and assessed
3. Fix developed on a private branch (if needed)
4. Patch released
5. Reporter credited (optional)

---

## Acknowledgements

Responsible disclosure helps keep this project and its users safe.  
Thank you for taking the time to report issues securely 🙏

