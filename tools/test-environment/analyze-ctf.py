#!/usr/bin/env python3
"""Summarize actual CTF runs; sampled carrier time is not exact possession time."""
import argparse
from collections import Counter, defaultdict
from bisect import bisect_right
import json
from pathlib import Path
import re
from statistics import median


def analyze(run):
    result = json.loads((run / 'result.json').read_text())
    launch = json.loads((run / 'launch.json').read_text())
    text = re.sub(r'\^[0-9]', '', (run / 'server.log').read_text(errors='replace'))
    first = result['initial']['time']
    header = rf"Bot scores at {first} ms: gametype {result['initial']['gametype']}, warmup {result['initial']['warmup']},"
    starts = list(re.finditer(header, text))
    if not starts: raise ValueError(f'missing live-start report: {run}')
    text = text[starts[-1].start():] # clock may reset when warmup ends
    final = result['final']
    end_header = rf"Bot scores at {final['time']} ms: gametype {final['gametype']}, warmup {final['warmup']},"
    ends = list(re.finditer(end_header, text))
    if ends: text = text[:ends[-1].start()] # ignore play after the last measured score
    clock = first
    events = Counter()
    tracks = defaultdict(list)
    escorts = defaultdict(list)
    carrier_traces = defaultdict(list)
    metadata = {}
    for line in text.splitlines():
        m = re.search(r'Bot scores at (\d+) ms', line)
        if m: clock = int(m[1])
        m = re.match(r'bottrack (\d+) (\d+) (\d+) (-?\d+) (-?\d+) (-?\d+) (\d+) ([sfd]) (\d+)', line)
        if m:
            clock, slot, team, x, y, z, speed = map(int, m.groups()[:7])
            state, job = m.groups()[7:]
            if clock >= first:
                tracks[slot].append((clock, state, int(job), team, [x, y, z], speed))
        if clock < first: continue
        if 'broadcast: print' in line:
            for key, pattern in [('grabs', r'got the (RED|BLUE) flag'),
                                 ('returns', r'returned the (RED|BLUE) flag'),
                                 ('carrier_kills', r"fragged (RED|BLUE)'s flag carrier"),
                                 ('captures', r'captured the (RED|BLUE) flag')]:
                if re.search(pattern, line): events[key] += 1
        m = re.search(r'(red|blue) roles .*? escort (\d+)/', line)
        if m: escorts[m[1]].append(int(m[2]))
        m = re.match(r'ctftrack (\d+) (\d+) hp(-?\d+) flag(\d+) mate(-?\d+) via(\d+) home(\d+) node(\w+)', line)
        if m and int(m[1]) >= first:
            meta = {'time': int(m[1]), 'health': int(m[3]), 'flag': int(m[4]),
                    'mate': int(m[5]), 'waypoint': int(m[6]), 'home_time': int(m[7]), 'node': m[8]}
            metadata[(meta['time'], int(m[2]))] = meta
            if meta['health'] > 0 and meta['flag']:
                carrier_traces[int(m[2])].append(meta)
    intervals = []
    for slot, rows in tracks.items():
        current = []
        for row in rows:
            if row[1] != 'd' and row[2] == 5:
                current.append(row)
            elif current:
                intervals.append({'slot': slot, 'team': current[0][3],
                                  'first_ms': current[0][0], 'last_ms': current[-1][0],
                                  'observed_seconds': (current[-1][0] - current[0][0]) / 1000,
                                  'last_position': current[-1][4], 'next_state': row[1]})
                current = []
        # A possession still active at shutdown is censored and excluded.
    lengths = [row['observed_seconds'] for row in intervals]
    # One carrier per team in standard CTF. These bot-only runs have no player
    # follow orders. Accompany samples are an escort proximity proxy; a stale
    # follow of a former carrier can still contribute to this legacy measure.
    # Use only preceding teammate samples within 1.1s, never future positions.
    clocks = {slot: [row[0] for row in rows] for slot, rows in tracks.items()}
    coverage = Counter()
    for slot, rows in tracks.items():
        for row in rows:
            if row[1] == 'd' or row[2] != 5: continue
            nearby = 0
            for other, other_rows in tracks.items():
                if other == slot: continue
                index = bisect_right(clocks[other], row[0]) - 1
                if index < 0: continue
                mate = other_rows[index]
                if row[0] - mate[0] > 1100 or mate[1] == 'd' or mate[2] != 2 or mate[3] != row[3]: continue
                if sum((a-b)**2 for a,b in zip(row[4], mate[4])) <= 600**2: nearby += 1
            coverage['samples'] += 1
            coverage['with_escort_within_600'] += nearby > 0
            coverage['with_two_escorts_within_600'] += nearby > 1
    route_trace = Counter()
    for rows in carrier_traces.values():
        for row in rows:
            route_trace['node_' + row['node']] += 1
            route_trace['home_unreachable_samples'] += row['home_time'] == 0
        for prev, row in zip(rows, rows[1:]):
            if row['time'] - prev['time'] > 1100: continue
            route_trace['paired_samples'] += 1
            route_trace['home_time_increases_by_50'] += (row['home_time'] > 0 and prev['home_time'] > 0 and
                                                       row['home_time'] >= prev['home_time'] + 50)
            route_trace['waypoint_changes'] += row['waypoint'] != prev['waypoint']
            if row['waypoint'] != prev['waypoint']:
                transition = ('pending_waypoint_switches' if prev['waypoint'] and row['waypoint'] else
                              'waypoint_to_direct' if prev['waypoint'] else 'direct_to_waypoint')
                route_trace[transition] += 1
    # Require an alive flag carrier and a current follow target. Bot thinks are
    # staggered at normal speed, so use preceding samples within 1.1 seconds.
    # This stricter node summary excludes confirmed follows of former carriers.
    positions = {(row[0], slot): row for slot, rows in tracks.items() for row in rows}
    escort_trace = Counter()
    for (time, slot), meta in metadata.items():
        row = positions.get((time, slot))
        index = bisect_right(clocks.get(meta['mate'], []), time) - 1
        carrier_row = tracks[meta['mate']][index] if index >= 0 else None
        carrier = metadata.get((carrier_row[0], meta['mate'])) if carrier_row else None
        if (not row or row[1] == 'd' or row[2] != 2 or meta['health'] <= 0 or meta['flag'] or
                not carrier or carrier['health'] <= 0 or not carrier['flag'] or
                not carrier_row or time - carrier_row[0] > 1100 or
                carrier_row[1] == 'd' or carrier_row[3] != row[3]):
            continue
        escort_trace['samples'] += 1
        escort_trace['node_' + meta['node']] += 1
        health_group = 'critical' if meta['health'] <= 40 else 'healthy'
        escort_trace[health_group + '_samples'] += 1
        escort_trace[health_group + '_item_samples'] += meta['node'] == 'item'
        escort_trace['within_600'] += sum((a-b)**2 for a,b in zip(row[4], carrier_row[4])) <= 600**2
    return {'run': str(run), 'status': result['status'], 'errors': result['errors'],
            'variant_hash': launch['qagame_sha256'], 'bots': len(result['initial']['bots']),
            'timescale': result['timescale'], 'requested_seconds': result['requested_seconds'],
            'team_scores': result['final']['team_scores'], 'kills': len(re.findall(r'Kill: \d+ \d+', text)),
            'events_after_live_start_approx': dict(events),
            'diagnostic_max_escorts': {team: max(counts) for team, counts in escorts.items()},
            'sampled_carrier_intervals': len(intervals),
            'sampled_carrier_median_seconds': median(lengths) if lengths else None,
            'sampled_carrier_max_seconds': max(lengths) if lengths else None,
            'carrier_intervals': intervals,
            'sampled_carrier_coverage': dict(coverage),
            'carrier_route_trace': dict(route_trace),
            'linked_escort_trace': dict(escort_trace),
            'measurement_limits': '1s position samples; events are between the initial and final measured score reports. Carrier intervals reflect LTG_RUSHBASE, exclude warmup/open intervals, and are not exact possession times. Nearby accompany jobs are an escort proximity proxy: stale follows can count and distance does not prove visibility/protection. Linked escort traces require the followed teammate to hold a flag in a preceding sample no older than 1.1s; deaths or flag changes between samples remain uncertain. Earlier role reports omit their representative bot.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runs', type=Path, nargs='+')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    data = [analyze(run) for run in args.runs]
    body = json.dumps(data, indent=2) + '\n'
    if args.output: args.output.write_text(body)
    else: print(body, end='')


if __name__ == '__main__':
    main()
