# Coding Standards

This document defines the coding style and engineering practices for the `iolinki` project.

## 1. C Language Standard
- **Standard**: C99 (ISO/IEC 9899:1999). A few test targets use C11 for
  `_Static_assert`.
- **Safety**: MISRA C:2012 subset (see `check_quality.sh`); the MISRA addon is
  report-only unless `IOLINKI_MISRA_ENFORCE=1`.

## 2. Naming Conventions
- **Prefixes**: All public symbols (functions, structs, enums) must start with `iolink_`.
- **Functions**: `snake_case` (e.g., `iolink_dll_process`).
- **Variables**: `snake_case`. Private/local variables do not require the prefix.
- **Types**: `snake_case` with `_t` suffix (e.g., `iolink_dll_ctx_t`).
- **Macros**: `UPPER_SNAKE_CASE` (e.g., `IOLINK_MAX_PAYLOAD`).

## 3. Documentation
- **Tool**: Doxygen.
- **Requirement**: Every public API function and structured type in `include/` must have a Doxygen comment block.
- **Style**:
  ```c
  /**
   * @brief Brief description of the function.
   * @param param_name Description of the parameter.
   * @return Description of the return value.
   */
  ```

## 4. Static Analysis
- **Cppcheck**: Must pass with `--enable=warning,style,performance,portability`
  and `--error-exitcode=1`.
- **MISRA**: Checked through the cppcheck MISRA addon; report-only by default
  (`IOLINKI_MISRA_ENFORCE=1` makes it fail).

## 5. Testing
- **Unit Tests**: Every behaviour change needs a test. Coverage is reported in
  the release notes and by `tools/coverage.sh`; there is no enforced threshold.
- **Framework**: CMocka.
- **Integration**: Major features need a full-stack integration test.

## 6. Git & Formatting
- **Formatter**: `clang-format` pinned to 23.1.1, using the project's
  `.clang-format` file.
- **Commit Messages**: Follow Conventional Commits (e.g. `feat(dll): ...`).
