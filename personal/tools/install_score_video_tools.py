"""Copy the checked standalone FFmpeg distribution beside a Kumo installation."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess

EXPECTED='8c40af3558a4f452b4eafd6b870d8a75dd0f2c91577103a5f7550f9d2a1de549'
def install(source:Path,program_directory:Path)->dict:
    binary=source/'bin/ffmpeg.exe'
    if hashlib.sha256(binary.read_bytes()).hexdigest()!=EXPECTED:raise ValueError('FFmpeg differs from the checked build')
    version=subprocess.check_output([str(binary),'-version'],text=True,encoding='utf8')
    encoders=subprocess.check_output([str(binary),'-hide_banner','-encoders'],text=True,encoding='utf8')
    if not all(name in encoders for name in ('libx264','prores_ks','aac','pcm_s16le')):raise ValueError('Required encoders are missing')
    target=program_directory/'tools/video';target.mkdir(parents=True,exist_ok=True)
    for relative,name in [('bin/ffmpeg.exe','ffmpeg.exe'),('LICENSE','LICENSE'),('README.txt','README.txt')]:shutil.copy2(source/relative,target/name)
    report={'version':version.splitlines()[0],'sha256':EXPECTED,'source':'https://github.com/FFmpeg/FFmpeg/commit/631e31773b','license':'GPL-3.0-or-later','configuration':version,'encoders':['libx264','prores_ks','aac','pcm_s16le']}
    (target/'provenance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    return {'directory':str(target),'sha256':EXPECTED,'bytes':binary.stat().st_size,'version':report['version']}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--bin',type=Path,required=True);args=p.parse_args();print(json.dumps(install(args.source,args.bin),ensure_ascii=False,indent=2))
