# Coding Standards

This document defines the coding style and engineering practices for the `iolinki` project.

## 1. C Language Standard
- **Standard**: C99 (ISO/IEC 9899:1999). A few test targets use C11 for
  `_Static_assert`.
- **Safety**: MISRA C:2012 subset (see `tools/gates.sh static`); the MISRA addon is
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
  `self`) states "must outlive the context" in its Doxygen comment.
- **Borrowed views must state their expiry.** A returned pointer the caller
  does not own names its invalidation point (e.g. "valid until the next
  `rx_frame()` call").
- **Direction tags are mandatory** for pointer parameters: `@param[in]`,
  `@param[out]` or `@param[in,out]`.
- **Nullability: non-null by default.** A header opens an
  `IOLINK_ASSUME_NONNULL_BEGIN`/`END` region, so an unmarked pointer is
  required (`_Nonnull` under Clang, nothing elsewhere). `IOLINK_NULLABLE` marks
  the cases where NULL carries meaning: an optional hook or context, "no
  application table", an output the caller may skip, or a return value whose
  NULL means "not found". A function may still guard `if (p == NULL) return
  err;` — that is a fallback for a caller bug, not a contract, so the parameter
  stays non-null. `IOLINK_NONNULL` is only written for nested pointer types
  (arrays of pointers), which the pragma does not infer.
  A pointer the callee *reads* stays non-null even when the paired length can
  be 0 — the empty case does not turn an input into an optional argument. The
  exception is a NULL that *is* a state the interface defines rather than a
  value the caller forgot to fill in: an unbound diagnosis registry means "no
  registry" and answers DeviceStatus 0, and a standalone ISDU engine
  legitimately has none, so `iolink_diag_device_status()` takes
  `IOLINK_NULLABLE` and its internal guard implements the declared state. The
  header then says what NULL means, and a test passes the literal NULL so that
  dropping the annotation fails to compile.
  An output the caller may skip is `IOLINK_NULLABLE` and names the precondition
  that makes skipping useful (`iolink_isdu_od_read(od_out)` with `od_len == 0`,
  `iolink_ds_serialize(out = NULL)` = measure only). The one type
  that is always `IOLINK_NULLABLE` is the application context
  (`iolink_app_t *`): it is whatever the application stored in
  `iolink_device_app_t::self`, and every hook has to tolerate NULL there.
  The region must be unconditional and the header must include
  `"iolinki/types.h"` for the macro; `tools/check_nullability.sh` fails the
  `quick` group otherwise. Tests that exercise a defensive guard reach it
  through `TEST_NULL(type)` (`tests/test_null.h`), which documents "this call
  breaks the contract on purpose".
- **No universal pointers.** Name the type, or declare an opaque type and let
  the owner define the struct (e.g. `iolink_tiol112_board_t`). `void *` is
  reserved for interfaces that cannot name a type (byte views, symbol tables,
  a lower layer's caller context) and every such use carries `IOLINK_OPAQUE`
  plus the reason in its Doxygen.
- **Context rules are part of the contract.** State whether a callback runs in
  main context or the data ISR, and whether re-entering the API is allowed.
- **Getter contexts are `const`** unless the call updates state that the
  caller owns.
