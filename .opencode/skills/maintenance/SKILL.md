---
name: maintenance
description: Use when maintaining the iolinki `own_dev` branch independently of upstream `w1ne/iolinki`, adopting or evaluating an upstream commit or patch, preparing a single-purpose PR back to upstream, updating `docs/UPSTREAM_DIVERGENCE.md`, or when a task mentions rebasing, merging or syncing with `upstream/develop` (do not — re-implement instead).
---

# Maintenance playbook (`own_dev`)

`own_dev` is the maintained mainline. Upstream is a reference, not a parent.

## The one rule

**Never merge or rebase onto `upstream/develop`, and never `git cherry-pick` an
upstream patch wholesale.** Upstream has a different transport (they still have
streaming hooks; we only have the ISR two-pump path), a different storage API,
and may not have adopted the spec corrections this branch depends on. A merge
reintroduces deleted APIs and silently drops behaviour.

Upstream changes are **re-implemented**, with a test, against this codebase.

Priority when the PDF and upstream disagree: the bundled V1.1.5 PDF under
`docs/IOL-Interface-Spec_10002_V1.1.5_Oct2025/` wins. See
`docs/UPSTREAM_DIVERGENCE.md` for the decisions already taken.

## Topology

```
origin   git@github.com:Adlyq/iolinki.git       # authoritative
upstream https://github.com/w1ne/iolinki.git    # read-only reference
mainline own_dev
```

- `develop` is a historical snapshot, not a release branch.
- `main` does not exist on `origin`.
- Re-check state instead of trusting numbers:

```bash
git fetch upstream --prune
git fetch origin --prune
git rev-list --left-right --count upstream/develop...HEAD   # <behind> <ahead>
git branch --show-current                                    # expect own_dev or fix/*/feature/*
```

## Adopting an upstream change

1. **Read the intent, not the diff.**

   ```bash
   git log --oneline upstream/develop -20
   git show upstream/develop -- <path>
   ```

   Identify the spec clause (Table/Annex) or the concrete bug it fixes.

2. **Check whether we already have it.** Search this repo by behaviour, not by
   symbol name (upstream renames things):

   ```bash
   rg -n 'ProductURI|ErrorCount|process input' src include tests
   ```

   Cross-check `docs/UPSTREAM_DIVERGENCE.md`; if it is already closed here, stop
   and say so.

3. **Classify it.**

   | Class | Action |
   |---|---|
   | Spec-mandated correction | implement + conformance test; note the Table/clause |
   | Bug fix touching the shared logic (`src/dll.c`, `src/isdu.c`, `src/crc.c`) | implement + regression test |
   | Fork-private area (transport, storage backend, DS accessors) | ignore; our design is intentional |
   | New upstream API/refactor | ignore unless it buys a concrete fix; do not restructure to match |

4. **Re-implement against this codebase.** Keep the diff minimal and local. Do
   not copy upstream's function shapes, context fields, or file layout. Editing
   existing files is fine — there is no "additive only" constraint anymore.

5. **Test.** Add or extend a case in `tests/` (typically
   `test_spec_conformance.c` for ISDU behaviour, `test_dll.c` for framing).
   Prefer a test that fails before the change.

6. **Document.** Add a `CHANGELOG.md` `[Unreleased]` entry. If it changes a
   spec-level behaviour or closes a listed divergence, update
   `docs/UPSTREAM_DIVERGENCE.md` (and mark the upstream PR status if relevant).

## Sending a fix upstream

Allowed and encouraged for self-contained spec corrections:

- one behaviour per PR, branched from upstream's current `develop`;
- reference the Table/clause and include a test;
- do **not** include the ISR-only transport, the unified NVM storage backend,
  the DS parameter-set accessors, examples, or docs that describe fork-private
  design;
- the local implementation must not depend on whether the PR is accepted.

Record the PR link in `docs/UPSTREAM_DIVERGENCE.md` §8. Do not add a
"rebase after it lands" step — nothing in this repo waits on upstream.

## Verification

```bash
# host build + full suite (untrimmed stack)
cmake -B build && cmake --build build
(cd build && ctest --output-on-failure)

# quality script (-Werror build + cppcheck; MISRA/format only with the
# IOLINKI_MISRA_ENFORCE=1 env var)
./check_quality.sh

# trimmed profiles only build the library (the unit suite is skipped)
cmake -B build_tiny -DIOLINK_TINY_PROFILE=ON -DBUILD_TESTING=OFF
cmake --build build_tiny

# ISR granularity variants are compile definitions, not CMake options
cmake -B build_frame \
  -DCMAKE_C_FLAGS="-DIOLINK_ISR_RX_MODE=1 -DIOLINK_ISR_TX_MODE=1"
cmake --build build_frame
```

Do not claim a variant passes if only the library compiled.

## Facts that are stale — do not repeat them

- `src/frame.c` does not exist. Neither do `phy->send` / `phy->recv_byte`.
- `IOLINK_PHY_FEATURE_ISR_PUMP` does not exist; the ISR pump is always on.
- `-DIOLINK_ISR_PUMP=ON` is not a CMake option.
- The platform switch is `IOLINK_PLATFORM` (not `PLATFORM`).
- `IOLINK_US_FROM_MS` was replaced by `iolink_us_from_ms()`.
- `iolink_ds_storage_api_t` was replaced by `iolink_storage_api_t`
  (`read`/`write`/`erase` + `user`, relative offsets).

## Commit hygiene

- Conventional Commits: `feat(dll):`, `fix(isdu):`, `test(spec):`, `docs:`.
- One behaviour or one refactor per commit.
- Topic branch from `own_dev`, PR back into `own_dev`.
- Never commit secrets, build directories, or extracted PDF text.
