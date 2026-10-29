#!/usr/bin/env python3
"""Read-only workflow authorization checks; no network or dispatch."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def main():
    text=(ROOT/'.github/workflows/main.yml').read_text()
    job=text.split('  composite_trial:\n',1)[1].split('  failure_context:\n',1)[0]
    assert "github.event_name == 'workflow_dispatch' && inputs.profile == 'ble-composite-trial'" in job
    assert "github.ref == 'refs/heads/codex/composite-device-trial-20261030'" in job
    assert "github.repository == 'murongqiaochu-hub/zmk-config-9981-pro'" in job
    assert 'timeout-minutes: 40' in job and 'permissions:\n      contents: read\n' in job
    assert 'actions: write' not in job and 'contents: write' not in job
    assert '--accept-experimental-device-trial --acknowledge-hid-database-change' in job
    assert 'tools/verify_composite_trial.py' in job and '--allow-dirty-local-source' not in job
    assert 'persist-credentials: false' in job and 'git push' not in job
    assert 'test ! -e "$COMPOSITE_BUILD"' in job
    assert "inputs.profile == 'standard'" in text.split('  build:\n',1)[1].split('  pure_candidate:\n',1)[0]
    assert "github.head_ref != 'codex/composite-device-trial-20261030'" in text
    assert "github.base_ref != 'codex/composite-device-trial-20261030'" in text
    assert "github.ref != 'refs/heads/codex/composite-device-trial-20261030'" in text
    assert text.count("refs/heads/codex/xbox-layout-20261020")==1
    assert text.count('HEAD:codex/xbox-layout-20261020')==1
    assert 'tools/verify_pure_candidate.py' in text
    print('PASS manual trial repo/ref/read-only/fresh-build gates; pure/delivery retained')
if __name__=='__main__': main()
