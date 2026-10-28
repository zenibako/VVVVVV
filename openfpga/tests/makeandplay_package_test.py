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
            "assert sys.argv[1]=='--verify'\n"
            "p=pathlib.Path(sys.argv[2])\n"
            "assert all((p/n).read_bytes()==b'verified public fixture' for n in "
            "('data.zip','vvvvvv-music.bin','makeandplay-report.json'))\n"
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

    def test_editions_coexist_without_shared_inputs_or_progress(self):
        self.assertEqual(self.package(output="standard").returncode, 0)
        mp = self.package(edition=True, output="makeandplay")
        self.assertEqual(mp.returncode, 0, mp.stderr)
        standard = self.repo / "standard"
        tree = self.repo / "makeandplay"
        core = tree / "Cores/zenibako.vvvvvvmp"
        common = tree / "Assets/vvvvvvmp/common"
        self.assertFalse((tree / "Cores/zenibako.vvvvvv").exists())
        self.assertFalse((tree / "Assets/vvvvvv").exists())
        metadata = json.loads((core / "core.json").read_text())["core"]["metadata"]
        self.assertEqual(metadata["shortname"], "vvvvvvmp")
        self.assertEqual(metadata["platform_ids"], ["vvvvvvmp"])
        slots = {s["id"]: s for s in json.loads((core / "data.json").read_text())["data"]["data_slots"]}
        self.assertEqual(slots[0]["filename"], "vvvvvv-mp.json")
        self.assertEqual(slots[3]["filename"], "vvvvvv-mp.elf")
        self.assertEqual(slots[10]["filename"], "vvvvvv-mp_0.sav")
        instance = json.loads((tree / "Assets/vvvvvvmp/zenibako.vvvvvvmp/vvvvvv-mp.json").read_text())
        instance_slots = {s["id"]: s["filename"] for s in instance["instance"]["data_slots"]}
        self.assertEqual(instance_slots[10], slots[10]["filename"])
        self.assertIn("ELF=vvvvvv-mp.elf", (common / "vvvvvv-mp.ini").read_text())
        self.assertEqual((common / "data.zip").read_bytes(), b"verified public fixture")
        self.assertTrue((common / "vvvvvv-music.bin").exists())
        self.assertTrue((common / "makeandplay-report.json").exists())
        self.assertTrue((core / "License-exceptions.txt").exists())
        self.assertTrue((core / "MAKE-AND-PLAY.txt").exists())
        self.assertEqual((tree / "Platforms/_images/vvvvvvmp.bin").read_bytes(),
                         (ROOT / "dist/platforms/_images/vvvvvv.bin").read_bytes())
        self.assertIn("Make and Play", (tree / "Platforms/vvvvvvmp.json").read_text())
        self.assertFalse((standard / "Assets/vvvvvv/common/data.zip").exists())
        self.assertFalse((standard / "Assets/vvvvvvmp").exists())
        self.assertIn("vvvvvv_0.sav", (standard / "Cores/zenibako.vvvvvv/data.json").read_text())
        with zipfile.ZipFile(common / "vvvvvv-repo.zip") as z:
            self.assertIn("graphics/font.png", z.namelist())

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
