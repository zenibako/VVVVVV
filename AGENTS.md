# VVVVVV Pocket benchmark routing

For Analogue Pocket benchmark planning, evidence collection or comparison, read
[the local pocket-benchmarking skill](.agents/skills/pocket-benchmarking/SKILL.md)
and [project integration guide](openfpga/BENCHMARKING.md). Establish the shared
harness checkout before following its workflow; a sibling folder is only a candidate.
For [routine regression checks](openfpga/BENCHMARKING.md#routine-regression-checks),
preserve the accepted reference, create independent candidates and run project
correctness plus complete host sweeps. Collector verification is not an output
gate; there is no VVVVVV RTL or `regression.py` adapter.

The [Pocket guide](openfpga/README.md) owns workloads, sampling, build options,
renderer/smoke checks and device gates. Read the current checkout's runtime pins;
do not infer deployed hardware or validated replay support from another checkout.
Prefer the dedicated `BENCH=1` core; its report writes remain volatile. Preserve
host versus device evidence boundaries and the existing correctness, hardware and
[release gates](openfpga/release/README.md). Collection grants no deployment,
pin changes or publication. Native/cross-platform work follows the
[desktop guide](desktop_version/README.md).

For release preparation, read the local
[release skill](.agents/skills/pocket-release/SKILL.md), which routes to the
shared pocket-dev-tools contract and retains this project's release gates.
