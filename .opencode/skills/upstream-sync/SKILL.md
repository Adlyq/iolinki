---
name: upstream-sync
description: Use when syncing the iolinki `own_dev` branch with upstream `w1ne/iolinki`, rebasing onto `upstream/develop`, resolving conflicts in `src/dll.c`, `src/isdu.c`, `src/frame.c` or `include/iolinki/protocol.h`, or dropping locally-applied spec patches that upstream has adopted.
---

# Upstream sync playbook (`own_dev`)

How to move `own_dev` forward with `upstream/develop` without losing the
transport / ISR / conformance work and without turning the branch into an
unmergeable fork.

## The one rule

**Rebase, never merge.** `own_dev` is a patch queue on top of
`upstream/develop`. It must stay linear so each patch can be replayed,
reordered, split, or dropped independently. Do not create merge commits and do
not merge `upstream/develop` into `own_dev`.

Priority when the PDF and upstream disagree:

1. **External behavior** (index values, access rules, error codes, lengths) →
   the bundled V1.1.5 PDF wins.
2. **Internal structure** (function shapes, context layout, helper locations)
   → upstream wins; adapt our patches instead of fighting the refactor.

## Repository topology

```
origin   git@github.com:Adlyq/iolinki.git        # personal fork
upstream https://github.com/w1ne/iolinki.git      # canonical
branch   own_dev                                  # topic branch
base     upstream/develop (a7cc831 at the time of writing)
```

Current expected state (re-check, do not trust blindly):

```bash
git rev-list --left-right --count upstream/develop...HEAD
# <behind> <ahead>  — ahead is ~30, behind should be 0 before you start
```

## Patch-queue anatomy

Identify commits by **subject**, not by hash: every rebase rewrites hashes.

| Group | Subjects | Additive? | Drop when |
|---|---|---|---|
| Transport / ISR | `feat(dll): shared transmit seam + opt-in ISR two-pump transport`, `test: add a transport warp…`, docs | mixed (see below) | never (core feature) |
| OD width | `feat(dll): support 8/32-byte OD widths…` | additive | upstream implements variable OD differently |
| Spec constants | `feat(iolink): add cycle-time…`, `feat(protocol): add ProductURI/… constants` | additive | almost never |
| Spec corrections | `fix(spec): align SystemCommand codes with Table B.9/C.1` | semantic divergence | upstream adopts the same corrections |
| 0x11 naming | `refactor(protocol): name 0x11 as IDX_NOTAVAIL…` | alias only | upstream renames/clarifies |
| ISDU conformance | `fix(isdu): reserve…`, `reject WRITE…`, `serve ErrorCount…`, `clamp identification…`, `serve ProductURI…`, `return process input…`, `serve process output…`, `parameter lock…`, plus their tests | mostly `isdu.c` rewrites | upstream implements the same gap |
| Docs | `docs: align conformance docs…`, `docs: mark the ISDU conformance gaps closed`, `docs: record the ErrorCount…` | additive | keep |

Keep the split-commit granularity: one gap / one correction per commit. It is
what makes "drop ours, take theirs" cheap.

## The dangerous surface

These are the only places where our diff is **not** purely additive.

### `src/dll.c` (commit `feat(dll): shared transmit seam + opt-in ISR…`)

- `dll_send()` is a transmit seam. In the default build its `#else` branch is
  literally upstream's `phy->send(...)`. Seven call sites in the
  `dll_handle_*` helpers use it instead of calling `phy->send` directly.
- `iolink_dll_process()` is wrapped in
  `#if IOLINK_PHY_FEATURE_ISR_PUMP` / `#else` / `#endif`; the streaming loop
  stays in the `#else`, the mailbox dispatch is in the `#if` branch.
- The ~160-line frame-complete dispatch was extracted into
  `dll_process_complete_frame()`, shared by both transports.
- `dll_expected_req_len()` is wrapped in
  `#if !IOLINK_PHY_FEATURE_ISR_PUMP`; the ISR build uses
  `dll_compute_req_len()` / `iolink_dll_expected_req_len()`.
- Everything else (variants, `iolink_dll_prepare_cyclic_reply`,
  `iolink_dll_process_isr`, TX-modes) is inside the trailing
  `#if IOLINK_PHY_FEATURE_ISR_PUMP` block.

`src/frame.c` is **untouched** by us. If it conflicts, take upstream.

### `src/isdu.c`

The conformance commits edit `handle_mandatory_indices`,
`handle_access_locks`, `build_direct_param_page1`, `handle_direct_parameters`
and `handle_standard_commands`. These are spec-mandated rewrites; expect
overlap if upstream ever reworks ISDU.

### `include/iolinki/protocol.h`

Three separable things:

- additive constants (`PRODUCT_URI`, `PROCESS_DATA_OUTPUT`, `OFFSET_TIME`,
  MasterCommand 0x95-0x9A, `SERV_NOTAVAIL` 0x20);
- corrections (SystemCommand Param* 0x01-0x06, 0x84 reserved, 0x40/0x41);
- `IOLINK_ISDU_ERROR_IDX_NOT_AVAIL` alias for 0x11.

## Upstream risk watchlist

Upstream has unmerged branches that rewrite our hot files. Before a rebase,
inspect them:

```bash
for b in feat/reentrant-device-context feat/shared-frame-helpers feat/business-family-v3; do
  echo "== $b"; git diff --stat upstream/develop..upstream/$b -- src include
done
```

At the time of writing they rewrite `src/dll.c` (‑550 to ‑571 lines),
`src/isdu.c` (‑724 to ‑786), `src/frame.c`, `src/params.c`, and
`feat/shared-frame-helpers` adds `src/iolink_core.c`. When any of these lands
in `develop`, `dll.c`/`isdu.c` will conflict textually; plan a dedicated
resolution session rather than a quick rebase.

## Pre-flight

```bash
git status --short                      # must be clean (ignore build_* dirs)
git fetch upstream --prune
git rev-list --left-right --count upstream/develop...HEAD
git branch backup/own_dev-$(date +%Y%m%d-%H%M) HEAD
git config rerere.enabled true          # reuses conflict resolutions
```

If `behind == 0`, there is nothing to sync; stop. If `ahead == 0`, reset the
branch instead of rebasing.

## Rebase

```bash
git rebase upstream/develop
```

- Resolve conflicts commit by commit; do **not** squash on the fly.
- On a conflict, inspect both sides before choosing:

  ```bash
  git status
  git diff --cc <file>
  git log --oneline --left-right HEAD...REBASE_HEAD -- <file>
  ```

- Use `git checkout --ours <file>` only when the whole file is ours and you are
  sure; prefer manual hunk resolution for `dll.c` / `isdu.c`.
- After each resolution: `git add <file> && git rebase --continue`.

### Conflict playbook

| File | Resolution |
|---|---|
| `src/frame.c` | take upstream (`git checkout --theirs` during rebase = upstream) |
| `src/dll.c` | keep upstream's streaming structure; re-apply `dll_send` at the 7 call sites, the `#if/#else` pump wrapper and the mailbox hook, and re-derive `dll_process_complete_frame` from upstream's new `iolink_dll_process` body |
| `src/isdu.c` | if upstream implements the same gap, **drop our commit** and keep upstream; otherwise keep our gap-specific hunks |
| `include/iolinki/protocol.h` | additive constants: keep both. Corrections: take upstream if it now matches the PDF, else keep ours |
| `include/iolinki/{dll,phy,config}.h` | additive feature flags/APIs: keep both; rename to upstream's new vocabulary if it collides |
| `src/device.c`, `src/crc.c` | additive: keep both |
| `tests/test_helpers.c` | keep the transport warp; adapt it to any upstream PHY-vtable change |
| `tests/CMakeLists.txt` | keep upstream registrations **and** our new test files |
| `docs/*` | keep both; re-state status against the new upstream |

Never run `clang-format -i` over a whole upstream file. The installed
clang-format is newer than the pinned one and will produce noise. Format only
the lines you touched, or skip `protocol.h` (it has pre-existing drift).

## Dropping upstream-equivalent patches

When upstream adopts a behavior we implemented, drop our patch instead of
carrying a duplicate:

```bash
git rebase -i upstream/develop
# change `pick` to `drop` for the now-redundant commit
```

Candidates, in order of likelihood:

1. each `fix(isdu): ...` conformance gap;
2. `fix(spec): align SystemCommand codes with Table B.9/C.1`;
3. `refactor(protocol): name 0x11 as IDX_NOTAVAIL per Table C.1`;
4. `feat(dll): support 8/32-byte OD widths for TYPE_1_V/2_V` if upstream adds
   variable OD width its own way.

If dropping a commit breaks a later commit's context, drop the dependent tests
too, or fold them into the upstream implementation's test file.

## Post-sync verification

```bash
# history sanity: each patch should still be one logical change
git range-diff upstream/develop..backup/own_dev-<stamp> upstream/develop..HEAD
git diff backup/own_dev-<stamp> HEAD --stat   # expect only intentional changes

# both transports must stay green
cmake -B build_dev && cmake --build build_dev -j"$(nproc)"
(cd build_dev && ctest --output-on-failure)          # expect 44/44

cmake -B build_dev_isr -DIOLINK_ISR_PUMP=ON
cmake --build build_dev_isr -j"$(nproc)"
(cd build_dev_isr && ctest --output-on-failure)      # expect 43/43

# optional quality gate
./check_quality.sh
```

Formatting on touched files only:

```bash
clang-format --dry-run --Werror src/isdu.c src/dll.c tests/test_spec_conformance.c
```

## Failure / rollback

```bash
git rebase --abort                       # mid-rebase
git reset --hard backup/own_dev-<stamp>  # after a bad completed rebase
git range-diff ...                       # confirm nothing else was lost
```

Do not `git push --force` a shared branch without coordinating; `own_dev` is a
personal topic branch, but force-pushing rewrites the backup lineage for anyone
who fetched it.

## Commit hygiene for new work

- Conventional Commits: `feat(dll):`, `fix(isdu):`, `test(spec):`, `docs:`.
- One index / one rule / one refactor per commit — the next rebase depends on
  it.
- Keep new behavior behind `#if IOLINK_PHY_FEATURE_*` where possible; prefer
  new files or additive blocks over editing upstream files.
- Update `docs/SPEC_CONFORMANCE.md` in the same commit as a behavior change.
- Never commit secrets, build directories, or the extracted PDF text.
