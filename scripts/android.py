#!/usr/bin/env python3
"""Build an ARM64 Android APK that compiles each user's imported game locally."""
import argparse
from pathlib import Path
import os,shutil,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[1]
def run(*args,**kwargs):subprocess.run([str(a) for a in args],cwd=ROOT,check=True,**kwargs)
def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--sdk',type=Path,default=ROOT/'build/android-sdk')
 parser.add_argument('--jobs',type=int,default=6)
 parser.add_argument('--package-only',action='store_true')
 args=parser.parse_args()
 sdk=args.sdk.resolve();build=ROOT/'build/ports/android-arm64';tools=sdk/'build-tools/35.0.0';ndk=sdk/'ndk/27.3.13750724'
 java=Path(subprocess.check_output(['/usr/libexec/java_home'],text=True).strip())
 env=dict(os.environ,JAVA_HOME=str(java))
 if not args.package_only:
  run('python3',ROOT/'scripts/apply-patches.py')
  run('cmake','-S',ROOT/'ports/android','-B',build,'-G','Ninja',
      '-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),
      '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DCMAKE_BUILD_TYPE=Release')
  run('cmake','--build',build,'--target','refare','dolrecomp','-j',args.jobs)
 staging=build/'apk-stage'
 if staging.exists():shutil.rmtree(staging)
 assets=staging/'assets';assets.mkdir(parents=True)
 shutil.copy2(ROOT/'assets/branding/ReFareLogo.png',assets/'ReFareLogo.png')
 shutil.copy2(ROOT/'assets/branding/ReFareUI.ttf',assets/'ReFareUI.ttf')
 shutil.copy2(ROOT/'assets/branding/NOTICE.txt',assets/'ReFareBranding.txt')
 shutil.copytree(ROOT/'lib/ModernGekko/vendor/dolphin/Data/Sys',assets/'Sys')
 (assets/'GFX.ini').write_text((ROOT/'config/GFX.ini').read_text().replace('InternalResolution = 3','InternalResolution = 1'))
 run('cmake','-DREFARE_STARTUP_JSON='+str(ROOT/'config/startup-patches.json'),
     '-DREFARE_STARTUP_CSV='+str(assets/'startup-patches.csv'),'-P',ROOT/'ports/ios/StartupPatches.cmake')
 classes=staging/'classes';classes.mkdir()
 sources=sorted((ROOT/'ports/android/java').rglob('*.java'))
 run(java/'bin/javac','-source','8','-target','8','-classpath',sdk/'platforms/android-35/android.jar','-d',classes,*sources)
 dex=staging/'dex';dex.mkdir()
 run(tools/'d8','--min-api','26','--lib',sdk/'platforms/android-35/android.jar','--output',dex,*sorted(classes.rglob('*.class')),env=env)
 unsigned=staging/'unsigned.apk'
 resources=staging/'res'
 for density,size in [('mdpi',48),('hdpi',72),('xhdpi',96),('xxhdpi',144),('xxxhdpi',192)]:
  directory=resources/('mipmap-'+density);directory.mkdir(parents=True)
  shutil.copy2(ROOT/f'assets/icons/icon-{size}.png',directory/'refare.png')
 compiled_resources=staging/'icons.zip'
 run(tools/'aapt2','compile','--dir',resources,'-o',compiled_resources)
 native=staging/'native';native.mkdir()
 from android_toolchain import prepare
 prepare(sdk,build,assets,native)
 lib=native/'librefare.so';shutil.copy2(build/'librefare.so',lib)
 llvm=ndk/'toolchains/llvm/prebuilt/darwin-x86_64'
 run(llvm/'bin/llvm-strip','--strip-unneeded',lib)
 symbols=subprocess.check_output([str(llvm/'bin/llvm-nm'),'--dynamic','--defined-only',str(lib)],text=True)
 if 'staticrecomp_get_module' in symbols:raise ValueError('Refusing to package a runtime containing a precompiled game module')
 run(tools/'aapt2','link','--manifest',ROOT/'ports/android/AndroidManifest.xml','-I',sdk/'platforms/android-35/android.jar','-A',assets,'-o',unsigned,compiled_resources)
 with zipfile.ZipFile(unsigned,'a',zipfile.ZIP_DEFLATED) as archive:
  archive.write(dex/'classes.dex','classes.dex')
  for binary in sorted(native.glob('*.so')):archive.write(binary,'lib/arm64-v8a/'+binary.name)
 aligned=staging/'aligned.apk';run(tools/'zipalign','-f','-p','4',unsigned,aligned)
 key=ROOT/'build/android-tools/refare-debug.keystore'
 if not key.exists():run(java/'bin/keytool','-genkeypair','-keystore',key,'-storepass','android','-keypass','android',
                         '-alias','androiddebugkey','-keyalg','RSA','-keysize','2048','-validity','10000','-dname','CN=ReFare Local Test',env=env)
 output=ROOT/'build/release/ReFare-Android-arm64-test.apk';output.parent.mkdir(parents=True,exist_ok=True)
 run(tools/'apksigner','sign','--ks',key,'--ks-key-alias','androiddebugkey','--ks-pass','pass:android',
     '--key-pass','pass:android','--out',output,aligned,env=env)
 run(tools/'apksigner','verify','--verbose',output,env=env)
 print('APK:',output)
if __name__=='__main__':main()
