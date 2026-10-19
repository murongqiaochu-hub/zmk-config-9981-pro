#!/usr/bin/env python3
"""Fail closed when config/keymaps or the canonical manual outgrow review metadata.

Checks detect omissions, not semantic truth or hardware acceptance. A reviewer must
read the manual before explicitly refreshing the manifest. No firmware/device I/O.
"""
from pathlib import Path
import copy
import hashlib
import json
import re
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build_manual import bindings, source_hashes, DIAGRAM_NAMES, LAYERS, sections


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def snapshot_errors(manifest, observed, doc_digest):
    expected = manifest['source_sha256']
    errors = []
    for p in sorted(set(expected) | set(observed)):
        if expected.get(p) != observed.get(p): errors.append('unreviewed source: ' + p)
    if manifest['manual_sha256'] != doc_digest: errors.append('manual changed without reviewed manifest')
    return errors


def run():
    manifest_path = ROOT / 'docs/manual_manifest.json'
    assert manifest_path.is_file(), 'Missing reviewed manual manifest; update the manual before delivery'
    m = json.loads(manifest_path.read_text())
    doc = ROOT / m['manual']
    errors = snapshot_errors(m, source_hashes(), digest(doc))
    assert not errors, '\n'.join(errors) + '\nUpdate prose + diagrams + review manifest in the same change.'
    text = doc.read_text()
    assert len(sections(text)) >= 10
    assert '手册 v' + m['manual_version'] in text
    assert m['source_commit'] in text, 'Prose must identify its functionality source commit'
    assert m['manual_date'] in text
    primary = bindings((ROOT / 'config/bbp9981.keymap').read_text())
    board = bindings((ROOT / 'config/boards/arm/bbp9981/bbp9981.keymap').read_text())
    assert primary == board == m['keymap_bindings'], 'Review all layers of both keymaps'
    ns = {'s': 'http://www.w3.org/2000/svg'}
    for layer, name in zip(LAYERS, DIAGRAM_NAMES):
        p = ROOT / 'docs/说明书图示' / (name + '.svg')
        assert digest(p) == m['diagrams_sha256'][str(p.relative_to(ROOT))]
        tree = ET.fromstring(p.read_text())
        keys = tree.findall('s:g', ns)
        assert len(keys) == 42
        assert [int(k.attrib['data-key']) for k in keys] == list(range(42))
        assert [k.attrib['data-binding'] for k in keys] == primary[layer], 'Stale layer diagram: ' + layer
        assert name + '.svg' in text, 'Prose omits diagram: ' + name
    # Negative self-checks avoid changing real source just to test the gate.
    baseline = dict(m['source_sha256'])
    changed = dict(baseline); first = next(iter(changed)); changed[first] = '0' * 64
    assert snapshot_errors(m, changed, m['manual_sha256'])
    added = dict(baseline); added['config/new_feature.h'] = 'f' * 64
    assert snapshot_errors(m, added, m['manual_sha256'])
    removed = dict(baseline); removed.pop(first)
    assert snapshot_errors(m, removed, m['manual_sha256'])
    assert snapshot_errors(m, baseline, '0' * 64)
    assert not snapshot_errors(m, baseline, m['manual_sha256'])
    assert (ROOT / 'AGENTS.md').is_file()
    rule = (ROOT / 'AGENTS.md').read_text()
    assert '必须' in rule and '说明书' in rule and 'check_manual_sync.py' in rule
    print(f'PASS manual sync: {len(baseline)} config files, 4x42 bindings/diagrams, v{m["manual_version"]}, modified/added/removed config + stale prose negatives; NOT a semantic or hardware acceptance test')


if __name__ == '__main__':
    run()
