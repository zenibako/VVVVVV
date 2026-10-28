"""Public Make and Play provenance gates, using synthetic archives only."""
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

SCRIPT = Path(__file__).with_name("makeandplay.py")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def zip_bytes(entries):
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as archive:
        for name, data in entries:
            archive.writestr(name, data)
    return out.getvalue()


class MakeAndPlayTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(SCRIPT.exists(), "locked Make and Play preparation helper is missing")
        spec = importlib.util.spec_from_file_location("makeandplay_test_subject", SCRIPT)
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.data = zip_bytes([("levels/sample.vvvvvv", b"player level"),
                               ("vvvvvvmusic.vvv", b"synthetic tone")])
        self.music = b"synthetic converted OFM2"
        self.notice = b"Fixture public distribution copyright notice"
        self.archive_bytes = zip_bytes([("data.zip", self.data),
                                        ("licenses/fixture.txt", self.notice),
                                        ("unselected-executable", b"do not extract")])
        self.archive = self.root / "official.zip"
        self.archive.write_bytes(self.archive_bytes)
        self.output = self.root / "prepared"
        self.lock = {
            "schema": 1, "edition": "makeandplay", "version": "fixture",
            "archive": {"url": "https://example.invalid/official.zip",
                        "size": len(self.archive_bytes), "sha256": digest(self.archive_bytes)},
            "data": {"path": "data.zip", "size": len(self.data), "sha256": digest(self.data)},
            "music": {"path": "vvvvvv-music.bin", "size": len(self.music), "sha256": digest(self.music)},
            "levels": ["levels/sample.vvvvvv"],
            "notices": {"licenses/fixture.txt": digest(self.notice)},
        }
        self.converter = self.root / "converter"
        self.converter.write_text("#!" + sys.executable + "\n"
                                  "import pathlib,sys\n"
                                  "s,o=map(pathlib.Path,sys.argv[1:]);o.mkdir()\n"
                                  "(o/'data.zip').write_bytes((s/'data.zip').read_bytes())\n"
                                  "(o/'vvvvvv-music.bin').write_bytes(" + repr(self.music) + ")\n"
                                  "(o/'prepare-report.json').write_text(str(s))\n")
        self.converter.chmod(0o755)

    def prepare(self, lock=None):
        return self.module.prepare(self.converter, self.output, self.archive,
                                   lock=self.lock if lock is None else lock)

    def test_prepares_only_pinned_assets_and_sanitized_public_provenance(self):
        report = self.prepare()
        self.assertEqual(self.output.joinpath("data.zip").read_bytes(), self.data)
        self.assertEqual(self.output.joinpath("vvvvvv-music.bin").read_bytes(), self.music)
        self.assertEqual(self.output.joinpath("notices/licenses/fixture.txt").read_bytes(), self.notice)
        self.assertFalse(self.output.joinpath("unselected-executable").exists())
        self.assertFalse(self.output.joinpath("prepare-report.json").exists())
        self.assertNotIn(str(self.root), json.dumps(report))
        self.assertEqual(report["source"], self.lock["archive"])
        self.assertEqual(report["levels"], ["levels/sample.vvvvvv"])
        self.assertEqual(self.module.validate_prepared(self.output, self.lock), report)

    def test_rejects_wrong_archive_digest_without_publishing_output(self):
        self.archive.write_bytes(b"not the official archive")
        with self.assertRaisesRegex(ValueError, "archive|Archive"):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_rejects_wrong_nested_data_digest(self):
        lock = copy.deepcopy(self.lock)
        lock["data"]["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "data.zip|data digest"):
            self.prepare(lock)
        self.assertFalse(self.output.exists())

    def test_rejects_unexpected_level_inventory(self):
        lock = copy.deepcopy(self.lock)
        lock["levels"] = ["levels/other.vvvvvv"]
        with self.assertRaisesRegex(ValueError, "level"):
            self.prepare(lock)
        self.assertFalse(self.output.exists())

    def test_rejects_changed_converter_music_before_publishing(self):
        lock = copy.deepcopy(self.lock)
        lock["music"]["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "music|vvvvvv-music"):
            self.prepare(lock)
        self.assertFalse(self.output.exists())

    def test_refuses_output_collision_before_reading_archive(self):
        self.output.mkdir()
        sentinel = self.output / "existing"
        sentinel.write_bytes(b"preserve")
        self.archive.unlink()
        with self.assertRaisesRegex(ValueError, "exists"):
            self.prepare()
        self.assertEqual(sentinel.read_bytes(), b"preserve")

    def test_verify_rejects_tampered_report_even_when_files_are_unchanged(self):
        self.prepare()
        path = self.output / "makeandplay-report.json"
        report = json.loads(path.read_text())
        report["host_path"] = "/private/customer/assets"
        path.write_text(json.dumps(report))
        with self.assertRaisesRegex(ValueError, "report|provenance"):
            self.module.validate_prepared(self.output, self.lock)

    def test_verify_rejects_changed_music(self):
        self.prepare()
        self.output.joinpath("vvvvvv-music.bin").write_bytes(b"different")
        with self.assertRaisesRegex(ValueError, "music|vvvvvv-music"):
            self.module.validate_prepared(self.output, self.lock)

    def test_verify_tolerates_common_apf_files_with_verified_notices(self):
        report = self.prepare()
        self.output.joinpath("os.bin").write_bytes(b"APF runtime")
        self.output.joinpath("vvvvvv-mp.ini").write_text("[os]\n")
        self.assertEqual(self.module.validate_prepared(self.output, self.lock), report)

    def test_verify_rejects_missing_locked_notice(self):
        self.prepare()
        self.output.joinpath("notices/licenses/fixture.txt").unlink()
        with self.assertRaisesRegex(ValueError, "notice|fixture.txt"):
            self.module.validate_prepared(self.output, self.lock)

    def test_verify_rejects_changed_locked_notice(self):
        self.prepare()
        self.output.joinpath("notices/licenses/fixture.txt").write_bytes(b"altered notice")
        with self.assertRaisesRegex(ValueError, "notice|fixture.txt"):
            self.module.validate_prepared(self.output, self.lock)

    def test_preparation_rejects_wrong_public_notice_digest(self):
        lock = copy.deepcopy(self.lock)
        lock['notices']['licenses/fixture.txt'] = '0' * 64
        with self.assertRaisesRegex(ValueError, 'notice'):
            self.prepare(lock)
        self.assertFalse(self.output.exists())

    def test_rejects_symlink_prepared_files(self):
        self.prepare()
        target = self.root / "outside"
        target.write_bytes(self.data)
        self.output.joinpath("data.zip").unlink()
        self.output.joinpath("data.zip").symlink_to(target)
        with self.assertRaisesRegex(ValueError, "[Ss]ymlink"):
            self.module.validate_prepared(self.output, self.lock)

    def test_rejects_duplicate_or_unsafe_nested_members(self):
        for entries in ([('levels/sample.vvvvvv', b'a'), ('../escape', b'x')],
                        [('levels/sample.vvvvvv', b'a'), ('LEVELS/SAMPLE.VVVVVV', b'b')]):
            data = zip_bytes(entries)
            archive = zip_bytes([('data.zip', data), ('licenses/fixture.txt', self.notice)])
            self.archive.write_bytes(archive)
            lock = copy.deepcopy(self.lock)
            lock['archive'].update(size=len(archive), sha256=digest(archive))
            lock['data'].update(size=len(data), sha256=digest(data))
            with self.assertRaisesRegex(ValueError, 'unsafe|duplicate'):
                self.prepare(lock)

    def test_bounded_read_rejects_archive_larger_than_pin(self):
        self.archive.write_bytes(self.archive_bytes + b'excess')
        with self.assertRaisesRegex(ValueError, 'size|archive'):
            self.prepare()

    def test_rejects_oversized_selected_notice_without_extracting(self):
        notice = b'x' * (1024 * 1024 + 1)
        archive = zip_bytes([('data.zip', self.data), ('licenses/fixture.txt', notice)])
        self.archive.write_bytes(archive)
        lock = copy.deepcopy(self.lock)
        lock['archive'].update(size=len(archive), sha256=digest(archive))
        lock['notices']['licenses/fixture.txt'] = digest(notice)
        with self.assertRaisesRegex(ValueError, 'limit'):
            self.prepare(lock)
        self.assertFalse(self.output.exists())

    def test_converter_failure_leaves_output_unpublished(self):
        self.converter.write_text('#!' + sys.executable + '\nimport sys\nsys.exit(1)\n')
        with self.assertRaisesRegex(ValueError, 'preparation failed'):
            self.prepare()
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob('.makeandplay-*')))

    def test_load_lock_and_cli_verify_production_lock(self):
        lock = self.module.load_lock()
        self.assertEqual(lock['edition'], 'makeandplay')
        self.assertEqual(len(lock['levels']), 17)
        # Real CLI option parsing must reject combining read-only verification
        # with a converter or accepting any alternate-lock escape hatch.
        import subprocess
        result = subprocess.run([sys.executable, str(SCRIPT), '--verify', str(self.root),
                                 '--converter', str(self.converter)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        result = subprocess.run([sys.executable, str(SCRIPT), '--lock', str(self.archive)],
                                capture_output=True)
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
