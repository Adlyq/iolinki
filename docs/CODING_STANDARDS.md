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

## 7. Ownership and Lifetime Annotations

C has no borrow checker; the stack encodes ownership in the API contract and
the comments. Public headers are the authoritative place for that contract;
implementation-level buffer ownership stays in `docs/CODE_FLOW.md` (section
12.2) and is linked, never duplicated.

- **Default is borrow.** A pointer parameter is borrowed for the duration of
  the call unless documented otherwise; the stack does not retain it.
- **Retained pointers must say so.** Any pointer kept beyond the call (e.g.
  `iolink_device_config_t`, `device_info`, `storage`, the PHY vtable and its
  `user`) states "must outlive the context" in its Doxygen comment.
- **Borrowed views must state their expiry.** A returned pointer the caller
  does not own names its invalidation point (e.g. "valid until the next
  `rx_frame()` call").
- **Direction tags are mandatory** for pointer parameters: `@param[in]`,
  `@param[out]` or `@param[in,out]`.
- **Context rules are part of the contract.** State whether a callback runs in
  main context or the data ISR, and whether re-entering the API is allowed.
- **Getter contexts are `const`** unless the call updates state that the
  caller owns.
