#!/usr/bin/env python3
"""Produce exact original-vs-current bot source patches and content inventories.

The original directory must be extracted from the recorded user build archive.
This records code and asset bytes; narrative and runtime results are supplied by
the per-iteration handoff report. No proprietary asset bytes are emitted.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def functions(text):
    result = {}
    definition = re.compile(r'^[A-Za-z_][\w *]*\b([A-Za-z_]\w*)\([^;]*?\)\s*\{', re.M)
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    literals = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
    # Keep byte offsets/newlines while preventing prose such as s(eek) in a
    # preceding comment from being mistaken for a function declaration.
    clean = literals.sub(lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), text)
    for match in definition.finditer(clean):
        depth = 0
        for token in tokens.finditer(text, match.end() - 1):
            if token.group() == '{':
                depth += 1
            elif token.group() == '}':
                depth -= 1
                if depth == 0:
                    result[match[1]] = text[match.start():token.end()]
                    break
    return result


def generate(original, current, output, archive, botfiles, variant):
    output.mkdir(parents=True, exist_ok=True)
    names = subprocess.check_output(['git', 'ls-files', '-z'], cwd=current).decode().split('\0')
    files, patch = [], []
    for name in names:
        if not name.startswith(('code/game/', 'code/botlib/', 'code/server/', 'code/qcommon/')):
            continue
        newpath, oldpath = current / name, original / name
        if not newpath.is_file():
            raise ValueError(f'tracked source missing: {name}')
        old = oldpath.read_bytes() if oldpath.is_file() else b''
        new = newpath.read_bytes()
        if oldpath.is_file() and old == new:
            continue
        before, after = old.decode('utf-8'), new.decode('utf-8')
        oldfunc, newfunc = functions(before), functions(after)
        diff = list(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
            fromfile='a/' + name if oldpath.is_file() else '/dev/null', tofile='b/' + name))
        patch.extend(diff)
        files.append({'file': name, 'original_sha256': sha(old) if oldpath.is_file() else None,
            'current_sha256': sha(new), 'original_bytes': len(old), 'current_bytes': len(new),
            'added_lines': sum(line.startswith('+') and not line.startswith('+++') for line in diff),
            'removed_lines': sum(line.startswith('-') and not line.startswith('---') for line in diff),
            'functions_added': sorted(newfunc.keys() - oldfunc.keys()),
            'functions_removed': sorted(oldfunc.keys() - newfunc.keys()),
            'functions_changed': sorted(name for name in oldfunc.keys() & newfunc.keys()
                                        if oldfunc[name] != newfunc[name])})
    manifest = {'variant': variant, 'original_archive': archive.name,
        'original_archive_sha256': sha(archive.read_bytes()), 'files': files,
        'scope': 'All changed tracked files under code/game, code/botlib, code/server and code/qcommon; includes bot diagnostics/timing as well as AI. Function lists use source text, so comment-only changes also count. Global structs/macros are covered by the full patch and file hashes.'}
    if botfiles:
        with zipfile.ZipFile(botfiles) as z:
            entries = [{'path': i.filename, 'bytes': i.file_size, 'sha256': sha(z.read(i))}
                       for i in z.infolist() if not i.is_dir()]
        manifest['original_botfiles'] = {'archive': botfiles.name,
            'archive_sha256': sha(botfiles.read_bytes()), 'files': entries,
            'modified_asset_files': [], 'status': 'Used as original test input; source iteration does not rewrite character, goal/item/weapon weights or AAS files.'}
    (output / 'bot-source-file-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'bot-ai-cumulative-from-original.patch').write_text(''.join(patch))
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--current', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--original-archive', type=Path, required=True)
    parser.add_argument('--botfiles', type=Path)
    parser.add_argument('--variant', required=True)
    args = parser.parse_args()
    result = generate(args.original, args.current, args.output, args.original_archive, args.botfiles, args.variant)
    print(f"{len(result['files'])} changed bot/runtime source files; original archive and every file hashed")


if __name__ == '__main__':
    main()
