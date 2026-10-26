"""Build Qt 5.15.2 Quick with the upstream QTBUG-80415 fix (Windows x64)."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tarfile
import urllib.request

QT_ARCHIVE = 'https://download.qt.io/archive/qt/5.15/5.15.2/submodules/qtdeclarative-everywhere-src-5.15.2.tar.xz'
QT_SHA256 = 'c600d09716940f75d684f61c5bdaced797f623a86db1627da599027f6c635651'
VK_ARCHIVE = 'https://api.github.com/repos/KhronosGroup/Vulkan-Headers/tarball/v1.2.154'
VK_SHA256 = '9e20540c2633b6cf4223045b8b68d260cf1eb618ed4cd8bf05530f61f418d7f1'
FIX_COMMIT = '1c010b202506bb7eb0f0e24ab37ad50e319abada'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def download(url, path, expected):
    if not path.exists():
        request = urllib.request.Request(url, headers={'User-Agent': 'MuseScore-QtQuick-fix'})
        with urllib.request.urlopen(request, timeout=60) as response, path.open('xb') as output:
            shutil.copyfileobj(response, output)
    if digest(path) != expected:
        raise RuntimeError('Archive checksum differs: ' + str(path))


def exports(path):
    data = path.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    machine, sections = struct.unpack_from('<HH', data, pe + 4)
    size = struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    if machine != 0x8664 or struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise RuntimeError('Expected a Windows x64 DLL: ' + str(path))

    def offset(rva):
        for index in range(sections):
            vsize, start, rawsize, raw = struct.unpack_from('<IIII', data, optional + size + index * 40 + 8)
            if start <= rva < start + max(vsize, rawsize):
                return raw + rva - start
        raise RuntimeError('Invalid PE address')

    directory = offset(struct.unpack_from('<I', data, optional + 112)[0])
    count = struct.unpack_from('<I', data, directory + 24)[0]
    names = offset(struct.unpack_from('<I', data, directory + 32)[0])
    result = set()
    for index in range(count):
        start = offset(struct.unpack_from('<I', data, names + index * 4)[0])
        result.add(data[start:data.index(0, start)].decode('ascii'))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qt', type=Path, required=True, help='Qt 5.15.2 MSVC x64 SDK')
    parser.add_argument('--work', type=Path, required=True, help='Dedicated build folder')
    parser.add_argument('--source', type=Path, help='Reuse extracted Qt 5.15.2 sources')
    parser.add_argument('--vulkan-headers', type=Path, help='Reuse Vulkan 1.2.154 include folder')
    parser.add_argument('--vcvars', type=Path, default=Path('C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/Common7/Tools/VsDevCmd.bat'))
    args = parser.parse_args()
    qt, work = args.qt.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    qmake = qt / 'bin/qmake.exe'
    version = subprocess.check_output([str(qmake), '-query', 'QT_VERSION'], text=True).strip()
    if version != '5.15.2':
        raise RuntimeError('This backport uses Qt 5.15.2; found ' + version)
    source = args.source.resolve() if args.source else work / 'sources/qtdeclarative-everywhere-src-5.15.2'
    if not args.source:
        archive = work / 'qtdeclarative-5.15.2.tar.xz'
        download(QT_ARCHIVE, archive, QT_SHA256)
        if not source.exists():
            with tarfile.open(archive) as tar:
                prefix = 'qtdeclarative-everywhere-src-5.15.2/'
                members = [m for m in tar if m.name.startswith(prefix) and not m.name[len(prefix):].startswith(('tests/', 'examples/', 'doc/'))]
                tar.extractall(source.parent, members=members, filter='data')
    header = source / 'src/quick/scenegraph/adaptations/software/qsgsoftwareinternalimagenode_p.h'
    text = header.read_text('utf8')
    if 'QSGTexture *m_texture;' in text:
        text = text.replace('#include <private/qsgtexturematerial_p.h>', '#include <private/qsgtexturematerial_p.h>\n\n#include <QtCore/QPointer>')
        header.write_bytes(text.replace('QSGTexture *m_texture;', 'QPointer<QSGTexture> m_texture;').encode())
    elif 'QPointer<QSGTexture> m_texture;' not in text:
        raise RuntimeError('The source differs from the upstream patch')
    headers = args.vulkan_headers.resolve() if args.vulkan_headers else None
    if headers is None:
        archive = work / 'vulkan-headers-1.2.154.tar.gz'
        download(VK_ARCHIVE, archive, VK_SHA256)
        with tarfile.open(archive) as tar:
            prefix = tar.getnames()[0].split('/')[0] + '/'
            headers = work / 'sources' / prefix / 'include'
            if not headers.exists():
                tar.extractall(work / 'sources', members=[m for m in tar if m.name.startswith(prefix + 'include/') or m.name == prefix + 'LICENSE.txt'], filter='data')
    if not (headers / 'vulkan/vulkan.h').exists():
        raise RuntimeError('Vulkan build headers are missing')
    temporary = work / 'tmp'
    temporary.mkdir(exist_ok=True)
    environment = dict(os.environ, TEMP=str(temporary), TMP=str(temporary))
    script = work / 'compiler-environment.cmd'
    script.write_bytes(('@echo off\r\ncall "' + str(args.vcvars) + '" -arch=x64 -host_arch=x64 >nul\r\nif errorlevel 1 exit /b 1\r\nset\r\n').encode('utf8'))
    result = subprocess.run(['cmd.exe', '/d', '/c', str(script)], env=environment, capture_output=True, check=True)
    for line in result.stdout.decode('mbcs', errors='replace').splitlines():
        if '=' in line and not line.startswith('='):
            name, value = line.split('=', 1)
            environment[name.upper()] = value
    environment['PATH'] = str(qt / 'bin') + ';' + environment['PATH'] + ';E:/Git/usr/bin'
    environment['_CL_'] = '/MP2'
    build = work / 'src/quick'
    build.mkdir(parents=True, exist_ok=True)
    common = ['CONFIG-=debug_and_release', 'CONFIG-=debug', 'CONFIG+=release']
    for name, command, cwd in [
        ('configure', [str(qmake), str(source / 'qtdeclarative.pro'), *common, 'QT_BUILD_PARTS=libs'], work),
        ('qmake', [str(qmake), str(source / 'src/quick/quick.pro'), *common, 'INCLUDEPATH+=' + str(headers)], build),
        ('build', ['nmake.exe', '/NOLOGO', '/f', 'Makefile.Release'], build),
    ]:
        print(name, flush=True)
        with (work / (name + '.log')).open('wb') as log:
            subprocess.run(command, cwd=cwd, env=environment, stdout=log, stderr=subprocess.STDOUT, check=True)
    original, binary = qt / 'bin/Qt5Quick.dll', work / 'bin/Qt5Quick.dll'
    before, after = exports(original), exports(binary)
    if before != after:
        raise RuntimeError('Exported symbols differ; inspect the component before packaging')
    manifest = {'qtVersion': version, 'fixCommit': FIX_COMMIT, 'sourceUrl': QT_ARCHIVE,
                'sourceSha256': QT_SHA256, 'vulkanHeadersUrl': VK_ARCHIVE,
                'vulkanHeadersSha256': VK_SHA256, 'originalSha256': digest(original),
                'binarySha256': digest(binary), 'exports': len(after), 'source': str(source)}
    (binary.parent / 'Qt5Quick.fix.json').write_bytes(json.dumps(manifest, indent=2).encode())
    for name, value in [('Qt5Quick.sha256', manifest['binarySha256']), ('Qt5Quick.original.sha256', manifest['originalSha256'])]:
        (binary.parent / name).write_text(value + '\n', encoding='ascii')
    print('Built', binary, manifest['binarySha256'])


if __name__ == '__main__':
    main()
