"""Exercise edition isolation through the actual SD packager, without game data."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class EditionPackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name)
        self.port = self.repo / "openfpga"
        (self.port / "scripts").mkdir(parents=True)
        shutil.copyfile(ROOT / "scripts/package.sh", self.port / "scripts/package.sh")
        shutil.copytree(ROOT / "dist", self.port / "dist")
        for path in ("desktop_version/fonts", "desktop_version/lang", "openfpga/fonts",
                     "openfpga/.deps/pocket-runtime", "openfpga/release", "public"):
            (self.repo / path).mkdir(parents=True, exist_ok=True)
        (self.repo / "desktop_version/fonts/font.png").write_bytes(b"source font")
        (self.repo / "LICENSE.md").write_text("fixture game license")
        (self.repo / "License exceptions.md").write_text("fixture Make and Play exception")
        for name in ("os20.rbf_r", "loader.bin", "os.bin"):
            (self.port / ".deps/pocket-runtime" / name).write_bytes(name.encode())
        self.elf = self.repo / "app.elf"
        self.elf.write_bytes(b"fixture ELF: MAKE AND PLAY EDITION")
        self.assets = self.repo / "public"
        (self.assets / "notices").mkdir()
        (self.assets / "notices/LICENSE").write_text("public distribution notice")
        for name in ("data.zip", "vvvvvv-music.bin", "makeandplay-report.json"):
            (self.assets / name).write_bytes(b"verified public fixture")
        # Model the external verifier contract; its real digest checks have
        # independent tests in release/test_makeandplay.py.
        (self.port / "release/makeandplay.py").write_text(
            "import pathlib,sys\n"
            "assert sys.argv[1] in ('--verify','--verify-packaged')\n"
            "p=pathlib.Path(sys.argv[2])\n"
            "names=('vvvvvv-mp-data.zip','vvvvvv-mp-music.bin') if sys.argv[1]=='--verify-packaged' else ('data.zip','vvvvvv-music.bin')\n"
            "assert all((p/n).read_bytes()==b'verified public fixture' for n in names+('makeandplay-report.json',))\n"
            "assert (p/'notices/LICENSE').read_text()=='public distribution notice'\n"
            "if (p/'swap-after-verification').exists():\n"
            "    (p/'data.zip').write_bytes(b'changed after verification')\n")

    def package(self, edition=False, bench=False, output="sd", **extra):
        env = {**os.environ, "MAKEANDPLAY": "1" if edition else "0",
               "BENCH": "1" if bench else "0", "PACKAGE_ELF": str(self.elf),
               "PACKAGE_OUT": str(self.repo / output), "MAKEANDPLAY_ASSETS": str(self.assets),
               **extra}
        return subprocess.run(["bash", str(self.port / "scripts/package.sh")],
                              env=env, capture_output=True, text=True)

    def test_editions_share_one_core_with_picker_and_isolated_progress(self):
        full = self.package(output="standard")
        self.assertEqual(full.returncode, 0, full.stderr)
        mp = self.package(edition=True, output="makeandplay")
        self.assertEqual(mp.returncode, 0, mp.stderr)
        standard = self.repo / "standard"
        tree = self.repo / "makeandplay"
        core = tree / "Cores/zenibako.vvvvvv"
        common = tree / "Assets/vvvvvv/common"
        public = common / "mp"
        self.assertFalse((tree / "Cores/zenibako.vvvvvvmp").exists())
        self.assertFalse((tree / "Assets/vvvvvvmp").exists())
        metadata = json.loads((core / "core.json").read_text())["core"]["metadata"]
        self.assertEqual(metadata["shortname"], "vvvvvv")
        self.assertEqual(metadata["platform_ids"], ["vvvvvv"])
        slots = {s["id"]: s for s in json.loads((core / "data.json").read_text())["data"]["data_slots"]}
        self.assertNotIn("filename", slots[0])
        self.assertEqual(slots[0]["extensions"], ["json"])
        self.assertEqual(slots[0]["parameters"], 275)
        self.assertEqual(slots[3]["filename"], "vvvvvv.elf")
        self.assertEqual(slots[10]["filename"], "vvvvvv_0.sav")
        instance = json.loads((tree / "Assets/vvvvvv/zenibako.vvvvvv/vvvvvv-mp.json").read_text())
        instance_slots = {s["id"]: s["filename"] for s in instance["instance"]["data_slots"]}
        self.assertEqual(instance_slots, {1: "os.bin", 2: "vvvvvv-mp.ini", 3: "vvvvvv-mp.elf",
                                         4: "mp/vvvvvv-mp-data.zip", 5: "vvvvvv-repo.zip",
                                         6: "mp/vvvvvv-mp-music.bin", 10: "vvvvvv-mp_0.sav"})
        ini = (common / "vvvvvv-mp.ini").read_text()
        self.assertIn("ELF=vvvvvv-mp.elf", ini)
        self.assertIn("ARGS=-assets mp/vvvvvv-mp-data.zip", ini)
        self.assertEqual((public / "vvvvvv-mp-data.zip").read_bytes(), b"verified public fixture")
        self.assertTrue((public / "vvvvvv-mp-music.bin").exists())
        self.assertTrue((public / "makeandplay-report.json").exists())
        self.assertTrue((core / "License-exceptions.txt").exists())
        self.assertTrue((core / "MAKE-AND-PLAY.txt").exists())
        self.assertEqual((tree / "Platforms/_images/vvvvvv.bin").read_bytes(),
                         (ROOT / "dist/platforms/_images/vvvvvv.bin").read_bytes())
        self.assertFalse((standard / "Assets/vvvvvv/common/data.zip").exists())
        self.assertFalse((standard / "Assets/vvvvvv/common/mp").exists())
        full_instance = json.loads((standard / "Assets/vvvvvv/zenibako.vvvvvv/vvvvvv.json").read_text())
        self.assertEqual({s["id"]: s["filename"] for s in full_instance["instance"]["data_slots"]}[10],
                         "vvvvvv_0.sav")
        self.assertFalse((common / "data.zip").exists())
        self.assertFalse((common / "vvvvvv.elf").exists())
        # Shared core/runtime definitions must merge without choosing one
        # edition's version over the other. Only the edition files differ.
        full_files = {p.relative_to(standard) for p in standard.rglob("*") if p.is_file()}
        mp_files = {p.relative_to(tree) for p in tree.rglob("*") if p.is_file()}
        shared = full_files & mp_files
        self.assertIn(Path("Cores/zenibako.vvvvvv/data.json"), shared)
        for relative in shared:
            self.assertEqual((standard / relative).read_bytes(), (tree / relative).read_bytes(),
                             str(relative))
        with zipfile.ZipFile(common / "vvvvvv-repo.zip") as z:
            self.assertIn("graphics/font.png", z.namelist())

    def test_benchmark_keeps_its_fixed_autostart_instance(self):
        result = self.package(bench=True, output="benchmark")
        self.assertEqual(result.returncode, 0, result.stderr)
        core = self.repo / "benchmark/Cores/zenibako.vvvvvvbench"
        slots = {s["id"]: s for s in json.loads((core / "data.json").read_text())["data"]["data_slots"]}
        self.assertEqual(slots[0]["filename"], "vvvvvv-bench.json")
        self.assertEqual(slots[3]["filename"], "vvvvvv-bench.elf")
        self.assertEqual(slots[10]["filename"], "vvvvvv-bench_0.sav")

    def test_rejects_unverified_assets_before_replacing_output(self):
        (self.assets / "data.zip").write_bytes(b"wrong input")
        output = self.repo / "preserved"
        output.mkdir()
        sentinel = output / "sentinel"
        sentinel.write_text("keep")
        result = self.package(edition=True, output="preserved")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(sentinel.read_text(), "keep")

    def test_rejects_standard_binary_for_bundled_public_assets(self):
        self.elf.write_bytes(b"standard edition ELF")
        self.assertNotEqual(self.package(edition=True).returncode, 0)

    def test_rejects_missing_notices_before_replacing_output(self):
        (self.assets / "notices/LICENSE").unlink()
        output = self.repo / "preserved"
        output.mkdir()
        sentinel = output / "sentinel"
        sentinel.write_text("keep")
        self.assertNotEqual(self.package(edition=True, output="preserved").returncode, 0)
        self.assertEqual(sentinel.read_text(), "keep")

    def test_rechecks_copied_assets_before_replacing_output(self):
        (self.assets / "swap-after-verification").touch()
        output = self.repo / "preserved"
        output.mkdir()
        sentinel = output / "sentinel"
        sentinel.write_text("keep")
        self.assertNotEqual(self.package(edition=True, output="preserved").returncode, 0)
        self.assertEqual(sentinel.read_text(), "keep")

    def test_makeandplay_cannot_reuse_campaign_benchmark(self):
        self.assertNotEqual(self.package(edition=True, bench=True).returncode, 0)

    def test_build_selects_edition_in_both_compilers_and_isolates_objects(self):
        target = "print-edition: ; @printf '%s\\n' '$(BUILD_DIR)' '$(OBJ_DIR)' '$(OPENFPGA_DEFS)' '$(HOST_DEFS)'"
        args = ["make", "-s", "-C", str(ROOT), "MAKEANDPLAY=1", "-f", "Makefile", "-f", "-"]
        result = subprocess.run(args + ["print-edition"], input=target + "\n", capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.strip().splitlines()
        self.assertIn("build/makeandplay", lines[0])
        self.assertIn("build/makeandplay/obj", lines[1])
        self.assertIn("-DMAKEANDPLAY", lines[2])
        self.assertIn("-DMAKEANDPLAY", lines[3])
        bad = subprocess.run(args + ["BENCH=1", "print-edition"], input=target + "\n", capture_output=True, text=True)
        self.assertNotEqual(bad.returncode, 0)


if __name__ == "__main__":
    unittest.main()
