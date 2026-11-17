"""Check local Android module preparation without any proprietary game inputs."""
from pathlib import Path
import re,shutil,struct,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class AndroidCompilationTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  if not shutil.which('javac'):raise unittest.SkipTest('JDK required')
  cls.temporary=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temporary.cleanup);cls.folder=Path(cls.temporary.name)
  java=ROOT/'ports/android/java/io/github/bittahbandit1/refare'
  subprocess.run(['javac','-d',str(cls.folder),str(java/'ModuleTables.java'),str(java/'GameCompiler.java'),str(ROOT/'tests/AndroidCompilationTest.java')],check=True)
 def invoke(self,*args):subprocess.run(['java','-ea','-cp',str(self.folder),'io.github.bittahbandit1.refare.AndroidCompilationTest',*map(str,args)],check=True)
 def test_cache_and_install(self):self.invoke('cache',self.folder/'cache-test')
 def test_tables_match_reference(self):
  for compact in [False,True]:
   generated=self.folder/str(compact);generated.mkdir()
   header='void func_80003100(CPUState* ctx);\nvoid func_80003108(CPUState* ctx);\n'
   header+='u32 offset = address - 0x80003100u;\nif (offset < 0x10u)' if compact else 'address >= 0x80003100u && address < 0x80003110u'
   (generated/'generated.h').write_text(header);(generated/'generated_smc.txt').write_text('0x80003104-0x80003108\n')
   dol=bytearray(0x110)
   for offset,value in [(0,0x100),(0x48,0x80003100),(0x90,0x10)]:struct.pack_into('>I',dol,offset,value)
   dol[0x100:]=bytes(range(16));(generated/'main.dol').write_bytes(dol)
   output=generated/'java.inc';reference=generated/'python.inc';self.invoke('tables',generated,output)
   subprocess.run([sys.executable,str(ROOT/'lib/ModernGekko/vendor/dolphin/module-template/gen_module_tables.py'),str(generated/'generated.h'),str(generated/'generated_smc.txt'),str(generated/'main.dol'),str(reference)],check=True)
   normalize=lambda p:re.sub(r'\s+','',re.sub(r'//[^\n]*','',p.read_text()))
   self.assertEqual(normalize(output),normalize(reference))
if __name__=='__main__':unittest.main()
