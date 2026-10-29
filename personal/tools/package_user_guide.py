"""Export the installed guide without links back into a developer checkout."""
from __future__ import annotations

import argparse
from collections import deque
from dataclasses import dataclass
import json
from pathlib import Path
import re
import subprocess
from urllib.parse import quote, unquote, urlsplit

LINK = re.compile(r'(!?\[[^\]\n]*\]\()(<[^>]*>|[^)\n]+)(\))')
FENCE = re.compile(r'(^\s*```[^\n]*\n.*?^\s*```[^\n]*(?:\n|$))', re.S | re.M)


@dataclass
class Repository:
    root: Path
    url: str
    revision: str
    files: set[str]

    @classmethod
    def read(cls, root, url):
        def git(*args):
            return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.PIPE).decode('utf8')
        return cls(root.resolve(), url, git('rev-parse', 'HEAD').strip(),
                   set(git('ls-tree', '-r', '--name-only', 'HEAD').splitlines()))

    def link(self, path):
        if not path.is_relative_to(self.root):
            return None
        relative = path.relative_to(self.root).as_posix()
        exact = next((name for name in self.files if name.casefold() == relative.casefold()), None)
        if exact:
            return f'{self.url}/blob/{self.revision}/{quote(exact)}'
        if path.is_dir() and any(name.startswith(relative.rstrip('/') + '/') for name in self.files):
            return f'{self.url}/tree/{self.revision}/{quote(relative)}'
        return None


def export_guide(source, destination, repositories, aliases=None):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if source == destination or destination.is_relative_to(source):
        raise ValueError('说明导出位置须与源码目录分开。')
    aliases = aliases or {}
    pending = deque([Path('personal/USER_GUIDE.md')])
    seen, updates, copied, unresolved = set(), [], [], []
    while pending:
        relative = pending.popleft()
        if relative in seen:
            continue
        seen.add(relative)
        original = source / relative
        target = destination / relative
        if not original.is_file():
            unresolved.append({'document': str(relative), 'target': 'source document missing'})
            continue
        text = original.read_text('utf8')

        def replace(match):
            raw = match.group(2).strip().strip('<>')
            url = urlsplit(raw)
            if not raw or raw.startswith('#') or (url.scheme and len(url.scheme) > 1):
                return match.group()
            name = unquote(raw.split('#', 1)[0])
            path = (original.parent / name).resolve()
            fragment = ('#' + url.fragment) if url.fragment else ''
            local = (target.parent / name).resolve()
            link = None
            if path.is_relative_to(source / 'personal') and path.is_file():
                output_relative = path.relative_to(source)
                if path.suffix == '.md':
                    pending.append(output_relative)
                elif not local.is_file() or local.read_bytes() != path.read_bytes():
                    local.parent.mkdir(parents=True, exist_ok=True)
                    local.write_bytes(path.read_bytes())
                    copied.append(str(output_relative))
                return match.group()
            if local.is_file() and local.is_relative_to(destination):
                return match.group()
            alias = aliases.get((relative.as_posix(), name))
            if alias:
                path = Path(alias).resolve()
            for repository in repositories:
                link = repository.link(path)
                if link:
                    break
            if link:
                updates.append({'document': relative.as_posix(), 'target': raw, 'url': link + fragment})
                return match.group(1) + link + fragment + match.group(3)
            unresolved.append({'document': relative.as_posix(), 'target': raw})
            return match.group()

        pieces = FENCE.split(text)
        exported = ''.join(piece if index % 2 else LINK.sub(replace, piece)
                           for index, piece in enumerate(pieces))
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.is_file() or target.read_bytes() != exported.encode('utf8'):
            target.write_bytes(exported.encode('utf8'))
    return {'documents': len(seen), 'updated_links': updates, 'copied_resources': copied,
            'unresolved': unresolved}


def check_export(destination):
    destination = Path(destination).resolve()
    pending = deque([destination / 'personal/USER_GUIDE.md'])
    seen, broken, links = set(), [], 0
    while pending:
        document = pending.popleft()
        if document in seen:
            continue
        seen.add(document)
        if not document.is_file():
            broken.append({'document': str(document), 'target': 'missing document'})
            continue
        text = ''.join(piece for index, piece in enumerate(FENCE.split(document.read_text('utf8')))
                       if index % 2 == 0)
        for match in LINK.finditer(text):
            raw = match.group(2).strip().strip('<>')
            url = urlsplit(raw)
            if not raw or raw.startswith('#') or (url.scheme and len(url.scheme) > 1):
                continue
            links += 1
            path = (document.parent / unquote(raw.split('#', 1)[0])).resolve()
            if not path.is_relative_to(destination) or not path.is_file():
                broken.append({'document': str(document.relative_to(destination)), 'target': raw})
            elif path.suffix == '.md':
                pending.append(path)
    return {'documents': len(seen), 'local_links': links, 'broken': broken}


def main():
    parser = argparse.ArgumentParser(description='整理安装包中的功能说明和资料链接')
    parser.add_argument('destination', type=Path, help='程序安装目录')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[2]
    projects = source.parent.parent
    plugins, mcp = projects / 'muse3_plugins', projects / 'mcp-musescore'
    repositories = [Repository.read(source, 'https://github.com/Freddd13/MuseScore')]
    for root, name in [(plugins, 'Muse3Plugins'), (mcp, 'mcp-musescore')]:
        if (root / '.git').exists():
            repositories.append(Repository.read(root, 'https://github.com/Freddd13/' + name))
    aliases = {('personal/docs/USABILITY_REWORK_20261019.md', 'review-images/20261019/'):
               plugins / 'Planning/review-images/20261019'}
    report = export_guide(source, args.destination, repositories, aliases)
    report['navigation'] = check_export(args.destination)
    if args.report:
        args.report.write_bytes(json.dumps(report, ensure_ascii=False, indent=2).encode('utf8'))
    print(f"已整理{report['documents']}份说明，更新{len(report['updated_links'])}处资料链接。")
    if report['unresolved'] or report['navigation']['broken']:
        for value in report['unresolved'] + report['navigation']['broken']:
            print(value['document'] + ' → ' + value['target'])
        raise SystemExit(1)


if __name__ == '__main__':
    main()
