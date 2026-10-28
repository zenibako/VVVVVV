#!/usr/bin/env python3
"""Build a deterministic local release candidate; this script never publishes it."""
import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import io
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
LOCK = Path(__file__).with_name("runtime.lock.json")
CORE = "Cores/zenibako.vvvvvv/"
COMMON = "Assets/vvvvvv/common/"
CORE_FILES = (
    "audio.json", "core.json", "data.json", "input.json", "interact.json",
    "variants.json", "video.json", "LICENSE.txt", "os20.rbf_r", "loader.bin",
)
SD_FILES = tuple(CORE + name for name in CORE_FILES) + tuple(
    COMMON + name for name in ("os.bin", "vvvvvv.elf", "vvvvvv.ini", "vvvvvv-repo.zip")
) + ("Assets/vvvvvv/zenibako.vvvvvv/vvvvvv.json", "Platforms/vvvvvv.json",
     "Platforms/_images/vvvvvv.bin")
RUNTIME_FILES = (CORE + "os20.rbf_r", CORE + "loader.bin", COMMON + "os.bin")
MP_COMMON = COMMON + "mp/"
MP_INSTANCE = "Assets/vvvvvv/zenibako.vvvvvv/vvvvvv-mp.json"
MP_LAUNCH_FILES = (COMMON + "vvvvvv-mp.elf", COMMON + "vvvvvv-mp.ini", MP_INSTANCE)
MP_SD_FILES = tuple(CORE + name for name in (*CORE_FILES, "License-exceptions.txt", "MAKE-AND-PLAY.txt")) + tuple(
    COMMON + name for name in ("os.bin", "vvvvvv-mp.elf", "vvvvvv-mp.ini", "vvvvvv-repo.zip")
) + tuple(MP_COMMON + name for name in ("vvvvvv-mp-data.zip", "vvvvvv-mp-music.bin", "makeandplay-report.json")) + (
    MP_INSTANCE, "Platforms/vvvvvv.json", "Platforms/_images/vvvvvv.bin")
SHARED_INFO = """VVVVVV for Analogue Pocket / openfpgaOS (altered port).
Based on VVVVVV, copyright Terry Cavanagh 2010-2020.
One core offers two launch files: vvvvvv.json (full game) and vvvvvv-mp.json
(Make and Play). Each edition has separate data/music and ordinary saves.
The full game requires your own legally obtained data.zip and converted music;
the preparation plugin handles only that owned installation.
The self-contained vvvvvv-makeandplay-openfpga.zip supplies the official free
Make and Play assets, including 17 player levels, with no purchase or preparation
plugin needed. Install it into the same core; it contains only the MAKEANDPLAY
application and excludes the original campaign. Its public asset filenames are
listed by its launch instance for Pocket Sync Required Files.
Game source uses the VVVVVV Source Code License v1.0 and license exceptions.
See LICENSE.txt, the free package's License-exceptions.txt/MAKE-AND-PLAY.txt,
and the release's licenses.tar.gz for notices. Complete source accompanies the
release; see SOURCE-AVAILABILITY.json for the matched runtime source.
Runtime: the pinned os20 openfpgaOS build. Ordinary game saves are supported;
hardware sleep and save states are not supported by this port.
Port and preparation tooling developed with OpenAI Codex and Claude Code.
"""


NATIVES = tuple(f"prepare-data-{system}-{arch}" + (".exe" if system == "windows" else "")
                for system in ("darwin", "linux", "windows") for arch in ("amd64", "arm64"))
INFO = """VVVVVV for Analogue Pocket / openfpgaOS (altered port).
Based on the VVVVVV source code, copyright Terry Cavanagh 2010-2020.
Game source uses the VVVVVV Source Code License v1.0 and its license exceptions;
see LICENSE.txt and the release's licenses.tar.gz for complete notices.
Bring your own legally obtained data.zip and music. These assets are not supplied.
Runtime: the pinned os20 openfpgaOS build. Ordinary game saves are supported.
Hardware sleep and save states are not supported by this port.
Port and preparation tooling developed with OpenAI Codex and Claude Code.
"""


def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


def parse_json(data, name):
    try:
        return json.loads(data)
    except (ValueError, UnicodeDecodeError) as error:
        raise ValueError(f"Invalid JSON in {name}: {error}") from error


def require_version(version):
    if not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError("version must be an explicit stable vX.Y.Z tag")


def read_regular(root, relative):
    """Reject symlinks in every path component, including the input root."""
    root = Path(root).absolute()
    root = root.parent.resolve() / root.name
    path = root / relative
    components = [root]
    for part in PurePosixPath(relative).parts:
        components.append(components[-1] / part)
    for component in components:
        if component.is_symlink():
            raise ValueError(f"Symlink input is not allowed: {component}")
    if not path.is_file():
        raise ValueError(f"Missing regular input file: {path}")
    return path.read_bytes()


def validate_runtime_and_elf(files, runtime_lock, core=CORE, common=COMMON, app="vvvvvv"):
    hashes = runtime_lock.get("sha256", {})
    if set(hashes) != set(RUNTIME_FILES):
        raise ValueError("runtime lock must pin exactly the three runtime files")
    for canonical, actual in ((CORE + "os20.rbf_r", core + "os20.rbf_r"),
                              (CORE + "loader.bin", core + "loader.bin"),
                              (COMMON + "os.bin", common + "os.bin")):
        if hashlib.sha256(files[actual]).hexdigest() != hashes[canonical]:
            raise ValueError(f"Runtime SHA-256 hash mismatch: {actual}")
    elf = files[common + app + ".elf"]
    if (len(elf) < 52 or elf[:7] != b"\x7fELF\x01\x01\x01" or
            elf[16:20] != b"\x02\x00\xf3\x00"):
        raise ValueError(f"{app}.elf must be an ELF32 little-endian RISC-V executable")


def makeandplay_helper():
    spec = importlib.util.spec_from_file_location("release_makeandplay", Path(__file__).with_name("makeandplay.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def merge_shared_files(first, second):
    """Merge edition snapshots only when every shared path is byte-identical."""
    for name in first.keys() & second.keys():
        if first[name] != second[name]:
            raise ValueError("Conflicting shared edition file: " + name)
    return {**first, **second}


def validate_slots(files, instance, application="vvvvvv"):
    """Keep the native edition picker and each app's physical files/save bound."""
    standard = {1: "os.bin", 2: "vvvvvv.ini", 3: "vvvvvv.elf", 4: "data.zip",
                5: "vvvvvv-repo.zip", 6: "vvvvvv-music.bin", 10: "vvvvvv_0.sav"}
    expected = standard if application == "vvvvvv" else {
        **standard, 2: "vvvvvv-mp.ini", 3: "vvvvvv-mp.elf", 4: "mp/vvvvvv-mp-data.zip",
        6: "mp/vvvvvv-mp-music.bin", 10: "vvvvvv-mp_0.sav"}
    try:
        path = CORE + "data.json"
        slots = parse_json(files[path], path)["data"]["data_slots"]
        picker = slots[0]
        if (picker["id"] != 0 or "filename" in picker or picker["extensions"] != ["json"]
                or picker["parameters"] != 275 or picker["required"] is not True):
            raise ValueError("Expected a required native JSON instance picker")
        if len(slots) != 8 or {s["id"]: s["filename"] for s in slots[1:]} != standard:
            raise ValueError("Shared core slot identities/save names do not match the full-game defaults")
        if next(s for s in slots if s["id"] == 4).get("required") is not True:
            raise ValueError("Player data must be a required file in the shared core metadata")
        slots = parse_json(files[instance], instance)["instance"]["data_slots"]
        if len(slots) != len(expected) or {s["id"]: s["filename"] for s in slots} != expected:
            raise ValueError("Edition instance slot identity/save names do not match")
    except (IndexError, KeyError, TypeError) as error:
        raise ValueError("Invalid native instance picker/slot metadata") from error


def collect_makeandplay(sd, version, release_date, runtime_lock, asset_lock=None):
    """Admit public assets only after validating the exact bytes to be packaged."""
    require_version(version)
    helper = makeandplay_helper()
    lock = helper.load_lock() if asset_lock is None else asset_lock
    names = list(MP_SD_FILES)
    for name in lock["notices"]:
        path = PurePosixPath(name)
        if (path.is_absolute() or str(path) != name or any(part in (".", "..") for part in path.parts)
                or any(character in name for character in ("\\", ":", "\0"))):
            raise ValueError("Invalid public distribution notice path")
        names.append(MP_COMMON + "notices/" + name)
    snapshot = {name: read_regular(sd, name) for name in names}
    for name, digest in lock["notices"].items():
        if hashlib.sha256(snapshot[MP_COMMON + "notices/" + name]).hexdigest() != digest:
            raise ValueError("Public distribution notice SHA-256 mismatch: " + name)
    validate_runtime_and_elf(snapshot, runtime_lock, CORE, COMMON, "vvvvvv-mp")
    if b"MAKE AND PLAY EDITION" not in snapshot[COMMON + "vvvvvv-mp.elf"]:
        raise ValueError("Make and Play binary is missing its compiled edition marker")
    # Validate a private snapshot, not paths which may change after inspection.
    # Only these already-read bytes are subsequently archived and hashed.
    prepared_names = ("vvvvvv-mp-data.zip", "vvvvvv-mp-music.bin", "makeandplay-report.json")
    with tempfile.TemporaryDirectory(prefix="vvvvvv-makeandplay-check-") as temporary:
        prepared = Path(temporary)
        for name in (*prepared_names, *("notices/" + name for name in lock["notices"])):
            target = prepared / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(snapshot[MP_COMMON + name])
        report = helper.validate_prepared(prepared, lock, packaged=True)
    files = dict(snapshot)
    for name, data in files.items():
        if name.endswith(".json"):
            parse_json(data, name)
    name = CORE + "core.json"
    core = parse_json(files[name], name)
    try:
        metadata = core["core"]["metadata"]
        if metadata["platform_ids"] != ["vvvvvv"] or metadata["shortname"] != "vvvvvv":
            raise ValueError("Expected shared VVVVVV core IDs for Make and Play")
        metadata["version"] = version[1:]
        metadata["date_release"] = release_date
        validate_slots(files, MP_INSTANCE, "vvvvvv-mp")
    except (KeyError, TypeError) as error:
        raise ValueError("Invalid Make and Play identity/slot metadata") from error
    if (not re.search(rb"(?m)^ELF=vvvvvv-mp\.elf\r?$", files[COMMON + "vvvvvv-mp.ini"])
            or not re.search(rb"(?m)^ARGS=-assets mp/vvvvvv-mp-data\.zip\r?$", files[COMMON + "vvvvvv-mp.ini"])):
        raise ValueError("Make and Play INI application identity is incorrect")
    files[name] = json_bytes(core)
    return snapshot, files, report


def collect_core(sd, version, release_date, runtime_lock):
    require_version(version)
    snapshot = {name: read_regular(sd, name) for name in SD_FILES}
    return prepare_core(snapshot, version, release_date, runtime_lock)


def prepare_core(snapshot, version, release_date, runtime_lock):
    """Validate the raw snapshot and transform a separate package copy."""
    require_version(version)
    files = dict(snapshot)
    validate_runtime_and_elf(files, runtime_lock)
    validate_slots(files, "Assets/vvvvvv/zenibako.vvvvvv/vvvvvv.json")
    if b"MAKE AND PLAY EDITION" in files[COMMON + "vvvvvv.elf"]:
        raise ValueError("Standard core cannot package a Make and Play edition binary")
    for name, data in files.items():
        if name.endswith(".json"):
            parse_json(data, name)
    name = CORE + "core.json"
    core = parse_json(files[name], name)
    try:
        metadata = core["core"]["metadata"]
        if metadata["platform_ids"] != ["vvvvvv"] or metadata["shortname"] != "vvvvvv":
            raise ValueError("Expected the game core IDs, not a benchmark or renamed core")
        metadata["version"] = version[1:]
        metadata["date_release"] = release_date
    except (KeyError, TypeError) as error:
        raise ValueError("Invalid game core metadata") from error
    files[name] = json_bytes(core)
    files[CORE + "updaters.json"] = json_bytes({
        "declare_ai": ["OpenAI Codex", "Claude Code"],
        "plugins": ["https://github.com/zenibako/VVVVVV-pocket"],
    })
    files[CORE + "info.txt"] = INFO.encode()
    return files


def zip_bytes(files):
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = (0o100644 << 16)
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, data)
    return output.getvalue()


def tar_bytes(files, epoch):
    """Entries are (contents, mode, optional symlink target), with normalized owners."""
    output = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
            for name, (data, mode, target) in sorted(files.items()):
                entry = tarfile.TarInfo(name)
                entry.mtime = epoch
                entry.mode = mode
                if target is not None:
                    entry.type = tarfile.SYMTYPE
                    entry.linkname = target
                else:
                    entry.size = len(data)
                archive.addfile(entry, None if target is not None else io.BytesIO(data))
    return output.getvalue()


def as_entries(files):
    return {name: (data, 0o644, None) for name, data in files.items()}


def git(repo, *args):
    try:
        return subprocess.check_output(["git", "-C", str(repo), *args], stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as error:
        raise ValueError(f"git {' '.join(args[:2])} failed: {error.stderr.decode(errors='replace').strip()}") from error


def dirty_tracked(repo):
    # Untracked inputs (which could include owned assets) are never archived.
    return bool(git(repo, "status", "--porcelain", "--untracked-files=no", "--ignore-submodules=untracked"))


def archive_source(repo, revision, prefix="", pins=None):
    """Read exact Git objects, recursively replacing gitlinks with their pinned source."""
    files = {}
    with tarfile.open(fileobj=io.BytesIO(git(repo, "archive", "--format=tar", revision))) as archive:
        for item in archive:
            if item.isdir():
                continue
            name = prefix + item.name
            if item.issym():
                files[name] = (b"", item.mode, item.linkname)
            elif item.isfile():
                files[name] = (archive.extractfile(item).read(), item.mode, None)
            else:
                raise ValueError(f"Unsupported source archive entry: {name}")
    tree = git(repo, "ls-tree", "-rz", revision)
    for record in tree.split(b"\0"):
        if not record:
            continue
        metadata, raw_name = record.split(b"\t", 1)
        mode, kind, raw_commit = metadata.split()
        if mode != b"160000":
            continue
        name = raw_name.decode()
        submodule = Path(repo) / name
        if not (submodule / ".git").exists():
            raise ValueError(f"Missing initialized source submodule: {prefix + name}; run git submodule update --init --recursive")
        commit = raw_commit.decode()
        if git(submodule, "rev-parse", "HEAD").decode().strip() != commit:
            raise ValueError(f"Source submodule is not at its pinned commit: {prefix + name}")
        if dirty_tracked(submodule):
            raise ValueError(f"Source submodule has dirty tracked files: {prefix + name}")
        if pins is not None:
            pins[prefix + name] = commit
        files.update(archive_source(submodule, commit, prefix + name + "/", pins))
    return files


def license_files(source):
    result = {}
    for name, entry in source.items():
        basename = PurePosixPath(name).name.lower()
        if (basename.startswith(("license", "licence", "copying", "notice", "copyright"))
                or any(part.lower() in ("licenses", "licences", "notices") for part in PurePosixPath(name).parts)):
            result[name] = entry
    return result


def runtime_disclosure(lock, included):
    runtime = lock["runtime"]
    url = "https://github.com/" + runtime["repository"] + ".git"
    commit = runtime["commit"]
    statement = ("The complete pinned Git source tree and initialized submodules are in runtime-source.tar.gz."
                 if included else "Runtime source and runtime license notices are NOT included in this candidate.\n"
                 "Do not publish this candidate: obtain the pinned source with its notices and rebuild it first.")
    text = f"""# Pinned runtime source and rebuilding

{statement}

Runtime repository: {url}
Commit: {commit}
Variant: {runtime['variant']}

Fetch the full source (the API-only SDK checkout is not a source distribution):

    git clone {url} runtime-source
    git -C runtime-source checkout --detach {commit}
    git -C runtime-source submodule update --init --recursive

Read that revision's README.md, Makefile, NOTICE, REUSE.toml and LICENSES/ before
rebuilding. Install the FPGA toolchain and SDK required by that revision; follow
its documented os20 build targets, not another runtime variant. Preserve its
source modifications, build scripts and all third-party notices. The packaged
os20.rbf_r, loader.bin and os.bin must match runtime.lock.json; rebuilding from
source is not asserted to reproduce those bytes without the pinned build setup.
Then rerun this assembler with --runtime-source runtime-source. This script
verifies Git identity and archives tracked source; it does not verify that the
runtime was rebuilt or provide permission for proprietary FPGA/toolchain IP.

The game/port sources and preparation dependency sources are in source.tar.gz.
The game uses its custom source license, and original purchased art/music remain
proprietary and must not be redistributed. See licenses.tar.gz and GAME-DATA.md.
"""
    return text.encode(), {"schema": 1, "runtime": {
        "url": url, "commit": commit, "variant": runtime["variant"],
        "source_archive": "runtime-source.tar.gz" if included else None,
        "notices_included": included,
        "rebuild_instructions": "RUNTIME-SOURCE.md"}}


def assemble(sd, tools, output, version, source_commit=None, *, repo=ROOT,
             runtime_lock=None, allow_dirty=False, runtime_source=None,
             makeandplay_sd=None, makeandplay_lock=None):
    """Validate pinned inputs and archive exact sources before writing a candidate."""
    require_version(version)
    output = Path(output)
    if output.exists() or output.is_symlink():
        raise ValueError(f"Output already exists: {output}")
    repo = Path(repo)
    head = git(repo, "rev-parse", "HEAD").decode().strip()
    commit = git(repo, "rev-parse", "--verify", (source_commit or head) + "^{commit}").decode().strip()
    dirty = dirty_tracked(repo)
    if dirty and not allow_dirty:
        raise ValueError("Source has dirty tracked files; commit first or use --allow-dirty for a local candidate")
    epoch = int(git(repo, "show", "-s", "--format=%ct", commit).strip())
    release_date = datetime.fromtimestamp(epoch, timezone.utc).strftime("%Y-%m-%d")
    lock = runtime_lock if runtime_lock is not None else parse_json(LOCK.read_bytes(), str(LOCK))
    core_inputs = {name: read_regular(sd, name) for name in SD_FILES}
    core = prepare_core(core_inputs, version, release_date, lock)
    mp_inputs, mp_core, mp_report = {}, {}, None
    if makeandplay_sd is not None:
        mp_inputs, mp_core, mp_report = collect_makeandplay(
            makeandplay_sd, version, release_date, lock, makeandplay_lock)
    # Validate the original shared bytes too: canonicalized JSON must not hide
    # disagreement between the independently generated edition trees.
    combined_inputs = merge_shared_files(core_inputs, mp_inputs)
    # The catalog package advertises both launch choices but carries no public
    # assets. Those belong only to the asset-bearing MAKEANDPLAY-only package,
    # which must never contain the full-game executable/original level arrays.
    if mp_report is not None:
        core = merge_shared_files(core, {name: mp_core[name] for name in MP_LAUNCH_FILES})
        mp_core[CORE + "updaters.json"] = core[CORE + "updaters.json"]
        mp_core[CORE + "info.txt"] = SHARED_INFO.encode()
    tool_files = {name: read_regular(tools, name) for name in (*NATIVES, "plugin.wasm", "plugin.json", "plugin_icon.png", "prepare-data.wasm")}
    for name in ("plugin.wasm", "prepare-data.wasm"):
        if tool_files[name][:8] != b"\0asm\x01\0\0\0":
            raise ValueError(f"Invalid WebAssembly (WASM) v1 header: {name}")
    manifest = parse_json(tool_files["plugin.json"], "plugin.json")
    if not isinstance(manifest, dict) or manifest.get("allowed_hosts") != []:
        raise ValueError("plugin.json must declare allowed_hosts as []")
    source_pins = {}
    runtime_pins = {}
    source = archive_source(repo, commit, pins=source_pins)
    required_source = ("LICENSE.md", "License exceptions.md", "openfpga/tools/README.md")
    for name in required_source:
        if name not in source or source[name][2] is not None:
            raise ValueError(f"Missing tracked source notice/instructions at {commit}: {name}")
    licenses = license_files(source)
    artifacts = {"source.tar.gz": tar_bytes(source, epoch),
                 "runtime.lock.json": json_bytes(lock),
                 "GAME-DATA.md": source["openfpga/tools/README.md"][0]}
    if mp_report is not None:
        for packaged, tracked in (("LICENSE.txt", "LICENSE.md"), ("License-exceptions.txt", "License exceptions.md")):
            if mp_inputs[CORE + packaged] != source[tracked][0]:
                raise ValueError("Make and Play package license differs from committed source: " + packaged)
        core[CORE + "info.txt"] = SHARED_INFO.encode()
        artifacts["vvvvvv-makeandplay-openfpga.zip"] = zip_bytes(mp_core)
        licenses.update({"makeandplay-distribution/" + name.removeprefix(MP_COMMON + "notices/"):
                         (data, 0o644, None) for name, data in mp_inputs.items()
                         if name.startswith(MP_COMMON + "notices/")})
    artifacts["vvvvvv-openfpga.zip"] = zip_bytes(core)
    if runtime_source is not None:
        runtime_source = Path(runtime_source)
        runtime_commit = lock["runtime"]["commit"]
        if git(runtime_source, "rev-parse", "HEAD").decode().strip() != runtime_commit:
            raise ValueError("Runtime source HEAD does not match runtime.lock.json")
        if dirty_tracked(runtime_source):
            raise ValueError("Runtime source has dirty tracked files")
        runtime = archive_source(runtime_source, runtime_commit, pins=runtime_pins)
        if "NOTICE" not in runtime or not any(name in runtime for name in ("LICENSE", "LICENSE.md", "LICENSE.txt")):
            raise ValueError("Runtime source must include tracked root license and NOTICE files")
        runtime_licenses = license_files(runtime)
        if not runtime_licenses:
            raise ValueError("Runtime source must include tracked license notices")
        artifacts["runtime-source.tar.gz"] = tar_bytes(runtime, epoch)
        licenses.update({"runtime/" + name: entry for name, entry in runtime_licenses.items()})
    runtime_info, availability = runtime_disclosure(lock, runtime_source is not None)
    artifacts["RUNTIME-SOURCE.md"] = runtime_info
    artifacts["SOURCE-AVAILABILITY.json"] = json_bytes(availability)
    artifacts["licenses.tar.gz"] = tar_bytes(licenses, epoch)
    for name in NATIVES:
        executable = "prepare-data.exe" if name.endswith(".exe") else "prepare-data"
        bundle = {"licenses/" + key: value for key, value in licenses.items()}
        bundle[executable] = (tool_files[name], 0o644 if name.endswith(".exe") else 0o755, None)
        bundle["LICENSE.md"] = source["LICENSE.md"]
        bundle["License exceptions.md"] = source["License exceptions.md"]
        bundle["README.md"] = source["openfpga/tools/README.md"]
        artifacts[name.removesuffix(".exe") + ".tar.gz"] = tar_bytes(bundle, epoch)
    for name in ("plugin.wasm", "plugin.json", "plugin_icon.png", "prepare-data.wasm"):
        artifacts[name] = tool_files[name]
    provenance = {
        "schema": 1, "version": version, "source_commit": commit, "source_head": head,
        "source_dirty": dirty, "source_submodules": source_pins, "runtime_submodules": runtime_pins,
        "source_archive": "exact tracked commit and pinned submodules; untracked files excluded",
        "source_timestamp": epoch, "date_release": release_date,
        "runtime": lock["runtime"], "runtime_source_included": runtime_source is not None,
        "publishable": not dirty and commit == head and runtime_source is not None,
        "publication_note": "Local candidate only. Explicit review and version/tag approval are still required.",
        "inputs_sha256": {name: hashlib.sha256(data).hexdigest()
                          for name, data in sorted({**tool_files, **combined_inputs}.items())},
        "packaged_metadata_sha256": {name: hashlib.sha256(data).hexdigest()
                                     for name, data in sorted({**core, **mp_core}.items()) if name.endswith(".json")},
    }
    if mp_report is not None:
        provenance["makeandplay"] = mp_report
    artifacts["PROVENANCE.json"] = json_bytes(provenance)
    artifacts["SHA256SUMS"] = "".join(
        f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in sorted(artifacts.items())).encode()
    # Only create output after every validation succeeds. A sibling temp dir keeps
    # interruption/failure from exposing a partially assembled candidate.
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".vvvvvv-release-", dir=output.parent) as staging:
        staged = Path(staging) / "candidate"
        staged.mkdir()
        for name, data in artifacts.items():
            (staged / name).write_bytes(data)
        staged.rename(output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sd", type=Path, required=True)
    parser.add_argument("--tools", type=Path, required=True)
    parser.add_argument("--makeandplay-sd", type=Path,
                        help="optional self-contained Make and Play package; also adds its launch choice to the main core ZIP")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-commit", help="Git source commit (default: actual repository HEAD)")
    parser.add_argument("--allow-dirty", action="store_true", help="local candidate only; archive committed source and record dirty status")
    parser.add_argument("--runtime-source", type=Path, help="clean checkout of the exact pinned runtime commit, with initialized submodules")
    args = parser.parse_args()
    try:
        output = assemble(args.sd, args.tools, args.output, args.version, args.source_commit,
                          allow_dirty=args.allow_dirty, runtime_source=args.runtime_source,
                          makeandplay_sd=args.makeandplay_sd)
    except (ValueError, OSError) as error:
        parser.exit(1, f"release assembly failed: {error}\n")
    print(f"Release candidate assembled: {output}")
    provenance = json.loads((output / "PROVENANCE.json").read_text())
    if not provenance["publishable"]:
        print("Candidate is not publishable: inspect PROVENANCE.json and RUNTIME-SOURCE.md.")


if __name__ == "__main__":
    main()
