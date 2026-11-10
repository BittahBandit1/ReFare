import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('apply_refare_patches', ROOT/'scripts/apply-patches.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class PatchStackTests(unittest.TestCase):
    def test_overlapping_stack_is_detected_without_changing_source(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            repo = folder/'source'; repo.mkdir()
            source = repo/'file.txt'; source.write_text('final\n')
            patches = []
            for index, (old, new) in enumerate([('original','middle'),('middle','final')]):
                patch = folder/f'{index}.patch'
                patch.write_text(f'--- a/file.txt\n+++ b/file.txt\n@@ -1 +1 @@\n-{old}\n+{new}\n')
                patches.append(patch)
            self.assertTrue(module.stack_is_applied(repo, patches))
            self.assertEqual(source.read_text(), 'final\n')
            source.write_text('middle\n')
            self.assertFalse(module.stack_is_applied(repo, patches))
            self.assertEqual(source.read_text(), 'middle\n')
