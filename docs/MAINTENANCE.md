# MAINTENANCE.md

How this repository is maintained. The short version: **`own_dev` is the
mainline, and upstream is a read-only reference.**

## 1. Repository topology

```
origin   git@github.com:Adlyq/iolinki.git       # authoritative
upstream https://github.com/w1ne/iolinki.git    # read-only reference
mainline own_dev
```

Snapshot at the time of writing (`git rev-list --left-right --count
upstream/develop...HEAD` → `0 114`): this branch is 114 commits ahead of, and 0
behind, `upstream/develop`. Re-check the numbers; do not treat them as a target.

## 2. Branch model

| Branch | Role |
| :--- | :--- |
| `own_dev` | maintained mainline. All work lands here; releases are tagged here. |
| `fix/*`, `feature/*` | short-lived topic branches cut from `own_dev`, merged back via PR. |
| `develop` | historical snapshot from before independent maintenance. Not a release branch and not a merge target. |
| `main` | does not exist on `origin`. Do not create it without an explicit policy change. |
| `backup/*` | local safety copies, never pushed as a shared branch. |

## 3. Development workflow

1. Cut a topic branch from `own_dev`.
2. Implement the change and its test. Update `CHANGELOG.md` under `[Unreleased]`.
3. Run the local checks:

   ```bash
   cmake -B build && cmake --build build
   (cd build && ctest --output-on-failure)
   ./check_quality.sh
   ```

4. Open the PR against `own_dev` and wait for CI.
5. Squash or keep the logical commits as agreed in review; do not force-push a
   branch someone else fetched without coordinating.

`no-commit-to-branch` blocks `main`/`develop`. `own_dev` is not blocked, but
topic branches + PR keep the history reviewable.

## 4. Releases

Releases are tagged from `own_dev` once CI is green:

```bash
git fetch origin
git switch own_dev
git pull --ff-only
git tag -a vX.Y.Z -m "Release version X.Y.Z"
git push origin vX.Y.Z
```

There is no `main` promotion step and no back-merge. See
[RELEASE_STRATEGY.md](RELEASE_STRATEGY.md) for the full process, and note that
the release workflow builds **Debug with coverage** — a local `Release` build is
only an optimisation sanity check, not what produces the artifacts.

## 5. Relationship with upstream

`upstream/develop` is a reference implementation, not a branch to converge on.
Never merge or rebase onto it: this branch carries a different transport (ISR
two-pump only), a different NVM/storage backend, and spec corrections upstream
may not have adopted. A merge would reintroduce removed APIs
(`src/frame.c`, `phy->send`/`phy->recv_byte`,
`IOLINK_PHY_FEATURE_ISR_PUMP`) and silently drop behaviour.

### Adopting an upstream change

1. Read the upstream commit and the spec section it claims to implement
   (`git show upstream/develop -- <path>`, then check the bundled V1.1.5 PDF).
2. Decide whether the behaviour is already present, spec-mandated, or
   fork-private. `docs/UPSTREAM_DIVERGENCE.md` lists the known deviations.
3. Re-implement the behaviour against **this** codebase — do not copy a diff.
   Upstream's context layout, storage API and transport differ.
4. Add or extend a regression test in `tests/`.
5. Record it in `CHANGELOG.md`; update `docs/UPSTREAM_DIVERGENCE.md` if it
   changes a spec-level behaviour or closes a divergence.

### Sending a fix upstream

Still possible and encouraged for self-contained spec corrections:

- one behaviour per PR, based on upstream's current `develop` API;
- include the spec table/clause and a test;
- do not bundle the fork-private transport, storage backend, or example ports;
- keep the local implementation independent of whether the PR is accepted.

## 6. Documents and their status

| Document | Status |
| :--- | :--- |
| `AGENTS.md` | current; agent rules for this repo |
| `docs/MAINTENANCE.md` | current; this file |
| `docs/UPSTREAM_DIVERGENCE.md` | historical divergence inventory; not a sync plan |
| `docs/SPEC_CONFORMANCE.md` | current; feature -> implementation -> test matrix |
| `docs/RELEASE_STRATEGY.md` | current; release process |
| `docs/superpowers/**` | historical plans/specs; not maintained |
