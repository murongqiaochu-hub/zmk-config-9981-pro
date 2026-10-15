#!/usr/bin/env python3
"""Compare actual C policies on local logs; never uploads data. No tap accuracy.
Usage: --baseline SOURCE --new-data DIR --legacy FILE --output DIR
Synthetic zero-motion 10ms polls interpolate log gaps. They are an explicit
model of the polling loop, not extra reads, contact labels or USB observations.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline', type=Path, required=True)
    p.add_argument('--new-data', type=Path, required=True)
    p.add_argument('--legacy', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    root = Path(__file__).resolve().parents[1]
    driver = Path('config/boards/arm/bbp9981/custom_driver')
    a.output.mkdir(parents=True, exist_ok=True)
    datasets = collections.OrderedDict()
    hashes = {}
    for directory in sorted(a.new_data.iterdir()):
        f = directory / 'frames.jsonl'
        if not f.is_file():
            continue
        hashes[directory.name] = hashlib.sha256(f.read_bytes()).hexdigest()
        rows = []
        for line in f.read_text().splitlines():
            r = json.loads(line)
            rows.append((r['t_ms'], r['dx'], r['dy'], int(r.get('read') == 'ok')))
        datasets['new:' + directory.name] = rows
    hashes['legacy'] = hashlib.sha256(a.legacy.read_bytes()).hexdigest()
    for line in a.legacy.read_text().splitlines():
        r = json.loads(line)
        if r.get('type') != 'firmware_log' or r.get('stage') == 'waiting_for_user':
            continue
        raw = r.get('raw_line', '')
        fields = dict(re.findall(r'(\w+)=([^\s]+)', raw))
        if not all(k in fields for k in ('uptime_ms', 'dx', 'dy', 'read')):
            continue
        row = (int(fields['uptime_ms']), int(fields['dx']), int(fields['dy']), int(fields['read'] == 'ok'))
        datasets.setdefault('old:' + r['stage'], []).append(row)
    record = {'method': 'actual C, explicit interpolated 10ms zero-motion polls; 130ms modelled tail; stage reset',
              'accuracy_measurable': False, 'input_sha256': hashes, 'results': {}}
    for policy, tree in [('baseline', a.baseline), ('candidate', root)]:
        exe = a.output / ('replay_' + policy)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wconversion',
                        '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        '-I', str(tree / driver), str(root / 'tools/replay_pulse_policy.c'), '-o', str(exe)], check=True)
        record['results'][policy] = {}
        for index, (stage, rows) in enumerate(datasets.items()):
            text = ''.join('%d %d %d %d\n' % row for row in rows)
            result = subprocess.run([str(exe)], input=text, text=True, capture_output=True, check=True)
            metrics = json.loads(result.stdout)
            metrics['source_rows'] = len(rows)
            events = [json.loads(line) for line in result.stderr.splitlines()]
            metrics['events_file'] = f'{policy}_{index:02d}_clicks.json'
            (a.output / metrics['events_file']).write_text(json.dumps(events, indent=2) + '\n')
            record['results'][policy][stage] = metrics
            print(policy, stage, metrics['click_candidates'], 'click candidates;', metrics['source_rows'], 'read rows')
    (a.output / 'comparison.json').write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    main()
