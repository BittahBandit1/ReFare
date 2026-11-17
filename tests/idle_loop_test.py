"""The scheduler optimization must reject altered or incomplete game code."""
import pathlib
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class SchedulerGuardTest(unittest.TestCase):
    def test_instruction_guard(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        with tempfile.TemporaryDirectory(prefix='refare-idle-') as temporary:
            path = pathlib.Path(temporary)
            source = path/'guard.cpp'
            source.write_text('#include "refare_idle_loop.hpp"\nint main(int argc,char** argv) { return argc==2 && refare::HasSchedulerIdleLoop(argv[1]) ? 0 : 1; }\n')
            binary = path/'guard'
            subprocess.run([compiler,'-std=c++20','-I',str(ROOT/'src'),str(source),'-o',str(binary)],check=True)
            data = bytearray(0x114)
            for offset,value in [(0,0x100),(0x48,0x800d7274),(0x90,20)]:
                struct.pack_into('>I',data,offset,value)
            struct.pack_into('>5I',data,0x100,0x4bffa859,0x800d98b8,0x28000000,0x4182fff8,0x4bffa835)
            dol = path/'main.dol'
            def accepted(contents):
                dol.write_bytes(contents)
                return subprocess.run([str(binary),str(dol)]).returncode==0
            self.assertTrue(accepted(data))
            for index in range(5):
                changed = data.copy()
                changed[0x103+index*4] ^= 1
                self.assertFalse(accepted(changed))
            self.assertFalse(accepted(data[:-1]))
            self.assertFalse(accepted(data[:128]))
            changed = data.copy()
            struct.pack_into('>I',changed,0x48,0x800e7274)
            self.assertFalse(accepted(changed))
            changed = data.copy()
            struct.pack_into('>I',changed,0,0xfffffff0)
            self.assertFalse(accepted(changed))

if __name__ == '__main__':
    unittest.main()
