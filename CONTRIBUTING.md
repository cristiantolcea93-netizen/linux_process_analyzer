# Contributing to Linux Process Analyzer

Thanks for your interest in contributing to **Linux Process Analyzer**!  
Contributions are welcome, but please read this document carefully before starting.

This project aims to remain **clean, predictable, and easy to maintain**, so a few rules apply.

---

## General Rules

- **All changes must go through a Pull Request**
- **Direct pushes to `main` are not allowed**
- **At least one approval from the project maintainer is required**
- Prefer small, focused PRs
- New features should be discussed first

---

## What You Can Contribute

### ✅ Accepted Contributions
- Bug fixes
- Performance improvements
- Code refactoring (no functional change)
- Documentation improvements
- Unit and integration tests
- New metrics (must fit the existing model)

### ⚠️ Discuss First
Please open an issue before working on:
- New CLI options
- Changes to JSON schema
- Changes to output format
- Architectural changes
- Plugin system extensions
- Major refactoring

### ❌ Not Accepted
- Breaking changes without discussion
- Cosmetic-only changes
- Features that significantly increase runtime overhead
- Non-Linux platforms

---

## Development Workflow

- Fork the repository
- Create a feature branch:
      
```bash
git checkout -b feature/my-change
```
- Make your changes
- Ensure the project builds cleanly:

```bash
./make.sh
```
   or 
   
```bash
./makeAll.sh
```
- Run any available tests (recommended before PR):
		
```bash
./makeAll.sh -includeUnitTests -includeIntegrationTests
```
- Run static analysis before opening a PR:

```bash
cppcheck \
    --enable=warning,style,performance \
    --inconclusive \
    --error-exitcode=1 \
    --std=c11 \
    --quiet \
    --inline-suppr \
    -i code/build \
    code/
```
- Commit with a clear message
- Open a Pull Request against `main`

---

## Build System
The official build entry points are:

```bash
./make.sh
./makeAll.sh
```
Both scripts support:
- `-includeUnitTests`
- `-includeIntegrationTests`
If no parameters are provided, only a build is performed.

## Testing

### Unit Tests
- Implemented using Unity
- Located in `tests/unit_tests`
- Run automatically via CTest
- Cover core modules:
	-process_stats
	-process_snapshot
	-args_parser
	-config

Run manually: 

```bash
./makeAll.sh -includeUnitTests
```
### Integration Tests

- Implemented as shell scripts
- Located in: `tests/integration/`
- Validate real execution scenarios
- Use tools such as:
  - `jq`
  - `stress-ng`

Run manually:

```bash
./makeAll.sh -includeIntegrationTests
```

### Writing tests

When adding new features:

- Add unit tests when possible
- Add integration tests for system-level behavior
- Avoid relying on specific process names
- Prefer deterministic checks 

## Continous Integration (CI) 
This project uses **GitHub Actions** for CI.
On every Pull Request and push to `main`, the following are executed:
- `cppcheck` static code analysis
- Full build
- Unit tests
- Integration tests

CI must pass before a PR can be merged.
If CI fails, the PR will not be accepted.

### Static analysis in CI

The `cppcheck` stage fails when unsuppressed findings are reported in the enabled categories:

- `warning`
- `style`
- `performance`
- `inconclusive` variants of the checks above

The CI workflow excludes `code/build` from analysis to avoid warnings from generated files.

For every GitHub Actions run, CI uploads a `cppcheck-report` artifact.
If the static analysis step fails, download that artifact from the workflow run page to inspect the XML report directly.

## Coding Guidelines

### C Code
- Follow existing code style
- Prefer clarity over cleverness
- Avoid macros unless absolutely necessary
- Avoid allocations in hot paths
- Handle all error cases

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
