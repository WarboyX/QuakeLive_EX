#!/usr/bin/env python3
"""Check match boundaries and actual carrier links in optional CTF traces."""
import importlib.util
import json
from pathlib import Path
from tempfile import TemporaryDirectory

spec = importlib.util.spec_from_file_location('ctf_analysis', Path(__file__).parent / 'test-environment/analyze-ctf.py')
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)

with TemporaryDirectory() as directory:
    run = Path(directory)
    initial = {'time': 1000, 'gametype': 5, 'warmup': 0, 'bots': [{}, {}]}
    final = dict(initial, time=2000, team_scores={'red': 0, 'blue': 0})
    (run / 'result.json').write_text(json.dumps({'initial': initial, 'final': final,
        'status': 'PASS', 'errors': [], 'timescale': 1, 'requested_seconds': 1}))
    (run / 'launch.json').write_text(json.dumps({'qagame_sha256': 'fixture'}))
    (run / 'server.log').write_text('''bottrack 500 0 1 0 0 0 0 s 2
ctftrack 500 0 hp100 flag0 mate1 via0 home100 nodefight
Bot scores at 1000 ms: gametype 5, warmup 0,
bottrack 1500 0 1 0 0 0 0 s 2
ctftrack 1500 0 hp100 flag0 mate1 via0 home100 nodeitem
bottrack 1250 1 1 10 0 0 0 s 5
ctftrack 1250 1 hp100 flag1 mate-1 via0 home100 nodeother
bottrack 1500 2 1 0 0 0 0 d 2
ctftrack 1500 2 hp0 flag0 mate1 via0 home100 nodefight
bottrack 1500 3 1 0 0 0 0 s 2
ctftrack 1500 3 hp100 flag0 mate4 via0 home100 nodefight
bottrack 1500 4 1 10 0 0 0 s 0
ctftrack 1500 4 hp100 flag0 mate-1 via0 home100 nodeother
bottrack 1500 5 2 0 0 0 0 s 2
ctftrack 1500 5 hp100 flag0 mate1 via0 home100 nodefight
Bot scores at 2000 ms: gametype 5, warmup 0,
bottrack 2500 0 1 0 0 0 0 s 2
ctftrack 2500 0 hp100 flag0 mate1 via0 home100 nodefight
bottrack 2500 1 1 10 0 0 0 s 5
ctftrack 2500 1 hp100 flag1 mate-1 via0 home100 nodeother
''')
    data = analysis.analyze(run)
    assert data['linked_escort_trace'] == {'samples': 1, 'node_item': 1, 'within_600': 1,
        'healthy_samples': 1, 'healthy_item_samples': 1}, data
    assert data['carrier_route_trace']['node_other'] == 1, data
    original = (run / 'server.log').read_text()
    transitions = '''ctftrack 1600 1 hp100 flag1 mate-1 via11 home100 nodeother
ctftrack 1900 1 hp100 flag1 mate-1 via22 home100 nodeother
ctftrack 1950 1 hp100 flag1 mate-1 via0 home100 nodeother
'''
    (run / 'server.log').write_text(original.replace('Bot scores at 2000', transitions + 'Bot scores at 2000'))
    routes = analysis.analyze(run)['carrier_route_trace']
    assert routes['direct_to_waypoint'] == routes['pending_waypoint_switches'] == routes['waypoint_to_direct'] == 1
    (run / 'server.log').write_text(original.replace('1500 0 hp100', '1500 0 hp40'))
    critical = analysis.analyze(run)['linked_escort_trace']
    assert critical['critical_samples'] == 1 and critical['critical_item_samples'] == 1
    assert 'healthy_samples' not in critical
    # A future sample or a sample older than 1.1s cannot prove a link.
    (run / 'server.log').write_text(original.replace('1250 1', '1600 1'))
    assert analysis.analyze(run)['linked_escort_trace'] == {}
    final['time'] = 3000
    (run / 'result.json').write_text(json.dumps({'initial': initial, 'final': final,
        'status': 'PASS', 'errors': [], 'timescale': 1, 'requested_seconds': 2}))
    (run / 'server.log').write_text(original.replace('1500 0', '2500 0').replace('Bot scores at 2000', 'Bot scores at 3000'))
    assert analysis.analyze(run)['linked_escort_trace'] == {}
    print('CTF trace analysis: live window, same-team carrier links, death and stale follow exclusions PASS')
