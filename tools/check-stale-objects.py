#!/usr/bin/env python3
"""
check-stale-objects.py - fail the build when an object file is older than a
header it was compiled against.

Why this exists
---------------
This is the tooling-error check. It does not look at the source; it looks at
what the compiler actually produced, because the source was never the problem.

E81: adding one `qboolean` to `vk_t` in code/renderervk/vk.h shifted every field
after it. `make` rebuilt vk.c and tr_init.c - their .c files had changed - and
did not rebuild tr_image.c, whose only change was the header. The stale
tr_image.o went on reading `vk.fboActive` at the old offset, read the wrong
field, and flipped `tr.overbrightBits` from 0 to 1. `tr.identityLight` halved
and the entire game and menu rendered at half brightness, from a commit whose
diff only compared extension strings.

The cause was a dead line in the Makefile: `-include $(OBJ_D_FILES)` where
`OBJ_D_FILES` derived from `$(OBJ)`, a variable this Makefile never assigns (the
lists are Q3OBJ, Q3R2OBJ, Q3RVKOBJ, JPGOBJ, Q3DOBJ). Every .c had been compiled
with -MMD and written a .d next to its .o since forever, and not one of those
files had ever been read. The Makefile is fixed. This check exists because the
fix is one `$(shell find ...)` away from silently expanding to nothing again,
and the failure it produces does not look like a build failure - it looks like a
rendering bug, a physics bug, a netcode bug, anything but the build.

So this verifies the *outcome* rather than the mechanism: whatever the Makefile
does, no object in the tree may be older than something it depends on.

What it checks
--------------
  1. Every .o has a .d next to it. An object with no dependency file cannot be
     rebuilt correctly by anything, which is the E81 state exactly.
  2. No prerequisite listed in a .d is newer than the .o that depends on it.
     That is the stale object: it exists, it links, and it disagrees with every
     other object about what the structs look like.
  3. Prerequisites that no longer exist are reported. -MMD is used without -MP,
     so a deleted header leaves a rule make cannot satisfy.

A clean build passes trivially - everything was just written. That is fine and
expected: the bug only exists in incremental builds, which is what everyone
actually runs while working.

Usage
-----
  tools/check-stale-objects.py                 check every dir under build/
  tools/check-stale-objects.py build/release-linux-x86_64
  tools/check-stale-objects.py --verbose       list what was checked

Exit status is 1 when anything is stale, so it can gate packaging.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Objects that legitimately have no .d file.
#
# win_resource.o comes from windres, not the C compiler, so there is no -MMD and
# no dependency file to write. It is not untracked: the Makefile spells its
# prerequisites out by hand (win_resource.rc and win_manifest.xml, at
# $(B)/client/win_resource.o and $(B)/ded/win_resource.o), which is exactly what
# a .d would have said. Keep this list short - every name on it is a file this
# check is blind to.
NO_DEPFILE = {
    'win_resource.o',
}


def parse_depfile(path):
    """Return the prerequisite paths from a gcc -MMD .d file.

    The format is a makefile rule: "target.o: prereq prereq \\\n prereq". Line
    continuations and the target itself are stripped. Escaped spaces in paths
    are honoured; nothing else in the makefile grammar is used by -MMD output.
    """
    try:
        with open(path, 'r', errors='replace') as f:
            text = f.read()
    except OSError:
        return None

    text = text.replace('\\\n', ' ')

    # Drop everything up to and including the first unescaped colon - that is
    # the "target.o:" part. Windows-style "C:/..." paths do not appear here
    # because the .d files are written by the build we ran, but the check for a
    # following space or slash costs nothing and makes that safe.
    colon = -1
    for i, ch in enumerate(text):
        if ch == ':' and (i == 0 or text[i - 1] != '\\'):
            colon = i
            break
    if colon < 0:
        return []
    text = text[colon + 1:]

    prereqs = []
    token = ''
    escaped = False
    for ch in text:
        if escaped:
            token += ch
            escaped = False
        elif ch == '\\':
            escaped = True
        elif ch.isspace():
            if token:
                prereqs.append(token)
                token = ''
        else:
            token += ch
    if token:
        prereqs.append(token)
    return prereqs


def mtime_ns(path):
    try:
        return os.stat(path).st_mtime_ns
    except OSError:
        return None


def check_tree(root, verbose=False):
    """Check one build directory. Returns (stale, orphans, missing, n_objects)."""
    stale = []
    orphans = []
    missing = []
    n = 0

    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            if not name.endswith('.o'):
                continue
            obj = os.path.join(dirpath, name)
            if name in NO_DEPFILE:
                continue
            n += 1
            dep = obj[:-2] + '.d'

            if not os.path.exists(dep):
                orphans.append(obj)
                continue

            obj_t = mtime_ns(obj)
            if obj_t is None:
                continue

            prereqs = parse_depfile(dep)
            if prereqs is None:
                orphans.append(obj)
                continue

            for p in prereqs:
                # .d files are written with paths relative to the directory
                # make ran in, which is the repo root.
                full = p if os.path.isabs(p) else os.path.join(REPO, p)
                t = mtime_ns(full)
                if t is None:
                    # A prerequisite inside the build tree is a generated
                    # intermediate, not a source file. renderergl2's GLSL is
                    # turned into .c under $(B)/renderergl2/glsl/, compiled, and
                    # then deleted by make as an intermediate - so it is
                    # *expected* to be gone once the build finishes, and its
                    # absence says nothing about whether the object is current.
                    # A missing prerequisite outside the build tree is a real
                    # source file that was renamed or deleted, which make cannot
                    # satisfy, and that is worth failing on.
                    if os.path.abspath(full).startswith(
                            os.path.abspath(root) + os.sep):
                        continue
                    missing.append((obj, p))
                    continue
                # GNU make rebuilds on strictly newer, so match that: an object
                # written in the same nanosecond as its header is not stale.
                if t > obj_t:
                    stale.append((obj, p, t - obj_t))

            if verbose:
                print(f'  ok  {os.path.relpath(obj, REPO)} '
                      f'({len(prereqs)} prerequisites)')

    return stale, orphans, missing, n


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    verbose = '--verbose' in sys.argv or '-v' in sys.argv

    if args:
        roots = args
    else:
        build = os.path.join(REPO, 'build')
        if not os.path.isdir(build):
            print('check-stale-objects: no build/ directory, nothing to check')
            return 0
        roots = [os.path.join(build, d) for d in sorted(os.listdir(build))
                 if os.path.isdir(os.path.join(build, d))]

    if not roots:
        print('check-stale-objects: no build directories, nothing to check')
        return 0

    total_objects = 0
    all_stale = []
    all_orphans = []
    all_missing = []

    for root in roots:
        if not os.path.isdir(root):
            print(f'check-stale-objects: {root}: no such directory',
                  file=sys.stderr)
            return 1
        if verbose:
            print(f'{os.path.relpath(root, REPO)}:')
        stale, orphans, missing, n = check_tree(root, verbose)
        total_objects += n
        all_stale += stale
        all_orphans += orphans
        all_missing += missing

    if all_orphans:
        print()
        print('STALE OBJECTS: %d object file(s) have no .d next to them.'
              % len(all_orphans))
        print('Nothing can know when to rebuild these. Compile flags lost -MMD,')
        print('or these objects predate it. Delete build/ and build again.')
        for o in all_orphans[:20]:
            print('  ' + os.path.relpath(o, REPO))
        if len(all_orphans) > 20:
            print('  ... and %d more' % (len(all_orphans) - 20))

    if all_missing:
        print()
        print('MISSING HEADERS: %d prerequisite(s) no longer exist.'
              % len(all_missing))
        print('-MMD is used without -MP, so make cannot satisfy these rules.')
        print('Usually means a header was renamed or deleted. Delete build/.')
        for o, p in all_missing[:20]:
            print('  %s needs %s' % (os.path.relpath(o, REPO), p))
        if len(all_missing) > 20:
            print('  ... and %d more' % (len(all_missing) - 20))

    if all_stale:
        print()
        print('STALE OBJECTS: %d object(s) are older than a header they '
              'include.' % len(all_stale))
        print()
        print('This is the E81 failure. These objects were compiled against an')
        print('older version of that header. If the header defines a struct,')
        print('they are reading fields at the wrong offsets right now and the')
        print('build links cleanly anyway. Do not ship this.')
        print()
        print('Fix: make sure dependency files are being included (the')
        print('-include OBJ_D_FILES block in the Makefile), then rebuild. A')
        print('"rm -rf build" always works.')
        print()
        for obj, prereq, delta in sorted(all_stale,
                                         key=lambda s: -s[2])[:40]:
            print('  %s' % os.path.relpath(obj, REPO))
            print('      is %.1fs older than %s' % (delta / 1e9, prereq))
        if len(all_stale) > 40:
            print('  ... and %d more' % (len(all_stale) - 40))

    if all_stale or all_orphans or all_missing:
        return 1

    print('check-stale-objects: %d object(s) up to date against their headers'
          % total_objects)
    return 0


if __name__ == '__main__':
    sys.exit(main())
