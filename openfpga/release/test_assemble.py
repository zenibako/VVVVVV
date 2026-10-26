"""Release contracts exercised with synthetic data; no owned game assets needed."""
import hashlib
import importlib.util
import io
import json
import os
import re
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
import zipfile

SCRIPT = Path(__file__).with_name("assemble.py")
CORE = "Cores/zenibako.vvvvvv/"
COMMON = "Assets/vvvvvv/common/"
FILES = [CORE + name for name in (
    "audio.json", "core.json", "data.json", "input.json", "interact.json",
    "variants.json", "video.json", "LICENSE.txt", "os20.rbf_r", "loader.bin")]
FILES += [COMMON + name for name in ("os.bin", "vvvvvv.elf", "vvvvvv.ini", "vvvvvv-repo.zip")]
FILES += ["Assets/vvvvvv/zenibako.vvvvvv/vvvvvv.json", "Platforms/vvvvvv.json",
          "Platforms/_images/vvvvvv.bin"]
NATIVES = [f"prepare-data-{system}-{arch}" + (".exe" if system == "windows" else "")
           for system in ("darwin", "linux", "windows") for arch in ("amd64", "arm64")]


def load_module():
    if not SCRIPT.exists():
        return None
    spec = importlib.util.spec_from_file_location("release_assemble", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.module = load_module()
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sd = self.root / "sd"
        for relative in FILES:
            path = self.sd / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"fixture " + relative.encode())
            if path.suffix == ".json":
                path.write_text('{"fixture": true}\n')
        self.original_core = {
            "core": {"magic": "APF_VER_1", "metadata": {
                "platform_ids": ["vvvvvv"], "shortname": "vvvvvv", "version": "0.0.1",
                "date_release": "2020-01-01"},
                "framework": {"sleep_supported": False, "hardware": {"link_port": False}},
                "cores": [{"id": 0, "filename": "os20.rbf_r"}]}}
        (self.sd / (CORE + "core.json")).write_text(json.dumps(self.original_core))
        elf = bytearray(52)
        elf[:7] = b"\x7fELF\x01\x01\x01"
        elf[16:20] = b"\x02\x00\xf3\x00"
        (self.sd / (COMMON + "vvvvvv.elf")).write_bytes(elf)
        self.lock = {"schema": 1, "runtime": {"repository": "example/runtime", "commit": "a" * 40,
                                                "variant": "os20"}, "sha256": {}}
        for relative in (CORE + "os20.rbf_r", CORE + "loader.bin", COMMON + "os.bin"):
            self.lock["sha256"][relative] = hashlib.sha256((self.sd / relative).read_bytes()).hexdigest()
        self.tools = self.root / "tools"
        self.tools.mkdir()
        for name in NATIVES:
            (self.tools / name).write_bytes(b"native " + name.encode())
        for name in ("plugin.wasm", "prepare-data.wasm"):
            (self.tools / name).write_bytes(b"\0asm\x01\0\0\0")
        (self.tools / "plugin.json").write_text('{"name":"VVVVVV", "allowed_hosts":[]}')
        (self.tools / "plugin_icon.png").write_bytes(b"fixture icon")
        self.repo = self.root / "repo"
        self.repo.mkdir()
        self.git(self.repo, "init", "-q")
        for name in ("LICENSE.md", "License exceptions.md", "openfpga/tools/README.md",
                     "openfpga/tools/third_party/library/LICENSE",
                     "openfpga/tools/third_party/notices/Go-LICENSE", "engine.c"):
            path = self.repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture source: " + name)
        self.git(self.repo, "add", ".")
        self.git(self.repo, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "-c", "commit.gpgsign=false", "commit", "-qm", "fixture")

    def git(self, root, *args):
        env = {**os.environ, "GIT_AUTHOR_DATE": "2026-10-25T23:35:00-04:00",
               "GIT_COMMITTER_DATE": "2026-10-25T23:35:00-04:00"}
        return subprocess.check_output(["git", "-C", str(root), *args], stderr=subprocess.PIPE, env=env).decode().strip()

    def core(self):
        self.assertIsNotNone(self.module, "release assembler must exist")
        return self.module.collect_core(self.sd, "v1.2.3", "2026-10-26", self.lock)

    def release(self, output="out", **kwargs):
        self.assertIsNotNone(self.module, "release assembler must exist")
        out = self.root / output
        self.module.assemble(self.sd, self.tools, out, "v1.2.3", repo=self.repo,
                             runtime_lock=self.lock, **kwargs)
        return out

    def test_core_allowlist_excludes_owned_files_and_benchmark(self):
        for relative in (COMMON + "data.zip", COMMON + "vvvvvv-music.bin", COMMON + "vvvvvv_0.sav",
                         COMMON + "source/data.zip", "Cores/zenibako.vvvvvvbench/core.json", "logs/error.log"):
            path = self.sd / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"private")
        entries = self.core()
        self.assertEqual(set(entries), set(FILES) | {CORE + "updaters.json", CORE + "info.txt"})
        self.assertNotIn(b"private", b"".join(entries.values()))

    def test_core_metadata_preserves_ids_framework_and_inputs(self):
        entries = self.core()
        core = json.loads(entries[CORE + "core.json"])
        self.assertEqual(core["core"]["metadata"]["version"], "1.2.3")
        self.assertEqual(core["core"]["metadata"]["date_release"], "2026-10-26")
        self.assertEqual(core["core"]["metadata"]["platform_ids"], ["vvvvvv"])
        self.assertEqual(core["core"]["framework"], self.original_core["core"]["framework"])
        self.assertEqual(core["core"]["cores"], self.original_core["core"]["cores"])
        self.assertEqual(json.loads((self.sd / (CORE + "core.json")).read_text()), self.original_core)
        self.assertEqual(json.loads(entries[CORE + "updaters.json"]).get("declare_ai"),
                         ["OpenAI Codex", "Claude Code"])
        self.assertIn(b"Terry Cavanagh", entries[CORE + "info.txt"])
        self.assertIn(b"save states", entries[CORE + "info.txt"])

    def test_runtime_corruption_is_rejected(self):
        (self.sd / (COMMON + "os.bin")).write_bytes(b"corrupt")
        self.assertIsNotNone(self.module, "release assembler must exist")
        with self.assertRaisesRegex(ValueError, "SHA-256|hash"):
            self.core()

    def test_missing_allowlisted_asset_is_rejected(self):
        (self.sd / (COMMON + "vvvvvv-repo.zip")).unlink()
        self.assertIsNotNone(self.module, "release assembler must exist")
        with self.assertRaisesRegex(ValueError, "Missing|missing"):
            self.core()

    def test_expected_file_and_parent_symlinks_are_rejected(self):
        path = self.sd / (COMMON + "vvvvvv.ini")
        path.unlink()
        path.symlink_to(self.sd / (COMMON + "os.bin"))
        self.assertIsNotNone(self.module, "release assembler must exist")
        with self.assertRaisesRegex(ValueError, "[Ss]ymlink"):
            self.core()
        path.unlink()
        path.write_bytes(b"ini")
        common = self.sd / COMMON
        common.rename(self.sd / "outside")
        common.symlink_to(self.sd / "outside", target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "[Ss]ymlink"):
            self.core()

    def test_wrong_elf_class_endianness_machine_and_type_are_rejected(self):
        path = self.sd / (COMMON + "vvvvvv.elf")
        original = path.read_bytes()
        self.assertIsNotNone(self.module, "release assembler must exist")
        for offset, replacement in ((0, 0), (4, 2), (5, 2), (16, 1), (18, 62)):
            with self.subTest(offset=offset):
                data = bytearray(original)
                data[offset] = replacement
                path.write_bytes(data)
                with self.assertRaisesRegex(ValueError, "ELF|RISC-V"):
                    self.core()

    def test_release_has_complete_checksums_sources_and_executable_bundles(self):
        out = self.release()
        expected = {"vvvvvv-openfpga.zip", "plugin.wasm", "plugin.json", "plugin_icon.png", "prepare-data.wasm",
                    "SHA256SUMS", "runtime.lock.json", "PROVENANCE.json", "source.tar.gz",
                    "licenses.tar.gz", "GAME-DATA.md", "RUNTIME-SOURCE.md", "SOURCE-AVAILABILITY.json"}
        expected.update(name.removesuffix(".exe") + ".tar.gz" for name in NATIVES)
        self.assertEqual({p.name for p in out.iterdir()}, expected)
        checksums = dict(line.split("  ", 1)[::-1] for line in (out / "SHA256SUMS").read_text().splitlines())
        self.assertEqual(set(checksums), expected - {"SHA256SUMS"})
        for name, digest in checksums.items():
            self.assertEqual(hashlib.sha256((out / name).read_bytes()).hexdigest(), digest)
        with zipfile.ZipFile(out / "vvvvvv-openfpga.zip") as archive:
            self.assertIn(CORE + "core.json", archive.namelist())
            self.assertEqual(archive.read("Platforms/_images/vvvvvv.bin"),
                             (self.sd / "Platforms/_images/vvvvvv.bin").read_bytes())
            self.assertNotIn("sd/", archive.namelist())
        self.assertEqual((out / "plugin_icon.png").read_bytes(),
                         (self.tools / "plugin_icon.png").read_bytes())
        for name in NATIVES:
            with tarfile.open(out / (name.removesuffix(".exe") + ".tar.gz")) as archive:
                executable = "prepare-data.exe" if name.endswith(".exe") else "prepare-data"
                self.assertEqual(archive.getmember(executable).mode, 0o644 if name.endswith(".exe") else 0o755)
                self.assertIn("LICENSE.md", archive.getnames())
                self.assertIn("README.md", archive.getnames())
                self.assertIn("licenses/openfpga/tools/third_party/library/LICENSE", archive.getnames())
                self.assertIn("licenses/openfpga/tools/third_party/notices/Go-LICENSE", archive.getnames())
        provenance = json.loads((out / "PROVENANCE.json").read_text())
        self.assertEqual(provenance["source_commit"], self.git(self.repo, "rev-parse", "HEAD"))
        self.assertFalse(provenance["publishable"])
        self.assertFalse(provenance["runtime_source_included"])
        with zipfile.ZipFile(out / "vvvvvv-openfpga.zip") as archive:
            metadata = json.loads(archive.read(CORE + "core.json"))["core"]["metadata"]
            self.assertEqual(metadata["date_release"], "2026-10-26")

    def test_artifacts_are_byte_identical_and_ignore_untracked_private_files(self):
        (self.repo / "owned-data.zip").write_bytes(b"PRIVATE-SENTINEL")
        first = self.release("first")
        second = self.release("second")
        self.assertEqual({p.name: p.read_bytes() for p in first.iterdir()},
                         {p.name: p.read_bytes() for p in second.iterdir()})
        with tarfile.open(first / "source.tar.gz") as archive:
            self.assertIn("engine.c", archive.getnames())
            self.assertNotIn("owned-data.zip", archive.getnames())

    def test_invalid_wasm_and_manifest_hosts_are_rejected_before_output(self):
        self.assertIsNotNone(self.module, "release assembler must exist")
        for name, content in (("plugin.wasm", b"broken"),
                              ("plugin.json", b'{"allowed_hosts":["example.com"]}'),
                              ("plugin.json", b"not json")):
            with self.subTest(name=name, content=content):
                original = (self.tools / name).read_bytes()
                (self.tools / name).write_bytes(content)
                with self.assertRaisesRegex(ValueError, "[Ww][Aa][Ss][Mm]|allowed_hosts|JSON"):
                    self.release()
                self.assertFalse((self.root / "out").exists())
                (self.tools / name).write_bytes(original)

    def test_dirty_tracked_source_requires_explicit_candidate_override(self):
        (self.repo / "engine.c").write_text("changed")
        self.assertIsNotNone(self.module, "release assembler must exist")
        with self.assertRaisesRegex(ValueError, "dirty|uncommitted"):
            self.release()
        out = self.release(allow_dirty=True)
        provenance = json.loads((out / "PROVENANCE.json").read_text())
        self.assertTrue(provenance["source_dirty"])
        self.assertFalse(provenance["publishable"])

    def test_existing_output_and_nonversion_are_rejected(self):
        out = self.root / "out"
        out.mkdir()
        self.assertIsNotNone(self.module, "release assembler must exist")
        with self.assertRaisesRegex(ValueError, "exist"):
            self.release()
        with self.assertRaisesRegex(ValueError, "version"):
            self.module.collect_core(self.sd, "latest", "2026-10-26", self.lock)

    def test_runtime_source_is_exact_pinned_git_content_with_notices(self):
        runtime = self.root / "runtime"
        runtime.mkdir()
        self.git(runtime, "init", "-q")
        for name in ("LICENSE", "NOTICE", "LICENSES/MIT.txt", "Makefile", "src/firmware.c"):
            path = runtime / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("runtime " + name)
        self.git(runtime, "add", ".")
        self.git(runtime, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "-c", "commit.gpgsign=false", "commit", "-qm", "fixture runtime")
        self.lock["runtime"]["commit"] = self.git(runtime, "rev-parse", "HEAD")
        (runtime / "private.bin").write_bytes(b"private")
        out = self.release(runtime_source=runtime)
        with tarfile.open(out / "runtime-source.tar.gz") as archive:
            self.assertEqual(set(archive.getnames()), {"LICENSE", "NOTICE", "LICENSES/MIT.txt", "Makefile", "src/firmware.c"})
        with tarfile.open(out / "licenses.tar.gz") as archive:
            self.assertIn("runtime/LICENSE", archive.getnames())
            self.assertIn("runtime/NOTICE", archive.getnames())
            self.assertIn("runtime/LICENSES/MIT.txt", archive.getnames())
        self.assertTrue(json.loads((out / "PROVENANCE.json").read_text())["publishable"])
        (runtime / "NOTICE").write_text("dirty")
        with self.assertRaisesRegex(ValueError, "dirty|uncommitted"):
            self.release("dirty-runtime", runtime_source=runtime)


    def test_recursive_submodules_are_archived_at_their_pins(self):
        leaf = self.root / "leaf"
        leaf.mkdir()
        self.git(leaf, "init", "-q")
        (leaf / "LICENSE").write_text("leaf license")
        self.git(leaf, "add", ".")
        self.git(leaf, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "-c", "commit.gpgsign=false", "commit", "-qm", "leaf")
        middle = self.root / "middle"
        middle.mkdir()
        self.git(middle, "init", "-q")
        self.git(middle, "-c", "protocol.file.allow=always", "submodule", "add", "-q", str(leaf), "nested")
        self.git(middle, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "-c", "commit.gpgsign=false", "commit", "-qm", "middle")
        self.git(self.repo, "-c", "protocol.file.allow=always", "submodule", "add", "-q", str(middle), "third_party/middle")
        self.git(self.repo, "-c", "protocol.file.allow=always", "submodule", "update", "--init", "--recursive")
        self.git(self.repo, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "-c", "commit.gpgsign=false", "commit", "-qm", "submodule")
        out = self.release()
        with tarfile.open(out / "source.tar.gz") as archive:
            self.assertEqual(archive.extractfile("third_party/middle/nested/LICENSE").read(), b"leaf license")
        with tarfile.open(out / "licenses.tar.gz") as archive:
            self.assertIn("third_party/middle/nested/LICENSE", archive.getnames())
        provenance = json.loads((out / "PROVENANCE.json").read_text())
        self.assertEqual(provenance.get("source_submodules"), {
            "third_party/middle": self.git(middle, "rev-parse", "HEAD"),
            "third_party/middle/nested": self.git(leaf, "rev-parse", "HEAD")})
        self.git(self.repo, "submodule", "deinit", "-f", "--all")
        with self.assertRaisesRegex(ValueError, "initialized.*submodule"):
            self.release("missing-submodule")

    def test_runtime_source_mismatch_and_missing_notice_are_rejected(self):
        self.assertIsNotNone(self.module, "release assembler must exist")
        with self.assertRaisesRegex(ValueError, "Runtime source HEAD"):
            self.release(runtime_source=self.repo)
        self.lock["runtime"]["commit"] = self.git(self.repo, "rev-parse", "HEAD")
        with self.assertRaisesRegex(ValueError, "Runtime.*NOTICE|runtime.*notice"):
            self.release(runtime_source=self.repo)


    def test_catalog_filter_selects_only_the_core_zip(self):
        catalog = json.loads(SCRIPT.with_name("catalog.json").read_text())
        pattern = re.compile(catalog["zip_filter"])
        self.assertIsNotNone(pattern.fullmatch("vvvvvv-openfpga.zip"))
        for name in ("vvvvvv-openfpga.tar.gz", "arbitrary.zip", "vvvvvv-openfpgaXzip",
                     "prefix-vvvvvv-openfpga.zip", "vvvvvv-openfpga.zip.backup"):
            with self.subTest(name=name):
                self.assertIsNone(pattern.fullmatch(name))


    def test_provenance_hashes_the_collected_snapshot_after_sd_changes(self):
        self.assertIsNotNone(self.module, "release assembler must exist")
        ini_name = COMMON + "vvvvvv.ini"
        original_ini = (self.sd / ini_name).read_bytes()
        raw_metadata = (self.sd / (CORE + "core.json")).read_bytes()
        archive_source = self.module.archive_source

        def mutate_sd_then_archive(*args, **kwargs):
            (self.sd / ini_name).write_bytes(b"changed after collection")
            return archive_source(*args, **kwargs)

        self.module.archive_source = mutate_sd_then_archive
        out = self.release()
        provenance = json.loads((out / "PROVENANCE.json").read_text())
        with zipfile.ZipFile(out / "vvvvvv-openfpga.zip") as archive:
            self.assertEqual(archive.read(ini_name), original_ini)
            self.assertEqual(provenance["inputs_sha256"][ini_name],
                             hashlib.sha256(original_ini).hexdigest())
            self.assertEqual(provenance["inputs_sha256"][CORE + "core.json"],
                             hashlib.sha256(raw_metadata).hexdigest())
            packaged_metadata = archive.read(CORE + "core.json")
            self.assertNotEqual(packaged_metadata, raw_metadata)
            self.assertEqual(provenance["packaged_metadata_sha256"][CORE + "core.json"],
                             hashlib.sha256(packaged_metadata).hexdigest())
            self.assertEqual(provenance["packaged_metadata_sha256"][CORE + "updaters.json"],
                             hashlib.sha256(archive.read(CORE + "updaters.json")).hexdigest())


if __name__ == "__main__":
    unittest.main()
