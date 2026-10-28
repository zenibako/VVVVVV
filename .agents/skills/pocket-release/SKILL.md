---
name: pocket-release
description: Use when validating VVVVVV Pocket distributions, preparing release candidates, reviewing plugin assets or publishing an explicitly authorized release.
---

# VVVVVV Pocket releases

Read repository instructions and the [release guide](../../../openfpga/release/README.md) first.

## Read the shared workflow

Canonical owner: <https://github.com/zenibako/pocket-dev-tools> (formerly
`pocket-harness`). Use the root established in this session, then
`POCKET_DEV_TOOLS_ROOT`, then legacy `POCKET_HARNESS_ROOT`. Resolve conflicting
explicit roots before proceeding. A sibling `pocket-dev-tools` or `pocket-harness`
is only a candidate: verify `.agents/skills/pocket-release/SKILL.md` and
`docs/RELEASING.md`, confirm the Git remote identifies the canonical repository
(or its former name), and record its full commit. Resolve and read that canonical
skill before following its relative links. Apply it together with this project's
local release guide; stronger local checks remain required.

If the shared checkout is unavailable, report the missing dependency and continue
applicable local checks. Do not claim shared validation, copy the shared skill
body, or create cross-repository skill symlinks. A checkout predating the release
skill needs an explicit update; a matching directory name alone is insufficient.

## Project contract

Use this checkout's `openfpga-release.yml`, `openfpga/release/assemble.py` and `runtime.lock.json`. Keep the complete asset/source/license inventory, protected publication environment and source/runtime checks. Candidate-only builds do not authorize publication; record physical launch, controls/audio, progress-save and applicable Memory/sleep checks separately from CI.
