#!/usr/bin/env python3
"""Run actual headless bot matches on authored fixtures, retaining every log.

Examples:
  python3 tools/test-environment/run.py --seconds 120
  python3 tools/test-environment/run.py --warmup --seconds 30
  python3 tools/test-environment/run.py --mode tdm --bots 4 --seconds 120
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[2]
MODES = {"ffa": 0, "tdm": 3, "ca": 4, "ctf": 5, "ft": 9, "ad": 11, "rr": 12}


def clean(text):
    return re.sub(r"\^[0-9]", "", text)


def snapshots(text):
    text = clean(text)
    header = re.compile(r"Bot scores at (\d+) ms: gametype (\d+), warmup (-?\d+), round state (\d+), playing (\d+)")
    found = list(header.finditer(text))
    result = []
    for index, match in enumerate(found):
        block = text[match.end():found[index + 1].start() if index + 1 < len(found) else len(text)]
        end = re.search(r"(\d+) connected bots; team scores red (-?\d+), blue (-?\d+)", block)
        if not end:
            continue
        bots = [{"slot": int(slot), "team": int(team), "score": int(score), "name": name.strip()}
                for slot, team, score, name in re.findall(r"  slot (\d+): team (\d+), score (-?\d+), ([^\n]+)", block[:end.end()])]
        result.append(dict(zip(("time", "gametype", "warmup", "round_state", "playing"), map(int, match.groups())),
                           bots=bots, team_scores={"red": int(end.group(2)), "blue": int(end.group(3))}))
    return result


def setup(root, build, assets):
    server = root / "server"
    base = server / "baseq3"
    base.mkdir(parents=True, exist_ok=True)
    executable = build / "quakelive_dedicated.x86_64"
    module = build / "baseq3/qagamex86_64.so"
    if not executable.is_file() or not module.is_file():
        raise RuntimeError(f"Build the Linux dedicated server and qagame first: {build}")
    # A successful shared-library link can retain unresolved imports.
    import ctypes
    empty = [str(p) for p in build.rglob("*.o") if p.stat().st_size == 0]
    if empty:
        raise RuntimeError("Empty native objects: " + ", ".join(empty))
    getattr(ctypes.CDLL(str(module.resolve())), "dllEntry")
    shutil.copy2(executable, server / executable.name)
    # Refresh the exact selected game module; never trust a previous extracted DLL.
    import zipfile
    with zipfile.ZipFile(base / "iobin.pk3", "w", zipfile.ZIP_DEFLATED) as archive:
        archive.write(module, module.name)
    if assets:
        # Keep synthetic bot/weapon/navigation data out of real-map runs.
        with zipfile.ZipFile(base / "pak00.pk3", "w", zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("default.cfg", "// Authored dedicated startup stub.\n")
            archive.writestr("FIXTURE-NOTICE.txt", "Startup stub only; supplied map/bot assets are separate.\n")
    else:
        subprocess.run([sys.executable, str(Path(__file__).with_name("generate-fixtures.py")), str(base)], check=True)
    for index, asset in enumerate(assets):
        if not asset.is_file():
            raise RuntimeError(f"Missing asset archive: {asset}")
        with zipfile.ZipFile(asset) as archive:
            if archive.testzip() is not None:
                raise RuntimeError(f"Corrupt asset archive: {asset}")
        shutil.copy2(asset, base / f"zzzz_supplied_{index:02d}.pk3")
    (base / "server.cfg").write_text("// Per-run controls are supplied by the runner.\n")
    (base / "autoexec.cfg").write_text("// Private authored fixture environment.\n")
    return server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPO / "test-environment")
    parser.add_argument("--build", type=Path, default=REPO / "build/release-linux-x86_64")
    parser.add_argument("--mode", choices=MODES, default="ffa")
    parser.add_argument("--seconds", type=int, default=120, help="simulated live-match seconds")
    parser.add_argument("--timescale", type=float, default=2, help="use 1 for CPU comparisons")
    parser.add_argument("--bots", type=int, default=2)
    parser.add_argument("--map", default="ql_fixture")
    parser.add_argument("--asset-pk3", type=Path, action="append", default=[], help="supplied ZIP/PK3 assets; override authored fixture data")
    parser.add_argument("--characters", default="FixtureA,FixtureB,FixtureC,FixtureD", help="comma-separated bot definition names")
    parser.add_argument("--skill", type=float, default=10)
    parser.add_argument("--tactics", type=int, choices=(0, 1), help="override the tactical layer for a control run")
    parser.add_argument("--diagnostic-interval", type=int, default=0, help="simulated seconds between full AI reports; 0 disables")
    parser.add_argument("--report-interval", type=float, default=.4, help="wall seconds between score reports")
    parser.add_argument("--track-interval", type=int, default=250, help="game milliseconds between position tracks")
    parser.add_argument("--warmup", action="store_true", help="negative control: stay in warmup with zero individual scores; CTF practice team captures are recorded")
    parser.add_argument("--require-score", action="store_true", help="require team-score progress in objective/round modes")
    parser.add_argument("--round-delay", type=int, default=1000, help="round countdown milliseconds; 0 exercises the zero-delay path")
    parser.add_argument("--port", type=int, default=27970)
    parser.add_argument("--set", action="append", default=[], metavar="NAME=VALUE",
                        help="extra server cvar, repeatable - e.g. bot_tacticsTeams=1 for a head-to-head (E224)")
    args = parser.parse_args()
    if not 2 <= args.bots <= 60 or args.seconds < 1 or not 0 < args.timescale <= 8:
        parser.error("bots must be 2–60, seconds positive, timescale in (0,8]")
    characters = args.characters.split(",")
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.map) or not all(re.fullmatch(r"[A-Za-z0-9_-]+", name) for name in characters):
        parser.error("map and character names must contain only letters, digits, underscore or hyphen")
    if not 1 <= args.skill <= 10 or not .1 <= args.report_interval <= 30 or args.track_interval < 0 or args.diagnostic_interval < 0:
        parser.error("skill must be 1–10, report interval .1–30 seconds")
    assets = [p.resolve() for p in args.asset_pk3]
    root, build = args.root.resolve(), args.build.resolve()
    label = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ") + "-" + args.mode + ("-warmup" if args.warmup else "-live")
    run = root / "runs" / label
    home = run / "home"; home.mkdir(parents=True)
    # Each run owns its engine, paks and extracted modules. A second run cannot
    # overwrite assets or replace the game module under a running server.
    server = setup(run, build, assets)
    log_path = run / "server.log"
    command = [str(server / "quakelive_dedicated.x86_64")]
    settings = {
        "fs_basepath": str(server), "fs_homepath": str(home), "dedicated": "1", "net_ip": "127.0.0.1",
        "net_port": str(args.port), "sv_master": "", "sv_master1": "", "sv_pure": "0", "sv_fps": "40",
        "sv_maxclients": str(max(8, args.bots)), "bot_enable": "1", "g_gametype": str(MODES[args.mode]),
        # Admit the whole population before allowing a round to start.
        # The measured live test switches this to 0 below; no human is needed.
        "g_doWarmup": "1", "g_warmup": "0", "g_warmupDelay": "0",
        "g_debugWarmup": "1", "g_roundWarmupDelay": str(args.round_delay),
        "timelimit": "0", "fraglimit": "0", "capturelimit": "0", "roundlimit": "0",
        "g_log": "fixture-game.log", "g_logSync": "1", "logfile": "2", "com_cpuTimings": "1",
        "bot_nochat": "1", "bot_debugTrack": str(args.track_interval), "bot_debugMovement": "5",
        "g_isBotOnly": "0", "g_training": "0", "sv_warmupReadyPercentage": "0.5",
    }
    if args.tactics is not None: settings["bot_tactics"] = str(args.tactics)
    for item in args.set:
        name, sep, value = item.partition("=")
        if not sep or not re.fullmatch(r"[A-Za-z0-9_]+", name) or not re.fullmatch(r"[A-Za-z0-9_.-]*", value):
            parser.error(f"--set wants NAME=VALUE with a plain cvar name and value, not {item!r}")
        settings[name] = value
    for key, value in settings.items(): command.extend(("+set", key, value))
    command.extend(("+devmap", args.map))
    metadata = {"command": command, "settings": settings, "live_test_g_doWarmup": 0 if not args.warmup else 1,
                "map": args.map, "characters": characters, "skill": args.skill,
                "assets": [{"path": str(p), "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in assets],
                "binary_sha256": hashlib.sha256((server / "quakelive_dedicated.x86_64").read_bytes()).hexdigest(),
                "qagame_sha256": hashlib.sha256((build / "baseq3/qagamex86_64.so").read_bytes()).hexdigest()}
    (run / "launch.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"Running actual {args.mode} server: {run}", flush=True)
    transcript = []
    errors = []
    process = None
    initial = None
    try:
        with log_path.open("w") as log:
            process = subprocess.Popen(command, cwd=server, stdin=subprocess.PIPE,
                                       stdout=log, stderr=subprocess.STDOUT, text=True, start_new_session=True)
            (run / "server.pid").write_text(str(process.pid) + "\n")

            def read():
                if process.poll() is not None:
                    raise RuntimeError(f"server exited early: {process.returncode}; see {log_path}")
                return log_path.read_text(errors="replace")

            def send(line):
                transcript.append({"wall_time": time.monotonic(), "command": line})
                process.stdin.write(line + "\n"); process.stdin.flush()

            def wait_for(predicate, deadline=15):
                stop = time.monotonic() + deadline
                while time.monotonic() < stop:
                    body = read()
                    if predicate(body): return body
                    time.sleep(.1)
                raise RuntimeError("timeout waiting for runtime prerequisite; see server.log")

            wait_for(lambda body: "AAS initialised." in body, deadline=60)
            for i in range(args.bots):
                team = "free" if args.mode == "ffa" else ("red" if i % 2 == 0 else "blue")
                send(f"addbot {characters[i % len(characters)]} {args.skill} {team} 0 Bot{i:02d}")
                time.sleep(.2)
            stop = time.monotonic() + 20
            while time.monotonic() < stop:
                send("botscores"); time.sleep(.2)
                reports = snapshots(read())
                if reports and len(reports[-1]["bots"]) == args.bots and reports[-1]["playing"] == args.bots:
                    initial = reports[-1]; break
            if initial is None: raise RuntimeError("bots did not reach the required match state")
            if not args.warmup:
                send("set g_doWarmup 0")
                stop = time.monotonic() + 15
                while time.monotonic() < stop:
                    send("botscores"); time.sleep(.2)
                    reports = snapshots(read())
                    if reports and reports[-1]["warmup"] == 0 and reports[-1]["playing"] == args.bots:
                        initial = reports[-1]; break
                else: raise RuntimeError("g_doWarmup 0 did not start a live bot-only match")
            send("bots")
            send(f"timescale {args.timescale}")
            deadline = time.monotonic() + max(30, args.seconds / args.timescale * 3 + 15)
            last_progress = time.monotonic()
            last_diagnostic = initial["time"]
            while time.monotonic() < deadline:
                send("botscores"); time.sleep(args.report_interval)
                reports = snapshots(read())
                if args.diagnostic_interval and reports and reports[-1]["time"] - last_diagnostic >= args.diagnostic_interval * 1000:
                    send("bots")
                    last_diagnostic = reports[-1]["time"]
                if reports and reports[-1]["time"] - initial["time"] >= args.seconds * 1000: break
                if time.monotonic() - last_progress >= 10:
                    elapsed = (reports[-1]["time"] - initial["time"]) / 1000 if reports else 0
                    print(f"  simulated {elapsed:.1f}s; team scores {reports[-1]['team_scores'] if reports else {}}", flush=True)
                    last_progress = time.monotonic()
            else: raise RuntimeError("server did not simulate the requested duration")
            send("bots")
            send("quit")
            process.wait(timeout=5)
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        errors.append(str(error))
    finally:
        if process is not None and process.poll() is None:
            try:
                transcript.append({"wall_time": time.monotonic(), "command": "quit"})
                process.stdin.write("quit\n"); process.stdin.flush()
                process.wait(timeout=3)
            except (OSError, subprocess.TimeoutExpired):
                process.terminate()
                try: process.wait(timeout=3)
                except subprocess.TimeoutExpired: process.kill(); process.wait()
    body = log_path.read_text(errors="replace")
    reports = snapshots(body)
    final = reports[-1] if reports else None
    live_body = body
    if initial:
        header = rf"Bot scores at {initial['time']} ms: gametype {initial['gametype']}, warmup {initial['warmup']},"
        starts = list(re.finditer(header, body))
        if starts: live_body = body[starts[-1].start():]
    # Game time can reset on the warmup-to-live restart. Cut by report position,
    # not just milliseconds, so warmup movement/captures are not counted.
    if final:
        header = rf"Bot scores at {final['time']} ms: gametype {final['gametype']}, warmup {final['warmup']},"
        ends = list(re.finditer(header, live_body))
        if ends: live_body = live_body[:ends[-1].start()]
    captures = len(re.findall(r"captured the (?:RED|BLUE)", live_body))
    kills = len(re.findall(r"Kill: \d+ \d+", live_body))
    tracks = re.findall(r"bottrack (\d+) (\d+) (\d+) (-?\d+) (-?\d+) (-?\d+) (\d+)", live_body)
    active_tracks = [track for track in tracks if initial and int(track[0]) >= initial["time"]]
    moved = sorted({int(track[1]) for track in active_tracks if int(track[6]) > 10})
    if final and initial:
        if len(final["bots"]) != args.bots: errors.append("bot population changed")
        if not args.warmup and args.mode != "ffa":
            earlier_teams = {bot["slot"]: bot["team"] for bot in initial["bots"]}
            if any(earlier_teams.get(bot["slot"]) in (1, 2) and bot["team"] == 3 for bot in final["bots"]):
                errors.append("playing bot was permanently moved to the spectator team")
        scores = [bot["score"] for bot in final["bots"]]
        if args.warmup:
            if final["warmup"] == 0 or any(scores): errors.append("warmup negative control became live or awarded scores")
            # AddScore suppresses individual warmup points. AddTeamScore
            # permits CTF practice captures; retain the timeline rather than
            # mistake those practice points for a live match.
        elif args.mode in ("ffa", "tdm"):
            if final["warmup"] != 0: errors.append("match returned to warmup")
            earlier = {bot["slot"]: bot["score"] for bot in initial["bots"]}
            if kills < 1 or not any(bot["score"] > earlier.get(bot["slot"], 0) for bot in final["bots"]):
                errors.append("no actual kills with positive score progress")
            if not moved: errors.append("no bot movement observed")
        else:
            if final["warmup"] != 0: errors.append("match returned to warmup")
            # For round/objective modes, record outcomes without imposing frag-score rules.
            if args.require_score and not any(final["team_scores"][team] > initial["team_scores"][team] for team in ("red", "blue")):
                errors.append("no team-score progress in objective/round test")
    if "Game has been forfeited" in body: errors.append("match forfeited")
    if process is not None and process.returncode != 0: errors.append(f"server exit code {process.returncode}")
    if re.search(r"(?:Segmentation fault|BotAISetupClient failed|ERROR:|Fatal:)", body): errors.append("runtime error in server log")
    capture_timeline = []
    if initial:
        previous = initial["team_scores"]
        for report in snapshots(live_body):
            if report["time"] < initial["time"]: continue
            if report["team_scores"] != previous:
                capture_timeline.append({"elapsed_seconds": (report["time"] - initial["time"]) / 1000,
                                         "team_scores": report["team_scores"]})
                previous = report["team_scores"]
    result = {"status": "FAIL" if errors else "PASS", "mode": args.mode, "map": args.map, "warmup_control": args.warmup,
              "requested_seconds": args.seconds, "timescale": args.timescale, "initial": initial, "final": final,
              "kills_after_initial": kills, "bots_with_movement": moved, "errors": errors,
              "captures": captures, "team_score_timeline": capture_timeline,
              "observed_round_states": sorted({report["round_state"] for report in reports}),
              "scope": "actual server and native qagame/botlib on " + ("supplied map/bot assets" if assets else "authored collision/navigation fixtures"),
              "reports": reports, "server_exit": process.returncode if process else None}
    (run / "commands.json").write_text(json.dumps(transcript, indent=2) + "\n")
    (run / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: result[key] for key in ("status", "kills_after_initial", "bots_with_movement", "errors")}, indent=2), flush=True)
    print(f"Results and logs: {run}", flush=True)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
