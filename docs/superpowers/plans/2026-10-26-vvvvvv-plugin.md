# VVVVVV Plugin Implementation Plan

> **For agentic workers:** Use subagent-driven-development to implement and review each task.

**Goal:** Prepare owned VVVVVV files through one shared converter, native CLI and Pocket Sync plugin, with a catalog-ready release pipeline.

**Architecture:** One Go module at openfpga/tools. The prep library owns validation/conversion/installation; frontends never duplicate conversion. Existing device packaging supplies a strictly verified core distribution.

**Tech Stack:** Go 1.25, pinned MIT pocket-music, TinyGo 0.43.0, Extism go-pdk 1.1.3 and go-sdk 1.7.1, Python release assembly.

**Spec:** ../specs/2026-10-26-vvvvvv-plugin-design.md

## Global constraints

Preserve core/platform IDs and ordinary saves. No purchased assets in source/releases. No external descriptions or release publication without user-authored/approved text and an explicit version. Keep originals first/default. Reject symlinks and unsafe paths. Preserve source licenses. Work from merged master 0bbff40b in a separate worktree.

## Task 1: Shared preparation and installation

Files: openfpga/tools/prep/*, cmd/prepare-data/*, internal/testgame/*.

Interfaces: prep.Prepare(source, output string, progress func(string)) (Report, error);
prep.Install(cardRoot string, discard bool, progress func(string)) (Report, error).
Report is JSON serializable and exposes Tracks int, Warnings []string and output file hashes.
Install reads Assets/vvvvvv/common/source and replaces only data.zip and vvvvvv-music.bin.

- [x] Write failing synthetic input, determinism, source-preservation and rollback tests.
- [x] Implement validation, pinned music conversion and transactional installation.
- [x] Add native/WASI CLI and tests, run Go tests/vet and decoder tests.
- [x] Review this task's contracts and quality.

## Task 2: Plugin and host integration

Files: openfpga/tools/plugin/*, cmd/pocket-plugin/*, integration/*, plugin.json.
Depends on Task 1 interfaces. Session New(root string, logger func(string)), Start() ([]byte,error), Handle([]byte) ([]byte,error).

- [x] Write failing conversation tests for install/help, keep/discard, stale answers and Kill.
- [x] Implement independent menu session and thin WASI exports using prep.Install.
- [x] Build TinyGo plugin and standard-Go WASI CLI; execute Extism with both preopen orders and compare native bytes.
- [x] Review plugin and integration coverage.

## Task 3: Release and catalog preparation

Files: openfpga/release/*, .github/workflows/openfpga-tools.yml, .github/workflows/openfpga-release.yml, openfpga/tools/README.md, openfpga/README.md.

- [x] Write failing core allowlist, runtime hash and release artifact tests.
- [x] Add deterministic assembly, source/provenance/licenses, versioned metadata and checksums.
- [x] Add CI and explicit-version release workflow with all verification gates.
- [ ] Build a complete local candidate and record catalog repository/filter/instructions.
- [ ] Review the branch, run relevant checks, sign commits and report publication readiness.

## Execution notes

- Ruling: use merged master 0bbff40b for the device source; ongoing final-room performance work stays in its existing worktree.
- Ruling: reuse pinned MIT pocket-music instead of copying AGPL Butterscotch application code.
- Real Extism tests found TinyGo WASI os.RemoveAll unsupported; bounded portable cleanup is verified through the actual host.
- Review fixed standalone parent ZIP paths and bounded unfinished Ogg packets before the recursive decoder. Actual WASM failure/retry regressions pass.
- Steam macOS data.zip contains the primary music blob; exact-root embedded blobs are supported, with separate-file precedence. Native owned-install test: 16 tracks, no warnings.
- Release review fixed snapshot provenance hashing and checked existing lightweight/annotated tag identity before publication.
