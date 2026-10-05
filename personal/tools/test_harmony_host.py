"""Run the actual Evolution plugin host with isolated settings and a bounded timeout."""
import json, os, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[2]
PLUGIN = HERE / 'share/plugins/HarmonyAssistant'
FIXTURE = HERE / 'mtest/mscore/scoreobserver'
APP = Path(sys.argv[1]).resolve()
WORK = Path(sys.argv[2]).resolve()
WORK.mkdir(parents=True, exist_ok=True)
source = (FIXTURE / 'smoke.qml').read_text(encoding='utf-8')
source = source.replace('../../../share/plugins/HarmonyAssistant/HarmonyAssistant_MS3.qml', (PLUGIN / 'HarmonyAssistant_MS3.qml').as_uri())
(WORK / 'smoke.qml').write_text(source, encoding='utf-8')
environment = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software', QML_DISABLE_DISK_CACHE='1')
command = [str(APP), '-s', '-m', '-c', str(WORK / 'settings'), '-p', str(WORK / 'smoke.qml'), str(FIXTURE / 'piano.mscx')]
try:
    result = subprocess.run(command, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=35)
    output = result.stdout
    status = result.returncode
except subprocess.TimeoutExpired as error:
    output, status = error.stdout or b'', 'timeout'
(WORK / 'smoke.log').write_bytes(output)
print(json.dumps({'exit':status,'report':str(WORK / 'native-smoke.json')}, ensure_ascii=True))
if (WORK / 'native-smoke.json').exists():
    report = json.loads((WORK / 'native-smoke.json').read_text(encoding='utf-8-sig'))
    print(json.dumps(report, ensure_ascii=True))
    sys.exit(bool(report['failures']))
print(output.decode('utf-8', errors='replace').encode('ascii',errors='backslashreplace').decode('ascii')[-7000:])
sys.exit(1)
