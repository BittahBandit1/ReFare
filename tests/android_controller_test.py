"""Exercise Android hat-axis mapping without requiring a connected device."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class AndroidControllerTests(unittest.TestCase):
 def test_hat_mapping(self):
  if not shutil.which('javac'):raise unittest.SkipTest('JDK required')
  with tempfile.TemporaryDirectory() as directory:
   subprocess.run(['javac','-d',directory,str(ROOT/'ports/android/java/io/github/bittahbandit1/refare/ControllerMapping.java'),str(ROOT/'tests/AndroidControllerMappingTest.java')],check=True)
   subprocess.run(['java','-ea','-cp',directory,'io.github.bittahbandit1.refare.AndroidControllerMappingTest'],check=True)
if __name__=='__main__':unittest.main()
