#!/usr/bin/env python3
"""Reject empty native objects and modules whose imports cannot resolve."""
import argparse,ctypes
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path);args=p.parse_args()
empty=[str(x) for x in args.build.rglob('*.o') if x.stat().st_size==0]
if empty:raise SystemExit('Empty object files: '+', '.join(empty))
for name in ('qagame','cgame','ui'):
 module=args.build/'baseq3'/f'{name}x86_64.so'
 lib=ctypes.CDLL(str(module.resolve()))
 getattr(lib,'dllEntry')
 if name!='qagame':getattr(lib,'vmMain')
 print(module.name+': imports and entry points resolved')
print('native module checks: PASS')
