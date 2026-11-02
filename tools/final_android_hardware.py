"""Unchanged historical full Kconfig/DTS comparison, not a new device test."""
from pathlib import Path
import re,json,hashlib
from elf_hid_artifact import require
BASELINE=Path(__file__).resolve().parents[1]/"tests/fixtures/final_android_baseline"
BASELINE_CONFIG_SHA="51e29321a412aaa8ce1d27e108fd2fdd030daa2e61cd6ffe120bfeb0190f0ce4"
BASELINE_DTS_SHA="77d47ac09693cfafe1f9c69717e5cd520341cbea59d5daf015eaac82cf007f99"
EXPECTED_CONFIG_CHANGES={}

def assigned_config(text):
    pairs=re.findall(r'^(CONFIG_\w+)=(.*)$',text,re.M)
    require(len(pairs)==len(dict(pairs)),'duplicate assigned Kconfig value')
    return dict(pairs)

def normalize_dts(text):
    # Actual Zephyr dts output, not compiler diagnostics or arbitrary fragments.
    require(text.count('/dts-v1/;')==1 and text.rstrip().endswith('};'),'invalid complete DTS')
    text=text[text.index('/dts-v1/;'):].replace('\r\n','\n')
    # Generated numeric assignments may renumber; all node properties, references,
    # bindings, quoted strings and hardware fields remain byte-exact.
    return re.sub(r'(?m)^[ \t]*phandle = < 0x[0-9a-f]+ >;\n','',text).strip()+'\n'

def check_standard(config,dts):
    raw=(BASELINE/'assigned_config.json').read_bytes();raw_dts=(BASELINE/'zephyr.dts').read_bytes()
    require(hashlib.sha256(raw).hexdigest()==BASELINE_CONFIG_SHA,'standard config fixture changed')
    require(hashlib.sha256(raw_dts).hexdigest()==BASELINE_DTS_SHA,'standard DTS fixture changed')
    meta=json.loads((BASELINE/'provenance.json').read_text())
    require(meta['run_id']==39743108339 and meta['build_commit']=='e2c4966136b5e6af5db795c6c46048672758ba30','wrong standard provenance')
    before=json.loads(raw);after=assigned_config(config)
    require(len(before)==1170,'incomplete standard config baseline')
    changes={key:{'before':before.get(key),'after':after.get(key)} for key in sorted(set(before)|set(after)) if before.get(key)!=after.get(key)}
    require(changes==EXPECTED_CONFIG_CHANGES,'unexpected complete Kconfig diff: '+json.dumps(changes,sort_keys=True))
    require(normalize_dts(dts)==normalize_dts(raw_dts.decode()),'standard DTS/hardware/keymap differs outside generated phandle assignments')
    return dict(run_id=39743108339,build_commit=meta['build_commit'],assigned_values=len(after),
                expected_config_changes=EXPECTED_CONFIG_CHANGES,complete_normalized_dts_identical=True,
                config_fixture_sha256=BASELINE_CONFIG_SHA,dts_fixture_sha256=BASELINE_DTS_SHA)
