#!/usr/bin/env python3
"""Build an unsigned Android shell; optional game modules are local-only.

No signing key is read, generated or embedded. Never installs to a device.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,zipfile
import xml.etree.ElementTree as ET

ANDROID='{http://schemas.android.com/apk/res/android}'

def prepare_manifest(source, destination, debug=False):
    ET.register_namespace('android',ANDROID[1:-1])
    tree=ET.parse(source);app=tree.getroot().find('application')
    if app is None:raise ValueError('Missing application')
    app.set(ANDROID+'debuggable','true' if debug else 'false')
    tree.write(destination,encoding='utf-8',xml_declaration=True)

def verify_packaged_manifest(bt, apk, debug, run):
    # Check the actual binary manifest after aapt2, not just its XML input.
    output=run([bt,'dump','xmltree',apk,'--file','AndroidManifest.xml'],'manifest-check').decode(errors='replace')
    import re
    m=re.search(r'android:debuggable[^\n]*?=\s*(?:\(type\s+0x12\))?\s*(0x[0-9a-fA-F]+|true|false)',output)
    if not m:raise ValueError('Packaged debuggable flag not found')
    actual=m.group(1).lower() not in ('0x0','0x00000000','false')
    if actual!=debug:raise ValueError('Packaged debuggable flag mismatch')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',type=Path,required=True)
    parser.add_argument('--jdk',type=Path,required=True)
    parser.add_argument('--native',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--modules',type=Path,help='Local-only Android rewritten main/sdk/rtld; never upload this directory or the resulting APK')
    parser.add_argument('--build-tools',default='35.0.0')
    parser.add_argument('--debug',action='store_true',help='Local development only: allow debugger and adb run-as; default is non-debuggable')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1];tree=root/'dm/android'
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    sdk=args.sdk.resolve();jdk=args.jdk.resolve();native=args.native.resolve()
    assert native.is_file(),'Native library missing'
    bt=sdk/'build-tools'/args.build_tools
    extension='.exe' if os.name=='nt' else ''
    env=dict(os.environ,JAVA_HOME=str(jdk));env['PATH']=str(jdk/'bin')+os.pathsep+env['PATH']
    temporary=out/'tmp';temporary.mkdir();env['TEMP']=env['TMP']=env['TMPDIR']=str(temporary)
    def run(cmd,name):
        p=subprocess.run([str(x) for x in cmd],env=env,capture_output=True)
        (out/(name+'.log')).write_bytes(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError(name+': '+(p.stdout+p.stderr).decode(errors='replace')[-2000:])
        return p.stdout
    android=sdk/'platforms/android-34/android.jar'
    manifest=out/'AndroidManifest.xml'
    prepare_manifest(tree/'AndroidManifest.xml',manifest,args.debug)
    run([bt/('aapt2'+extension),'link','-o',out/'base.apk','--manifest',manifest,'-I',android,'--min-sdk-version','29','--target-sdk-version','33'],'manifest')
    verify_packaged_manifest(bt/('aapt2'+extension),out/'base.apk',args.debug,run)
    classes=out/'classes';classes.mkdir();dex=out/'dex';dex.mkdir()
    run([jdk/'bin'/('javac'+extension),'--release','8','-encoding','UTF-8','-cp',android,'-d',classes,*sorted((tree/'src').rglob('*.java'))],'javac')
    run([bt/('d8.bat' if os.name=='nt' else 'd8'),'--min-api','29','--lib',android,'--output',dex,*sorted(classes.rglob('*.class'))],'d8')
    modules=[]
    if args.modules:
        directory=args.modules.resolve()
        for name in ['main','sdk','rtld']:
            layout=(directory/(name+'.fixups.tsv')).read_text()
            if '# ANDROID_APP_SLOT\t0x2' not in layout:raise ValueError('Need Android rewrite, not Apple modules: '+name)
            for suffix in ['patched.bin','fixups.tsv','gates.tsv']:
                p=directory/(name+'.'+suffix);data=p.read_bytes()
                modules.append((p.name,data,hashlib.sha256(data).hexdigest()))
    with zipfile.ZipFile(out/'base.apk') as base,zipfile.ZipFile(out/'raw-unsigned.apk','w') as package:
        for info in base.infolist():
            if not info.filename.startswith('META-INF/'):package.writestr(info,base.read(info))
        package.write(dex/'classes.dex','classes.dex')
        package.write(native,'lib/arm64-v8a/libd2r_mobile_android.so')
        for name,data,digest in modules:package.writestr('assets/modules/'+name,data,compress_type=zipfile.ZIP_DEFLATED)
        if modules:package.writestr('assets/module-manifest.json',json.dumps({'files':[{'name':n,'bytes':len(d),'sha256':h} for n,d,h in modules]}))
        for folder in ['ryujinx-audio','bcdec']:
            for p in (root/'dm/third_party'/folder).iterdir():
                if p.name in ('LICENSE','LICENSE.txt','SOURCE.md'):package.write(p,'assets/licenses/'+folder+'/'+p.name)
    apk=out/'rhythmhost-unsigned.apk'
    run([bt/('zipalign'+extension),'-f','-P','16','4',out/'raw-unsigned.apk',apk],'zipalign')
    run([bt/('zipalign'+extension),'-c','-P','16','4',apk],'zipalign-check')
    with zipfile.ZipFile(apk) as z:
        assert z.testzip() is None
        assert not any(n.startswith('META-INF/') for n in z.namelist())
    report={'signed':False,'debuggable':args.debug,'application_id':'org.d2r.binaryprobe','game_modules_included':bool(modules),'rom_included':False,'private_local_package':bool(modules),
            'apk':str(apk),'sha256':hashlib.sha256(apk.read_bytes()).hexdigest()}
    (out/'package-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
