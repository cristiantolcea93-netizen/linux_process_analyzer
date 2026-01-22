# Contributing to Linux Process Analyzer

Thanks for your interest in contributing to **Linux Process Analyzer**!  
Contributions are welcome, but please read this document carefully before starting.

This project aims to remain **clean, predictable, and easy to maintain**, so a few rules apply.

---

## General Rules

- **All changes must go through a Pull Request**
- **Direct pushes to `main` are not allowed**
- **At least one approval from the project maintainer is required**
- Small, focused PRs are preferred over large, mixed changes
- New features should be discussed before implementation

---

## What You Can Contribute

### ✅ Accepted Contributions
- Bug fixes
- Performance improvements
- Code refactoring (no functional change)
- Documentation improvements
- Tests
- New metrics (must fit the existing model)

### ⚠️ Discuss First
Please open an issue before working on:
- New CLI options
- Changes to JSON schema
- Changes to output format
- Architectural changes
- Plugin system extensions

### ❌ Not Accepted
- Breaking changes without discussion
- Style-only changes with no functional value
- Features that significantly increase runtime overhead
- Platform-specific code outside Linux

---

## Development Workflow

1. Fork the repository
2. Create a feature branch:
   ```bash
   git checkout -b feature/my-change
   ```
3. Make your changes
4. Ensure the project builds cleanly:
   ```bash
   cd code && make.sh
   ```
5. Run any available tests
6. Commit with a clear message
7. Open a Pull Request against `main`

---

## Coding Guidelines

### C Code
- Follow existing code style
- Prefer clarity over cleverness
- Avoid macros unless absolutely necessary
- No dynamic allocation in hot paths unless justified
- Handle errors explicitly

### Time & Metrics
- Use `CLOCK_MONOTONIC` for all calculations
- Never mix realtime and monotonic clocks
- Keep metric calculations deterministic

### JSON Output
- Do not change existing fields
- New fields must be documented in `SCHEMA.md`
- Schema version must be updated if breaking changes are introduced

---

## Commit Messages

Use clear, descriptive commit messages:

Good examples:
```
Fix RSS delta calculation for short-lived processes
Add write_rate metric to JSON output
Refactor process snapshot parsing
```

Avoid:
```
fix
updates
misc changes
```

---

## Testing

- New features should include tests where applicable
- Bug fixes should include a regression test if possible
- Tests must not depend on system-specific process names

---

## Reporting Issues

When reporting a bug, please include:
- Linux distribution and kernel version
- How the tool was executed (CLI options)
- Expected vs actual behavior
- Relevant logs or JSON output (if available)

---

## License

By contributing to this project, you agree that your contributions will be licensed under the **MIT License**, the same license used by this project.

---

## Final Notes

This project is maintained by a single maintainer.  
Reviews may take some time, but all reasonable contributions will be considered.

Thank you for helping improve Linux Process Analyzer!
