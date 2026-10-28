---
name: pocket-benchmarking
description: Use when choosing platform profiles, planning benchmark cores, or collecting and comparing Analogue Pocket benchmark evidence for VVVVVV; excludes unrelated desktop work.
---

# VVVVVV Pocket benchmarking router

## Shared repository identity

The canonical repository is <https://github.com/zenibako/pocket-dev-tools>,
formerly `pocket-harness`. For discovery use a root explicitly established in
the session, then `POCKET_DEV_TOOLS_ROOT`, then legacy `POCKET_HARNESS_ROOT`.
Resolve conflicting explicit roots before proceeding. Both sibling directory
names are candidates only; validate the canonical benchmark skill and collector
as described below. Existing local paths and CLI names remain supported.

Read this checkout's [integration guide](../../../openfpga/BENCHMARKING.md),
[Pocket workloads and checks](../../../openfpga/README.md), and
[release gates](../../../openfpga/release/README.md).

Resolve the shared checkout with the discovery rules above. Require its
canonical `.agents/skills/pocket-benchmarking/SKILL.md`, `benchmarks/__main__.py`,
`benchmarks/README.md`, `benchmarks/PLATFORMS.md`, and `benchmarks/SELECTION.md`.
Read that canonical shared skill before following its relative references. From
that harness root, check `python3 -m benchmarks compare --help` for software,
platform and configuration modes, `--vary` and `--reason`. If any dependency or
feature is missing, report it; do not silently use an older comparator.

Keep the shared workflow in its owning checkout: no copied skill body or
cross-repository symlinks. The project guide owns local artifact paths and
workload constraints. Prefer the existing dedicated `BENCH=1` core; full host CSV
uses `vvvvvv-host`, while device pages use `device-summary` with original
screenshots. Neither importing nor profile registration validates VVVVVV RTL.
