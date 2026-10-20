import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[1] / "tools/verify_patch_chain.py"
SPEC = importlib.util.spec_from_file_location("verify_patch_chain", TOOL)
chain = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(chain)


class PatchChainTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo = self.root / "source with spaces"
        self.repo.mkdir()
        self.git("init", "--quiet")
        self.git("config", "user.name", "Gate test")
        self.git("config", "user.email", "gate@example.invalid")
        for name in ("changed", "untouched", "deleted", "mode"):
            (self.repo / name).write_text("base\n")
        self.git("add", ".")
        self.git("commit", "--quiet", "-m", "fixture")
        self.base = self.git("rev-parse", "HEAD").decode().strip()
        (self.repo / "changed").write_text("candidate\n")
        (self.repo / "deleted").unlink()
        (self.repo / "binary new").write_bytes(b"\0\1\xff")
        (self.repo / "link").symlink_to("changed")
        self.git("add", ".")
        self.patch = self.root / "candidate.patch"
        self.patch.write_bytes(self.git("diff", "--cached", "--binary"))

    def git(self, *args):
        return subprocess.check_output(["git", *args], cwd=self.repo, stderr=subprocess.PIPE)

    def check(self, exclusions=(), allow_root_doc_pointer_mode=False):
        return chain.verify(self.repo, self.base, [self.patch], list(exclusions),
                            allow_root_doc_pointer_mode)

    def root_doc_pointer(self, name="NOTES.md"):
        path = self.repo / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.symlink_to("changed")
        self.git("add", name)
        self.patch.write_bytes(self.git("diff", "--cached", "--binary"))
        path.unlink()
        path.write_text("changed")
        return path

    def test_root_doc_pointer_mode_is_explicit(self):
        self.root_doc_pointer()
        self.assertFalse(self.check()["ok"])
        result = self.check(allow_root_doc_pointer_mode=True)
        self.assertTrue(result["ok"])
        self.assertEqual(result["root_doc_pointer_mode_differences"], 1)

    def test_root_doc_pointer_content_remains_strict(self):
        self.root_doc_pointer().write_text("untouched")
        self.assertFalse(self.check(allow_root_doc_pointer_mode=True)["ok"])

    def test_root_doc_pointer_exception_does_not_cover_source(self):
        for name in ("src/NOTES.md", "source.c"):
            with self.subTest(name=name):
                path = self.root_doc_pointer(name)
                self.assertFalse(self.check(allow_root_doc_pointer_mode=True)["ok"])
                path.unlink()
                path.symlink_to("changed")

    def test_identical_stack_and_source(self):
        self.assertTrue(self.check()["ok"])

    def test_modified_file_outside_patch(self):
        (self.repo / "untouched").write_text("unrecorded change\n")
        self.assertEqual([d["path"] for d in self.check()["differences"]], ["untouched"])

    def test_added_source_outside_patch(self):
        (self.repo / "forgotten.c").write_text("int forgotten;\n")
        self.assertFalse(self.check()["ok"])
        result = self.check(["forgotten.c"])
        self.assertTrue(result["ok"])
        self.assertEqual(result["excluded_files"], ["forgotten.c"])

    def test_missing_expected_file(self):
        (self.repo / "binary new").unlink()
        self.assertFalse(self.check()["ok"])

    def test_deleted_file_reintroduced(self):
        (self.repo / "deleted").write_text("unexpected\n")
        self.assertFalse(self.check()["ok"])

    def test_mode_and_symlink_changes(self):
        (self.repo / "mode").chmod(0o755)
        (self.repo / "link").unlink()
        (self.repo / "link").symlink_to("untouched")
        self.assertEqual({d["path"] for d in self.check()["differences"]}, {"mode", "link"})

    def test_cli_mismatch_is_failure(self):
        (self.repo / "changed").write_text("wrong\n")
        result = subprocess.run(["python3", str(TOOL), "--repo", str(self.repo),
                                 "--base", self.base, str(self.patch)], capture_output=True)
        self.assertEqual(result.returncode, 1)

    def test_checkout_line_endings(self):
        (self.repo / ".gitattributes").write_text("*.csv text eol=crlf\n")
        (self.repo / "data.csv").write_bytes(b"one,two\r\n")
        self.git("add", ".gitattributes", "data.csv")
        self.patch.write_bytes(self.git("diff", "--cached", "--binary"))
        self.assertTrue(self.check()["ok"])

    def test_cli_bad_patch_is_error(self):
        self.patch.write_text("not a patch\n")
        result = subprocess.run(["python3", str(TOOL), "--repo", str(self.repo),
                                 "--base", self.base, str(self.patch)], capture_output=True)
        self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()
