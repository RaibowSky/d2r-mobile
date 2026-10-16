#!/usr/bin/env python3
"""Source-only unsigned iOS build, to run on a macOS runner with Xcode."""
from pathlib import Path
import concurrent.futures, hashlib, json, os, plistlib, subprocess, sys, tarfile, urllib.request, zipfile
root=Path(__file__).resolve().parents[2]
os.chdir(root)
build=root/'build';build.mkdir(exist_ok=True)
vendor=root/'vendor';vendor.mkdir(exist_ok=True)
archive=vendor/'MoltenVK-ios-v1.4.2.tar'
expected='b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1'
if not archive.exists():
    urllib.request.urlretrieve('https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.2/MoltenVK-ios.tar',archive)
assert hashlib.sha256(archive.read_bytes()).hexdigest()==expected,'MoltenVK archive checksum mismatch'
with tarfile.open(archive) as tar:tar.extractall(vendor,filter='data')
sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip()
clang=subprocess.check_output(['xcrun','--find','clang'],text=True).strip()
header=next(vendor.rglob('vulkan.h')).parent.parent
libs=[p for p in vendor.rglob('libMoltenVK.a') if 'ios-arm64' in str(p) and 'simulator' not in str(p)]
if not libs:libs=[p for p in vendor.rglob('libMoltenVK.a') if '/iOS/' in str(p)]
assert len(libs)==1,libs
common=[clang,'-target','arm64-apple-ios26.0','-isysroot',sdk,'-O2','-gline-tables-only','-ffixed-x18','-fno-stack-check',
        '-DDM_GAME_PORT=1','-DDM_SHELL=1','-include','TargetConditionals.h','-include','dm/ipad/compat.h',
        '-Idm/host','-Idm/android','-Idm/gfx/nvn','-Idm/gfx/nvn/include','-I'+str(header),
        '-Wno-format','-Wno-unused-function','-Wno-deprecated-declarations']
sources=json.loads((root/'sources.json').read_text())
def compile_one(source):
    obj=build/(source.replace('/','_')+'.o')
    args=common+(['-fobjc-arc'] if source.endswith('.m') else [])+['-c',source,'-o',str(obj)]
    proc=subprocess.run(args,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (build/(obj.name+'.log')).write_text(proc.stdout)
    return source,obj,proc.returncode,proc.stdout
results=list(concurrent.futures.ThreadPoolExecutor(max_workers=4).map(compile_one,sources))
errors=[r for r in results if r[2]]
for source,obj,code,log in errors:print(f'COMPILE_FAILED {source}\n{log}',flush=True)
if errors:raise SystemExit(f'{len(errors)}/{len(results)} compilation units failed')
app=build/'Payload/RhythmHost.app';app.mkdir(parents=True,exist_ok=True)
link=common+[str(r[1]) for r in results]+[str(libs[0]),'-o',str(app/'RhythmHost'),'-lc++','-lz','-lresolv']
for framework in ['UIKit','Foundation','QuartzCore','Metal','AudioToolbox','AVFoundation','CoreGraphics','IOSurface','IOKit','GameController']:
    link+=['-framework',framework]
link+=['-Wl,-rpath,@executable_path/Frameworks']
proc=subprocess.run(link,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(build/'link.log').write_text(proc.stdout);print(proc.stdout)
if proc.returncode:raise SystemExit(proc.returncode)
info=dict(CFBundleExecutable='RhythmHost',CFBundleIdentifier='org.rhythmhost.ipad',CFBundleName='RhythmHost',
          CFBundleDisplayName='節奏遊戲',CFBundlePackageType='APPL',CFBundleShortVersionString='0.1',CFBundleVersion=os.environ.get('GITHUB_RUN_NUMBER','1'),
          MinimumOSVersion='26.0',UIDeviceFamily=[2],LSRequiresIPhoneOS=True,
          UIRequiresFullScreen=True,UIFileSharingEnabled=True,LSSupportsOpeningDocumentsInPlace=True,
          UISupportedInterfaceOrientations=['UIInterfaceOrientationLandscapeLeft','UIInterfaceOrientationLandscapeRight'],
          UILaunchScreen={},UIViewControllerBasedStatusBarAppearance=True,ITSAppUsesNonExemptEncryption=False)
(app/'Info.plist').write_bytes(plistlib.dumps(info))
shutil=__import__('shutil')
shutil.copyfile(root/'LICENSE',app/'Host-LICENSE.txt')
for p in vendor.rglob('*'):
    if p.is_file() and p.name in ('LICENSE','LICENSE.txt'):shutil.copyfile(p,app/('MoltenVK-'+p.name));break
notices=app/'Licenses';notices.mkdir(exist_ok=True)
for p in (root/'dm/third_party/ryujinx-audio').rglob('*'):
    if p.is_file():shutil.copyfile(p,notices/p.name)
with zipfile.ZipFile(build/'RhythmHost-shell.zip','w',zipfile.ZIP_DEFLATED) as z:
    for p in app.rglob('*'):
        if p.is_file():z.write(p,p.relative_to(build))
report=dict(game_content_included=False,sdk=sdk,moltenvk_sha256=expected,source_units=len(results),
            executable_sha256=hashlib.sha256((app/'RhythmHost').read_bytes()).hexdigest(),
            source_commit=os.environ.get('GITHUB_SHA'),signed=False,device_tested=False)
(build/'build-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report),flush=True)
