# Shared Pocket benchmark integration

The [Pocket guide](README.md) owns VVVVVV's existing workloads, seeds, warmups,
frame counts, 30 Hz pacing, smoke/renderer checks and device procedure. Keep its
scripted route and focused/exhaustive final sweeps distinct. This guide connects
those workloads to the shared collector; it changes no sampling or release rules.
Read [release assembly](release/README.md) before preparing a release candidate.

## Establish the shared harness

Use the canonical [pocket-dev-tools](https://github.com/zenibako/pocket-dev-tools)
checkout, formerly `pocket-harness`: take an explicit session root, then
`POCKET_DEV_TOOLS_ROOT`, then legacy `POCKET_HARNESS_ROOT`. Resolve conflicting
explicit roots before choosing. Resolve the selected path
to a canonical directory; a sibling name alone does not establish the dependency.
Before proceeding, require these files in that checkout:

- `.agents/skills/pocket-benchmarking/SKILL.md`
- `benchmarks/__main__.py`
- `benchmarks/README.md`, `benchmarks/PLATFORMS.md`, `benchmarks/SELECTION.md`

Read the canonical shared skill and those maintained guides, then run from the
harness root:

```sh
cd "$POCKET_HARNESS_ROOT"
python3 -m benchmarks --help
python3 -m benchmarks compare --help
```

The latter must offer `software`, `platform`, `configuration`, `--vary`, and
`--reason`. Stop and report a missing checkout/file/feature rather than assuming
compatibility. Set the legacy `POCKET_HARNESS_ROOT` used in these command
examples to that already verified path; do not let an old environment value
select a different checkout. Do not copy the shared skill or symlink it across repositories.
All collector commands below run from this verified harness root. Its `build/`
is the private location for definitions, metadata, logs, screenshots and bundles;
none belong in this project's source or release archives.

## Routine regression checks

Use the [shared preserved-baseline contract](https://github.com/zenibako/pocket-dev-tools/blob/fd5fc7af6cb51b19bb800d6c2cd43b7c03cfcf58/benchmarks/REGRESSIONS.md)
(introduced in shared commit `fd5fc7af6cb51b19bb800d6c2cd43b7c03cfcf58`) from the verified
`pocket-dev-tools` checkout. Require its `benchmarks/REGRESSIONS.md` and record the
actual shared commit; do not silently substitute an older checkout. Keep private
source exports, accepted evidence, indices and candidate/failure records under
that checkout's ignored directories. Do not ship them in this project's releases.

Keep an accepted source/build and immutable host-sweep evidence separate from
new candidate builds/runs. Preserve source/dirty inventory, assets, compiler and
ordered flags, backend, seed, preset/route version, exact room/state and viewport
roster, warmup, frame counts, 30 Hz pacing and enabled instrumentation. Match the
host domain and controls before comparing a routine software change; focused,
exhaustive and generic scripted-route workloads are different evidence.

Run the independent renderer/pixel/RNG and applicable smoke checks before
archiving a complete successful `vvvvvv-host` sweep, including its expected
counts/footer. Retain failed/incomplete logs without converting them to PASS.
Record a private baseline path/digest; create each candidate under a new name
without rewriting or promoting over the accepted evidence. Test a known bad
candidate against the project's output decision, not only the timing parser.

Collector `verify` checks archive integrity and `compare` reports compatible
numeric deltas. Neither reruns correctness tests, certifies equal output or
applies an automatic performance threshold. There is no VVVVVV RTL adapter or
`regression.py` automation; the shared Butterscotch command cannot accept these
cases. Missing private assets or checks remain explicit coverage gaps.

Keep device summaries and host microseconds separate, and repeat applicable
Pocket launch/gameplay/save/audio/Memories/sleep checks for affected changes.
Preserve the existing hardware, release/license and publication gates. Different
runtime/compiler/platform settings require a separately declared experiment,
not a relaxed software regression comparison.

## Select candidates and identify their actual bytes

Start with the current checkout's [dependency pins](scripts/fetch-deps.sh),
[runtime lock](release/runtime.lock.json), and [packager](scripts/package.sh).
Choose a shortlist compatible with the port's ISA/features, memory, SDK/API and
packaging requirements. Record each profile ID/digest and its candidate, selected,
incompatible, deferred or unselected disposition with a reason. Evaluate against
representative scenes and chosen targets (frame budget, worst frames, audio or
loading as relevant). The os20/os25 × 90/100 MHz matrix is an evidence inventory;
this project need not build or test all four combinations. Clock, CPU variant,
cache sizes and enabled features are independent declarations, not deductions
from a label. A single tested candidate establishes suitability within scope,
not superiority over untested candidates.

Prepare an actual version-1 platform definition using the shared platform guide.
Record OS/SDK source identities and CPU ISA, issue width, cache sizes and features
from the selected build configuration. Obtain that exact CPU configuration from
the runtime source/build; the downloaded binary trio alone is insufficient to
create a complete profile. Do not invent missing identities or alter pins to
satisfy the collector.

For a prepared default `BENCH=1` package, the relevant local files are:

| Artifact | Path relative to this repository |
| --- | --- |
| Application | `openfpga/build/sd/Assets/vvvvvv/common/vvvvvv-bench.elf` |
| OS | `openfpga/build/sd/Assets/vvvvvv/common/os.bin` |
| FPGA bitstream | `openfpga/build/sd/Cores/zenibako.vvvvvvbench/os20.rbf_r` |
| Chip32 loader | `openfpga/build/sd/Cores/zenibako.vvvvvvbench/loader.bin` |

Focused/exhaustive sweeps use their documented `build/final-sweep` or
`build/final-sweep-all` package roots instead. Inspect the selected package:
`loader.bin` in the existing pinned package is the small 199-byte Chip32 program,
not an OS-containing RISC-V loader. Hash those actual loader bytes separately
from `os.bin` and the RBF; do not substitute another loader or pad it to fit an
assumption. Verify the size/identity again for any other candidate. The bitstream
also contains runtime components, so keep OS/RBF/loader from the intended matching
build. A profile hash does not prove source-to-binary correspondence or flashing.

With private definition/config files prepared and `VVVVVV_ROOT` set to this
checkout's absolute path, the default package example is:

```sh
python3 -m benchmarks profile create build/vvvvvv-platform-definition.json \
  --os-bin "$VVVVVV_ROOT/openfpga/build/sd/Assets/vvvvvv/common/os.bin" \
  --bitstream "$VVVVVV_ROOT/openfpga/build/sd/Cores/zenibako.vvvvvvbench/os20.rbf_r" \
  --loader "$VVVVVV_ROOT/openfpga/build/sd/Cores/zenibako.vvvvvvbench/loader.bin" \
  --cpu-config build/vvvvvv-cpu-config.cfg --output build/platforms/vvvvvv-candidate.json
python3 -m benchmarks profile verify build/platforms/vvvvvv-candidate.json \
  --os-bin "$VVVVVV_ROOT/openfpga/build/sd/Assets/vvvvvv/common/os.bin" \
  --bitstream "$VVVVVV_ROOT/openfpga/build/sd/Cores/zenibako.vvvvvvbench/os20.rbf_r" \
  --loader "$VVVVVV_ROOT/openfpga/build/sd/Cores/zenibako.vvvvvvbench/loader.bin" \
  --cpu-config build/vvvvvv-cpu-config.cfg
```

Adapt the explicit package paths for each actual candidate; filenames are not
proof of its variant or clock. Creation hashes all four inputs, including the
CPU config; verification checks bytes only. Keep the packaged ELF hash, actual
compiler/flags, source revision, SDK identity and asset manifest in run metadata.

## Collect the existing workloads

Use the [documented `BENCH=1` builds](README.md#build), with fresh build/object
directories when switching presets. `make BENCH=1 package` creates
`zenibako.vvvvvvbench` beside gameplay with distinct app/settings/save names.
Benchmark save writes currently stay in memory; a separate core ID does not
provide durable result export. Its assets folder shares `os.bin` with gameplay:
a candidate benchmark package must not overwrite that shared file with a
different runtime. Resolve deployment isolation separately before device use.

Preserve each preset's exact route version, room/state roster, seed, warmup,
sampling denominator and pacing. Record core role (`conditions.core_role`),
assets, framebuffer, audio, initial state and other effective settings, plus
known physical device and firmware. Optional deployment metadata can retain
core/save IDs and package hash. Build/source attribution is declared, not device
attestation. Run repeated paired trials on the same Pocket and retain variation.

For host sweeps, retain the complete successful smoke output, including expected
case counts and footer, not selected `SWEEP_VIEW`/`SWEEP_RESULT` lines:

```sh
python3 -m benchmarks import --format vvvvvv-host \
  --source build/vvvvvv-host.log --metadata build/vvvvvv-host-run.json
```

These CSV times are host microseconds and stay in a host-only evidence scope.
The generic scripted-route output is not automatically a supported host-sweep
CSV input. Do not attach device profiles to host evidence or present host timing
as Pocket timing.

For Pocket results, finish measurement before taking screenshots. Capture every
required results page, retaining the original images. Prepare `device-summary`
rows only for the measurements visible there, using stable room/state IDs and
explicit units; convert displayed milliseconds to microseconds (32.7 ms =
32700 us). Preserve unknown sample counts as null. A ranked room's cross-view
maximum need not occur in its worst-average viewport; do not manufacture
per-viewport rows or percentiles from ranked screens.

```sh
python3 -m benchmarks import --format device-summary \
  --source build/vvvvvv-device-values.json --metadata build/vvvvvv-device-run.json \
  --attachment build/vvvvvv-page-1.png --attachment build/vvvvvv-page-2.png \
  --profile build/platforms/vvvvvv-candidate.json
```

Repeat attachments for every actual required page. This archives raw originals;
it does not OCR or certify the transcription, add a correctness PASS, or provide
a device exporter. Use the printed bundle path with `verify`:

```sh
python3 -m benchmarks verify build/benchmarks/RUN_ID
python3 -m benchmarks matrix --project vvvvvv --game vvvvvv \
  --runs build/benchmarks/RUN_ID \
  --profiles build/platforms/vvvvvv-candidate.json
```

Select actual bundle paths and relevant profiles explicitly. Different
application/workload/conditions/backend/device scopes remain separate matrix
rows; imported counts are not independent trials. `profile_registered` is a
declaration and screenshot-backed `device_observed` remains unchecked.
Unselected/untested cells are not failed requirements. Preserve failed evidence.

## Choose the comparison that matches the experiment

| Mode | Controls and permitted differences |
| --- | --- |
| `software` (default) | Fixed platform/profile, toolchain, runtime, workload, assets, conditions and sampling; application source/ELF may change |
| `platform` | Same known ELF/source/compiler/flags, device, assets, full workload/conditions and sampling; declare every detected changed factor with `--vary` |
| `configuration` | Complete profile-bound device runs with known ELF/source/toolchain; rebuilt ELF/toolchain and platform may differ, with a required `--reason` |

```sh
python3 -m benchmarks compare build/benchmarks/BASELINE build/benchmarks/CANDIDATE
python3 -m benchmarks compare build/benchmarks/BASELINE build/benchmarks/CANDIDATE \
  --mode platform --vary clock
python3 -m benchmarks compare build/benchmarks/BASELINE build/benchmarks/CANDIDATE \
  --mode configuration --reason "The candidate SDK requires a different application ELF."
```

These are alternative modes for appropriately matched pairs. Platform mode
requires matching known device ID and asset-manifest hash, and complete profile
bindings; use `--vary variant` and/or `--vary runtime` too when those factors
actually changed. The shared platform guide defines conservative factor
detection, including changed CPU config and OS/SDK sources. Different ELFs cannot
enter platform mode. Bound software pairs require identical profiles; legacy
unbound pairs remain provisional and cannot be mixed with bound runs.

Configuration mode still holds physical device, assets, full case/conditions,
adapter and sample/metric rosters fixed. Read its retained full configurations,
field/presence differences, detected components, source checks and limitations.
Its attribution is `whole_configuration_only`: the result cannot isolate an
OS, CPU, cache, clock, compiler or application effect. Multiple `--vary` factors
also describe a combined experiment. Neither mode rescales 90/100 MHz timings,
proves statistical significance, or automatically selects a winner.

Choose among eligible tested configurations using measured variation and the
project's targets; a tie or insufficient evidence is valid. Keep the existing
renderer/pixel/RNG, smoke, gameplay, launch/save/audio/sleep checks and physical
Pocket acceptance separate. Hardware fit/timing closure and release/license/
publication gates still apply. The shared RTL model validates its supported
Butterscotch os25 workloads; collecting VVVVVV evidence supplies neither a
VVVVVV replay adapter nor validation of this port's deployed runtime.
