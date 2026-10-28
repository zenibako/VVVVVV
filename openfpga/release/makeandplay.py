#!/usr/bin/env python3
"""Prepare only the locked, public VVVVVV Make and Play distribution.

The existing native converter owns the audio format. This wrapper establishes
redistribution provenance; it never accepts an arbitrary owned installation.
See the repository's LICENSE.md and License exceptions.md.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import stat
import subprocess
import tempfile
import urllib.request
import zipfile

LOCK = Path(__file__).with_name("makeandplay.lock.json")
MAX_ARCHIVE = 70 * 1024 * 1024
MAX_DATA = 64 * 1024 * 1024
MAX_MUSIC = 256 * 1024 * 1024
MAX_NOTICE = 1024 * 1024
REPORT_NAME = "makeandplay-report.json"


def load_lock(path=None) -> dict:
    """Load checked-in source and converted-output identities."""
    lock = json.loads(Path(path or LOCK).read_text())
    if lock.get("schema") != 1 or lock.get("edition") != "makeandplay":
        raise ValueError("Unsupported Make and Play lock")
    if lock["data"]["path"] != "data.zip" or lock["music"]["path"] != "vvvvvv-music.bin":
        raise ValueError("Unexpected locked output filenames")
    return lock


def _json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


def expected_report(lock) -> dict:
    """The report deliberately contains only public source identities."""
    return {"schema": 1, "edition": lock["edition"], "version": lock["version"],
            "source": dict(lock["archive"]),
            "files": {key: {"size": lock[item]["size"], "sha256": lock[item]["sha256"]}
                      for key, item in (("data.zip", "data"), ("vvvvvv-music.bin", "music"))},
            "levels": list(lock["levels"]), "notices": dict(lock["notices"])}


def _safe_name(name):
    clean = name.rstrip("/")
    p = PurePosixPath(clean)
    if (not clean or "\\" in name or ":" in name or "\0" in name or
            p.is_absolute() or str(p) != clean or any(x in (".", "..") for x in p.parts)):
        raise ValueError("Archive contains an unsafe path")
    return clean


def _regular(directory, relative, limit):
    root = Path(directory)
    _safe_name(relative)
    parts = [root]
    for part in PurePosixPath(relative).parts:
        parts.append(parts[-1] / part)
    if any(p.is_symlink() for p in parts):
        raise ValueError("Symlink prepared input is not permitted: " + relative)
    path = parts[-1]
    if not path.is_file() or path.stat().st_size > limit:
        raise ValueError("Missing or oversized prepared file: " + relative)
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    if len(data) > limit:
        raise ValueError("Oversized prepared file: " + relative)
    return data


def _identity(data, identity, label):
    if len(data) != identity["size"] or hashlib.sha256(data).hexdigest() != identity["sha256"]:
        raise ValueError("Pinned size or SHA-256 mismatch: " + label)


def _members(archive):
    infos = archive.infolist()
    if len(infos) > 4096:
        raise ValueError("Archive has too many entries")
    seen = set()
    total = 0
    for info in infos:
        key = _safe_name(info.filename).casefold()
        if key in seen:
            raise ValueError("Archive contains a duplicate path")
        seen.add(key)
        kind = stat.S_IFMT(info.external_attr >> 16)
        if kind not in (0, stat.S_IFREG, stat.S_IFDIR) or info.flag_bits & 1:
            raise ValueError("Archive contains a non-regular or encrypted entry")
        total += info.file_size
        if total > 256 * 1024 * 1024:
            raise ValueError("Archive exceeds uncompressed size limit")
    return infos


def _levels(data, lock):
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            levels = sorted(info.filename for info in _members(archive)
                            if not info.is_dir() and info.filename.endswith(".vvvvvv"))
    except (zipfile.BadZipFile, RuntimeError) as error:
        raise ValueError("Invalid public data.zip") from error
    if levels != lock["levels"]:
        raise ValueError("Public player level inventory differs from lock")
    return levels


def validate_prepared(directory, lock=None) -> dict:
    """Verify public assets, every locked notice and sanitized provenance.

    Both prepared output and APF common are accepted; unrelated APF files are
    ignored, but the distribution notices are required in either directory.
    """
    lock = load_lock() if lock is None else lock
    for name, item, maximum in (("data.zip", "data", MAX_DATA),
                                ("vvvvvv-music.bin", "music", MAX_MUSIC)):
        data = _regular(directory, name, min(maximum, lock[item]["size"]))
        _identity(data, lock[item], name)
        if name == "data.zip":
            _levels(data, lock)
    for name, digest in lock["notices"].items():
        notice = _regular(directory, "notices/" + name, MAX_NOTICE)
        if hashlib.sha256(notice).hexdigest() != digest:
            raise ValueError("Public notice SHA-256 mismatch: " + name)
    try:
        report = json.loads(_regular(directory, REPORT_NAME, MAX_NOTICE))
    except (ValueError, UnicodeDecodeError) as error:
        raise ValueError("Invalid Make and Play provenance report") from error
    if report != expected_report(lock):
        raise ValueError("Make and Play provenance report differs from public lock")
    return report


def _read_archive(path, lock):
    size = lock["archive"]["size"]
    if size > MAX_ARCHIVE:
        raise ValueError("Locked archive exceeds size limit")
    if path is not None:
        path = Path(path)
        if path.is_symlink() or not path.is_file() or path.stat().st_size != size:
            raise ValueError("Public archive size or file type differs from lock")
        with path.open("rb") as stream:
            data = stream.read(size + 1)
    else:
        with urllib.request.urlopen(lock["archive"]["url"], timeout=60) as stream:
            data = stream.read(size + 1)
    _identity(data, lock["archive"], "official archive")
    return data


def _selected(archive, name, limit):
    _safe_name(name)
    try:
        info = archive.getinfo(name)
        if info.is_dir() or info.file_size > limit:
            raise ValueError("Public archive member exceeds limit: " + name)
        with archive.open(info) as stream:
            data = stream.read(limit + 1)
        if len(data) != info.file_size or len(data) > limit:
            raise ValueError("Public archive member size mismatch: " + name)
        return data
    except (KeyError, zipfile.BadZipFile, RuntimeError) as error:
        raise ValueError("Missing or damaged public archive member: " + name) from error


def prepare(converter, output, archive=None, *, lock=None) -> dict:
    """Convert exact official assets privately; publish only verified outputs."""
    output = Path(output)
    if output.exists() or output.is_symlink():
        raise ValueError("Output already exists")
    lock = load_lock() if lock is None else lock
    archive_bytes = _read_archive(archive, lock)
    try:
        with zipfile.ZipFile(io.BytesIO(archive_bytes)) as source:
            _members(source)
            data = _selected(source, "data.zip", MAX_DATA)
            _identity(data, lock["data"], "data.zip")
            _levels(data, lock)
            notices = {name: _selected(source, name, MAX_NOTICE) for name in lock["notices"]}
    except zipfile.BadZipFile as error:
        raise ValueError("Invalid public distribution archive") from error
    for name, notice in notices.items():
        if hashlib.sha256(notice).hexdigest() != lock["notices"][name]:
            raise ValueError("Public notice SHA-256 mismatch: " + name)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".makeandplay-", dir=output.parent) as temp:
        root = Path(temp)
        original = root / "source"
        original.mkdir()
        (original / "data.zip").write_bytes(data)
        converted = root / "converted"
        result = subprocess.run([str(Path(converter).resolve()), str(original.resolve()), str(converted.resolve())],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        if result.returncode:
            raise ValueError("Native Make and Play preparation failed")
        staged = root / "prepared"
        staged.mkdir()
        for name, item, maximum in (("data.zip", "data", MAX_DATA), ("vvvvvv-music.bin", "music", MAX_MUSIC)):
            prepared = _regular(converted, name, min(maximum, lock[item]["size"]))
            _identity(prepared, lock[item], name)
            (staged / name).write_bytes(prepared)
        for name, notice in notices.items():
            path = staged / "notices" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(notice)
        (staged / REPORT_NAME).write_bytes(_json_bytes(expected_report(lock)))
        report = validate_prepared(staged, lock)
        if output.exists() or output.is_symlink():
            raise ValueError("Output already exists")
        staged.rename(output)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", type=Path, metavar="DIRECTORY")
    parser.add_argument("--converter", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--archive", type=Path, help="exact pinned official Linux distribution ZIP")
    args = parser.parse_args()
    if args.verify:
        if args.converter or args.output or args.archive:
            parser.error("--verify cannot be combined with preparation options")
    elif not args.converter or not args.output:
        parser.error("preparation requires --converter and --output")
    try:
        report = validate_prepared(args.verify) if args.verify else prepare(args.converter, args.output, args.archive)
    except (ValueError, OSError) as error:
        parser.exit(1, "Make and Play validation failed: " + str(error) + "\n")
    print(_json_bytes(report).decode(), end="")


if __name__ == "__main__":
    main()
