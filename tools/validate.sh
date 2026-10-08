#!/bin/sh
# validate.sh - every source-level feature check, in one place.
#
# package-release.sh runs this before it builds, and so does CI, so a check
# added here runs in both. package-release.sh explains what each one is for.
# None of them needs a build or Quake Live's paks: the name lists they check
# against (docs/pak-manifest.txt, docs/ql-menu-names.txt) are checked in.
set -e
cd "$(dirname "$0")/.."
python3 content/serverconfigs/check-configs.py
python3 tools/stub-report.py
python3 tools/dead-cvars.py
python3 tools/check-assets.py
python3 tools/check-menu-defaults.py
python3 tools/check-menu-cvars.py
python3 tools/check-menus.py
python3 tools/check-score-fields.py
python3 tools/test-bot-performance.py
python3 tools/test-ctf-recovery.py
python3 tools/test-ctf-analysis.py
python3 tools/test-ctf-escort-items.py
python3 tools/test-ctf-carrier-selection.py
python3 tools/test-ctf-route-threat.py
python3 tools/test-ctf-relay.py
python3 tools/test-ctf-escort-intercept.py
python3 tools/test-aim-sweep.py
python3 tools/test-route-intel.py
echo "validate: all checks passed"
