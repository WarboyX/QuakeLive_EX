#!/usr/bin/env python3
"""Run the reusable runtime suite; a game regression keeps a nonzero exit code."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys

REPO = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPO / "test-environment")
    parser.add_argument("--build", type=Path, default=REPO / "build/release-linux-x86_64")
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--timescale", type=float, default=4, help="use 1 for CPU comparison runs")
    parser.add_argument("--graphics", action="store_true")
    parser.add_argument("--software", action="store_true")
    parser.add_argument("--rounds", action="store_true", help="include CA regressions; currently fails on the shipped game code")
    args = parser.parse_args()
    root = args.root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    here = Path(__file__).resolve().parent
    cases = [("ffa-live", ["--mode", "ffa", "--bots", "2"]),
             ("ffa-warmup", ["--warmup", "--mode", "ffa", "--bots", "2"]),
             ("tdm-live", ["--mode", "tdm", "--bots", "4"]),
             ("ctf-live", ["--mode", "ctf", "--bots", "4", "--require-score"])]
    if args.rounds:
        cases.extend((("ca-normal", ["--mode", "ca", "--bots", "4", "--require-score", "--round-delay", "1000"]),
                      ("ca-zero", ["--mode", "ca", "--bots", "4", "--require-score", "--round-delay", "0"])))
    results = []
    for index, (name, extra) in enumerate(cases):
        command = [sys.executable, str(here / "run.py"), "--root", str(root / name), "--build", str(args.build.resolve()),
                   "--seconds", str(min(45, args.seconds) if name.endswith("warmup") else args.seconds),
                   "--timescale", str(args.timescale), "--port", str(27970 + index), *extra]
        print(f"Testing {name}", flush=True)
        before = set((root / name).rglob("result.json"))
        process = subprocess.run(command)
        created = set((root / name).rglob("result.json")) - before
        report = json.loads(next(iter(created)).read_text()) if len(created) == 1 else {"status": "FAIL", "errors": ["missing result"]}
        results.append({"case": name, "exit_code": process.returncode,
                        "status": report["status"], "report": str(next(iter(created))) if len(created) == 1 else None,
                        "errors": report.get("errors", [])})
    if args.graphics:
        command = [sys.executable, str(here / "run-shadow.py"), "--root", str(root)]
        if args.software: command.append("--software")
        process = subprocess.run(command)
        results.append({"case": "shadow-vulkan", "exit_code": process.returncode,
                        "status": "PASS" if process.returncode == 0 else "FAIL"})
    failed = any(result["exit_code"] != 0 or result["status"] != "PASS" for result in results)
    summary = {"status": "FAIL" if failed else "PASS", "results": results}
    report_path = root / ("suite-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ") + ".json")
    report_path.write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2), flush=True)
    print(f"Suite report: {report_path}", flush=True)
    return bool(failed)


if __name__ == "__main__":
    raise SystemExit(main())
