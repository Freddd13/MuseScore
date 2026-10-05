"""Run Qt's real Widgets/QML host regression with an isolated runtime layout.

Arguments: test executable, installed program directory, output directory.
The test itself uses temporary settings and never attaches to another app.
"""
import os
import filecmp
import shutil
import stat
import subprocess
import sys
from pathlib import Path

test, installed, output = [Path(p).resolve() for p in sys.argv[1:4]]
runtime = output / 'runtime'
runtime.mkdir(parents=True, exist_ok=True)
def stage_file(source, destination):
    if destination.exists():
        if filecmp.cmp(str(source), str(destination), shallow=False):
            return
        destination.chmod(destination.stat().st_mode | stat.S_IWRITE)
    shutil.copy2(str(source), str(destination))

for source in installed.iterdir():
    if source.is_dir():
        for parent, folders, files in os.walk(str(source)):
            destination = runtime / Path(parent).relative_to(installed)
            destination.mkdir(parents=True, exist_ok=True)
            for name in files:
                stage_file(Path(parent) / name, destination / name)
target = runtime / 'bin' / test.name
stage_file(test, target)
# Qt's style loader also consults QLibraryInfo, independently of MsQmlEngine.
# Keep the test runtime's module/plugin paths consistent with its staged tree.
(runtime / 'bin/qt.conf').write_text(
    '[Paths]\nPrefix=../\nPlugins=bin\nQml2Imports=qml\n'
    '[Platforms]\nWindowsArguments=fontengine=freetype\n', encoding='ascii')
env = dict(os.environ, QT_QUICK_BACKEND='software', QML_DISABLE_DISK_CACHE='1',
           HARMONY_TEST_ARTIFACTS=str(output))
env.pop('QT_QPA_PLATFORM', None)
env.pop('QT_QUICK_CONTROLS_STYLE', None)
log = output / 'gui.txt'
try:
    result = subprocess.run([str(target), '-o', str(log) + ',txt'], env=env, timeout=45)
    status = result.returncode
except subprocess.TimeoutExpired:
    status = 124
print(log.read_text(encoding='utf-8', errors='replace').encode('ascii', errors='backslashreplace').decode('ascii'))
sys.exit(status)
