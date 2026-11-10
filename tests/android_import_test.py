"""Run the Android importer against synthetic ISO/CISO images on the host JVM."""
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class AndroidImporterTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  if not shutil.which('javac'): raise unittest.SkipTest('JDK required')
  cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
  cls.root=Path(cls.temp.name)
  subprocess.run(['javac','-d',str(cls.root),str(ROOT/'ports/android/java/io/github/bittahbandit1/refare/DiscImporter.java'),str(ROOT/'tests/AndroidImportTest.java')],check=True)
 def test_iso_ciso_and_rejected_imports(self):
  image=bytearray(0x10000)
  def word(offset,value): struct.pack_into('>I',image,offset,value)
  image[:6]=b'GCTE51';word(0x1c,0xc2339f3d);word(0x420,0x3000);word(0x424,0x4000);word(0x428,50)
  word(0x3000,0x100);word(0x3048,0x80004000);word(0x3090,4);word(0x3100,0x12345678)
  digest=hashlib.sha256(image[0x3000:0x3104]).hexdigest()
  word(0x4000,0x01000000);word(0x4008,3);word(0x400c,0x01000000);word(0x4014,3)
  word(0x4018,4);word(0x401c,0x5000);word(0x4020,5)
  image[0x4024:0x4032]=b'dir\0hello.bin\0';image[0x5000:0x5005]=b'hello'
  def invoke(data,name,success=True,expected=digest):
   source=self.root/(name+'.iso');source.write_bytes(data);dest=self.root/name
   result=subprocess.run(['java','-cp',str(self.root),'io.github.bittahbandit1.refare.AndroidImportTest',str(source),str(dest),expected],capture_output=True,text=True)
   self.assertEqual(result.returncode==0,success,result.stderr)
   if success:self.assertEqual((dest/'files/dir/hello.bin').read_bytes(),b'hello')
   else:self.assertFalse(dest.exists())
   return source,dest
  source,dest=invoke(image,'iso')
  again=subprocess.run(['java','-cp',str(self.root),'io.github.bittahbandit1.refare.AndroidImportTest',str(source),str(dest),digest],capture_output=True,text=True)
  self.assertNotEqual(again.returncode,0);self.assertEqual((dest/'files/dir/hello.bin').read_bytes(),b'hello')
  header=bytearray(0x8000);header[:4]=b'CISO';struct.pack_into('<I',header,4,0x10000);header[8]=1
  invoke(header+image,'ciso');invoke(image,'hash',False,'0'*64)
  unsafe=bytearray(image);unsafe[0x4024:0x4027]=b'../';invoke(unsafe,'traversal',False)
  invoke(header+image[:100],'truncated',False)
  self.assertFalse(list(self.root.glob('import-*')))
if __name__=='__main__':unittest.main()
