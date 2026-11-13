import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


TOOL = Path(__file__).resolve().parents[1] / "tools/prepare_source.py"
SPEC = importlib.util.spec_from_file_location("prepare_source", TOOL)
source = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(source)


class PrepareSourceTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.repo = self.root / "existing source"
        self.repo.mkdir()
        self.git("init", "--quiet")
        self.git("config", "user.name", "Source fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        (self.repo / "file").write_text("base\n")
        self.git("add", "file")
        self.git("commit", "--quiet", "-m", "fixture")
        base = self.git("rev-parse", "HEAD")
        (self.repo / "file").write_text("first\n")
        self.git("add", "file")
        first = self.git("diff", "--cached") + "\n"
        baseline = self.git("write-tree")
        (self.repo / "file").write_text("second\n")
        (self.repo / "binary").write_bytes(b"\0\xff\1")
        (self.repo / "link").symlink_to("file")
        self.git("add", "file", "binary", "link")
        second = self.git("diff", "--cached", "--binary", baseline) + "\n"
        self.data = [first.encode(), second.encode()]
        self.manifest = {
            "upstream": base, "baseline_tree": baseline,
            "candidate_tree": self.git("write-tree"),
            "baseline_patch_count": 1, "candidate_patch_count": 2,
            "patches": [{"path": f"patches/{i}.patch", "sha256": hashlib.sha256(data).hexdigest()}
                        for i, data in enumerate(self.data)],
        }
        self.public = self.root / "patch repository"
        for item, data in zip(self.manifest["patches"], self.data):
            path = self.public / item["path"]
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.manifest_path = self.public / source.MANIFEST
        self.manifest_path.parent.mkdir(parents=True)
        self.save_manifest()
        series = self.public / source.SERIES
        series.parent.mkdir(parents=True)
        series.write_text("\n".join(item["path"] for item in self.manifest["patches"]) + "\n")
        self.output = self.root / "prepared source"

    def git(self, *args):
        return source.git(self.repo, *args)

    def save_manifest(self):
        self.manifest_path.write_text(json.dumps(self.manifest))

    def test_prepare_fetches_pinned_base_and_stages_exact_tree(self):
        manifest, patches = source.load_stack(self.public)
        tree = source.prepare(self.output, manifest, patches, self.repo.as_uri())
        self.assertEqual(tree, manifest["candidate_tree"])
        self.assertEqual(source.git(self.output, "rev-parse", "HEAD"), manifest["upstream"])
        self.assertEqual((self.output / "file").read_text(), "second\n")
        self.assertEqual((self.output / "binary").read_bytes(), b"\0\xff\1")
        self.assertTrue((self.output / "link").is_symlink())
        self.assertEqual(source.git(self.output, "diff", "--name-only"), "")

    def test_index_only_preserves_dirty_source_index_refs_and_objects(self):
        (self.repo / "file").write_text("uncommitted developer work\n")
        (self.repo / "untracked").write_text("preserve\n")
        def snapshot():
            return {str(p.relative_to(self.repo)): p.read_bytes()
                    for p in self.repo.rglob("*") if p.is_file() and not p.is_symlink()}
        before = snapshot()
        self.assertEqual(source.verify_with(self.repo, self.manifest, self.data),
                         self.manifest["candidate_tree"])
        self.assertEqual(snapshot(), before)

    def test_hash_mismatch_precedes_destination_creation(self):
        (self.public / "patches/0.patch").write_text("corrupt\n")
        with patch.object(source, "ROOT", self.public), patch("sys.argv", [str(TOOL), "--output", str(self.output)]):
            self.assertEqual(source.main(), 1)
        self.assertFalse(self.output.exists())

    def test_reordered_series_rejected(self):
        (self.public / source.SERIES).write_text("patches/1.patch\npatches/0.patch\n")
        with self.assertRaisesRegex(ValueError, "order/count"):
            source.load_stack(self.public)

    def test_escaping_patch_rejected(self):
        (self.root / "outside.patch").write_bytes(self.data[0])
        (self.public / "patches/0.patch").unlink()
        (self.public / "patches/0.patch").symlink_to(self.root / "outside.patch")
        with self.assertRaisesRegex(ValueError, "escapes"):
            source.load_stack(self.public)

    def test_existing_destination_preserved(self):
        self.output.mkdir()
        (self.output / "keep").write_text("existing\n")
        with self.assertRaises(FileExistsError):
            source.prepare(self.output, self.manifest, self.data, self.repo.as_uri())
        self.assertEqual(list(self.output.iterdir()), [self.output / "keep"])
        self.assertEqual((self.output / "keep").read_text(), "existing\n")

    def test_missing_base_does_not_check_out_another_revision(self):
        self.manifest["upstream"] = "1" * 40
        with self.assertRaises(subprocess.CalledProcessError):
            source.prepare(self.output, self.manifest, self.data, self.repo.as_uri())
        self.assertFalse((self.output / "file").exists())
        self.assertTrue((self.output / ".git").is_dir())

    def test_bad_late_patch_stops_and_preserves_partial_output(self):
        with self.assertRaises(subprocess.CalledProcessError):
            source.prepare(self.output, self.manifest, [self.data[0], b"invalid\n"], self.repo.as_uri())
        self.assertEqual((self.output / "file").read_text(), "first\n")

    def test_tree_mismatches_are_fatal(self):
        for field in ("baseline_tree", "candidate_tree"):
            with self.subTest(field=field):
                manifest = dict(self.manifest, **{field: "1" * 40})
                with self.assertRaisesRegex(ValueError, "source tree"):
                    source.verify_with(self.repo, manifest, self.data)

    def test_git_environment_does_not_redirect_existing_workspace(self):
        with patch.dict("os.environ", {"GIT_DIR": str(self.root / "missing.git"),
                                      "GIT_INDEX_FILE": str(self.root / "unexpected-index")}):
            self.assertEqual(source.verify_with(self.repo, self.manifest, self.data),
                             self.manifest["candidate_tree"])
        self.assertFalse((self.root / "unexpected-index").exists())


if __name__ == "__main__":
    unittest.main()
