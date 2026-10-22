#!/usr/bin/env python3
"""Export one canonical manual and source-derived key diagrams; stdlib only.

This never builds or flashes firmware. PDF rendering is a separate opt-in step.
Refreshing the manifest is an explicit acknowledgement AFTER reviewing the prose.
"""
from pathlib import Path
import argparse
import base64
import datetime as dt
import hashlib
import html
import json
import re
import subprocess
import sys
import unicodedata

ROOT = Path(__file__).resolve().parents[1]
DOC = ROOT / 'docs/9981Pro_使用说明书.md'
MANIFEST = ROOT / 'docs/manual_manifest.json'
KEYMAP = ROOT / 'config/bbp9981.keymap'
LAYOUT = ROOT / 'config/boards/arm/bbp9981/bbp9981-layouts.dtsi'
FONT = Path('/usr/share/fonts/opentype/sourcehan/SourceHanSansSC-Regular.otf')
LAYERS = ['default', 'sym', 'upper', 'media']
DIAGRAM_NAMES = ['01_普通层', '02_SYM层', '03_aA层', '04_MEDIA层']
CAPS = ['左肩', '右肩', '通话', '黑莓', '触板', '返回', '结束'] + list('QWERTYUIOP') + list('ASDFGHJKL') + ['←', 'alt'] + list('ZXCVBNM') + ['$', 'Enter', '左aA', '0', 'Space', 'SYM', '右aA']
NAMES = dict(CLCK='Caps', LGUI='GUI / Cmd', K_BACK='Back', ESC='Esc', TAB='Tab', LSHFT='Shift', LCTRL='Ctrl', LALT='Alt', SPACE='空格', BSPC='退格', ENTER='Enter', C_VOL_UP='音量＋', C_VOL_DN='音量－', PSCRN='PrintScr', C_AC_SEARCH='搜索', HASH='#', UNDER='_', MINUS='−', PLUS='+', AT='@', STAR='*', SLASH='/', COLON=':', SEMI=';', APOS="'", DQT='"', DEL='Delete', QMARK='?', EXCL='!', COMMA=',', DOT='.', UP='↑', LEFT='←', DOWN='↓', RIGHT='→', C_EJECT='弹出', LT='<', GT='>', PIPE='|', KP_EQUAL='小键盘＝', BSLH='\\', AMPS='&', LBRC='{', RBRC='}', LBKT='[', RBKT=']', LPAR='(', RPAR=')', CARET='^', F4='F4', F5='F5', F12='F12')
NAMES.update({f'N{i}': str(i) for i in range(10)})


def sha(data):
    return hashlib.sha256(data).hexdigest()


def select_cursor_speed_keymap(text, enabled=None):
    """Resolve the one optional v2.3.3 keymap branch, not arbitrary CPP.

    Production DTS uses the same Kconfig boolean. Explicit enabled=False lets
    historical proofs retain the exact old map without changing their BASE.
    """
    if enabled is None:
        enabled = bool(re.search(r'^CONFIG_BBP9981_CURSOR_SPEED=y$',
                                 (ROOT / 'config/bbp9981.conf').read_text(), re.M))
    out, inside, take = [], False, False
    for line in text.splitlines(keepends=True):
        directive = line.strip()
        if directive == '#ifdef CONFIG_BBP9981_CURSOR_SPEED':
            assert not inside, 'Nested cursor-speed keymap branch'
            inside, take = True, enabled
        elif inside and directive == '#else':
            take = not enabled
        elif inside and directive == '#endif':
            inside = False
        elif inside:
            assert not directive.startswith(('#if ', '#ifdef ', '#ifndef ', '#elif ')), 'Unreviewed nested keymap feature'
            if take: out.append(line)
        else:
            out.append(line)
    assert not inside, 'Unclosed cursor-speed keymap branch'
    return ''.join(out)


def bindings(text):
    text = select_cursor_speed_keymap(text)
    result = {}
    for layer in LAYERS:
        m = re.search(r'\b' + layer + r'_layer\s*\{.*?bindings\s*=\s*<(.*?)>;', text, re.S)
        if not m:
            raise ValueError('Missing layer: ' + layer)
        body = re.sub(r'//[^\n]*', '', m.group(1))
        result[layer] = [' '.join(x.split()) for x in re.findall(r'&[^&]+', body)]
        if len(result[layer]) != 42:
            raise ValueError(f'{layer}: expected42, got {len(result[layer])}')
    return result


def source_paths():
    raw = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z', '--', 'config'], cwd=ROOT)
    return sorted(set(x.decode() for x in raw.split(b'\0') if x))


def source_hashes():
    return {p: sha((ROOT / p).read_bytes()) for p in source_paths()}


def label(binding, layer=None, position=None):
    parts = binding.split()
    b, a = parts[0], parts[1:]
    if b == '&kp': return [NAMES.get(a[0], a[0])]
    if b == '&mkp': return ['左键' if a[0] == 'LCLK' else '右键']
    if b == '&microtap_shoulder':
        if layer == 'default' and position == 0: return ['左键↔右键', '三击轻拍开关']
        if layer == 'default' and position == 1: return ['右键↔左键']
        return ['鼠标肩键角色互换']
    if b == '&aa_scroll_gesture_key':
        if layer == 'upper' and position == 3: return ['aA按住:槽1', '锁轮三击:固定左→上']
        if layer == 'upper' and position == 5: return ['aA按住:槽2', '锁轮三击:固定右→上']
        if layer == 'upper' and position == 34: return ['aA按住双击', '翻转鼠标']
        return ['aA层手势']
    if b == '&cursor_speed': return ['指针速度', '循环五档']
    if b == '&gamepad':
        return [['↑'], ['↓'], ['←'], ['→'], ['A'], ['B'], ['X'], ['Y'], ['LB'], ['RB'],
                ['LT'], ['RT'], ['View'], ['Menu'], ['Guide'], ['L3'], ['R3'],
                ['按住→右杆'], ['L3/R3', '按黑莓模式']][int(a[0])]
    if b == '&mt': return [NAMES[a[1]], '按住 ' + NAMES[a[0]]]
    if b == '&sk_kp': return ['Ctrl', '点按一次性']
    if b == '&aa_mo': return ['aA', '点一次 / 按住']
    if b == '&aa_layer': return ['固定 aA 层']
    if b == '&sl_mo': return ['SYM' if a[0] == '1' else 'aA', '点一次 / 按住']
    if b == '&tdc_to_SYM': return [';', '双击 SYM']
    if b == '&tdc_to_MEDIA': return ['静音', '双击 MEDIA']
    if b == '&msc': return ['滚↑' if a[0] == 'SCRL_UP' else '滚↓']
    if b == '&aa_scroll_shoulder':
        return ['滚↑', '锁定半速'] if a[0] == 'SCRL_UP' else ['滚↓', '锁定半速']
    if b == '&bt': return ['蓝牙槽 ' + a[-1]]
    if b == '&out': return ['BLE 偏好']
    if b == '&td0': return ['左键', '双击清当前槽']
    if b == '&sys_reset': return ['重启！']
    if b == '&ext_power': return ['外设电源！']
    if b == '&rgb_ug': return [{'RGB_BRD': '键盘灯−', 'RGB_BRI': '键盘灯＋', 'RGB_TOG': '键盘灯开关'}[a[0]]]
    if b == '&bl': return ['触板灯−' if a[0] == 'BL_DEC' else '触板灯＋']
    if b == '&td_boot_out': return ['单击刷机！', '双击切输出']
    if b == '&aa_sym_ctrl': return ['Ctrl / 退出']
    if b == '&to': return ['固定 aA 层' if a[0] == 'UPPER' else '回普通层']
    if b == '&none': return ['—']
    raise ValueError('Unknown binding; review manual: ' + binding)


def diagram(layer, items, geometry):
    out = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="-7 -7 1014 458" role="img" aria-label="' + layer + ' complete keymap">']
    for i, ((w, h, x, y), binding) in enumerate(zip(geometry, items)):
        yy, hh = y * .74, h * .74
        lines = label(binding, layer, i)
        danger = layer == 'upper' and i in (4, 10, 11, 35)
        tint = i in (37, 40, 41) or layer == 'upper' and i == 7 or binding.startswith('&gamepad ')
        fill, stroke, ink = ('#fff0f1', '#dc8491', '#a32839') if danger else (('#e7f6f5', '#95cccb', '#087d7e') if tint else ('#f8fafc', '#cbd6e2', '#20364d'))
        if binding == '&none': ink = '#9daabd'
        out.append(f'<g data-key="{i}" data-binding="{html.escape(binding, quote=True)}"><rect x="{x+4}" y="{yy+4}" width="{w-8}" height="{hh-8}" rx="9" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')
        for j, line in enumerate(lines):
            size = 30 if len(line) <= 2 and len(lines) == 1 else (22 if len(line) <= 6 else 17)
            units = sum(1.0 if unicodedata.east_asian_width(c) in 'WF' else 0.62 for c in line)
            size = round(min(size, (w - 22) / max(units, 1)), 2)
            ty = yy + 34 if len(lines) == 1 else yy + 25 + j * 21
            out.append(f'<text x="{x+w/2}" y="{ty}" text-anchor="middle" font-size="{size}" fill="{ink}" font-family="ManualHan,Arial,sans-serif">{html.escape(line)}</text>')
        out.append(f'<text x="{x+w-11}" y="{yy+hh-10}" text-anchor="end" font-size="12" fill="#7b8da2" font-family="ManualHan,Arial,sans-serif">{html.escape(CAPS[i])}</text></g>')
    return ''.join(out) + '</svg>'


def inline(text):
    # Transform escaped inline tokens, never arbitrary HTML from Markdown.
    pattern = r'\[([^\]]+)\]\(<([^>]+)>\)|\*\*(.+?)\*\*|`([^`]+)`'
    out, start = [], 0
    for m in re.finditer(pattern, text):
        out.append(html.escape(text[start:m.start()].replace('\\|', '|')))
        if m.group(1) is not None:
            out.append('<a href="' + html.escape(m.group(2), quote=True) + '">' + html.escape(m.group(1)) + '</a>')
        elif m.group(3) is not None:
            out.append('<strong>' + html.escape(m.group(3)) + '</strong>')
        else:
            out.append('<code>' + html.escape(m.group(4)) + '</code>')
        start = m.end()
    out.append(html.escape(text[start:].replace('\\|', '|')))
    return ''.join(out)


def cells(line):
    return [s.strip().replace('\\|', '|') for s in re.split(r'(?<!\\)\|', line.strip())[1:-1]]


def blocks(text):
    lines, result, i = text.splitlines(), [], 0
    while i < len(lines):
        s = lines[i].strip()
        if not s: i += 1; continue
        if s.startswith('### '): result.append(('h3', s[4:])); i += 1; continue
        m = re.fullmatch(r'!\[([^\]]+)\]\(<([^>]+)>\)', s)
        if m: result.append(('image', (m.group(1), m.group(2)))); i += 1; continue
        if s.startswith('|'):
            rows = []
            while i < len(lines) and lines[i].strip().startswith('|'):
                row = cells(lines[i])
                if not all(re.fullmatch(r':?-+:?', c.replace(' ', '')) for c in row): rows.append(row)
                i += 1
            if len({len(r) for r in rows}) != 1: raise ValueError('Ragged table: ' + str(rows))
            result.append(('table', rows)); continue
        if s.startswith('- ') or re.match(r'\d+\. ', s):
            rows = []
            ordered = not s.startswith('- ')
            while i < len(lines) and (re.match(r'\d+\. ', lines[i].strip()) if ordered else lines[i].strip().startswith('- ')):
                rows.append(re.sub(r'^\d+\. |^- ', '', lines[i].strip())); i += 1
            result.append(('ol' if ordered else 'ul', rows)); continue
        para = [s]; i += 1
        while i < len(lines) and lines[i].strip() and not re.match(r'^(### |\||!\[|- |\d+\. )', lines[i].strip()):
            para.append(lines[i].strip()); i += 1
        result.append(('p', ' '.join(para)))
    return result


def sections(md):
    parts = re.split(r'^## (.+)$', md, flags=re.M)
    return [(parts[i], blocks(parts[i+1])) for i in range(1, len(parts), 2)]


STYLE = '''
:root{--ink:#19304a;--muted:#607389;--blue:#125bb4;--line:#dbe5ef}*{box-sizing:border-box}
html,body{margin:0;padding:0}body{font-family:ManualHan,Arial,sans-serif;font-size:13px;line-height:1.58;color:var(--ink);background:#e9eef5;-webkit-print-color-adjust:exact;print-color-adjust:exact}
.page{width:148mm;height:210mm;position:relative;background:#fff;padding:9mm 10mm 14mm;break-after:page;page-break-after:always}.page:last-child{break-after:auto;page-break-after:auto}
.eyebrow{font-size:10px;letter-spacing:.7px;color:#125bb4;margin-bottom:7px}.edition{float:right;color:#607389}
h2{font-size:23px;line-height:1.3;margin:0 0 11px;font-weight:700}h3{font-size:15px;line-height:1.4;margin:13px 0 6px}p{margin:7px 0}ul,ol{padding-left:20px;margin:8px 0}li{margin:5px 0}strong{font-weight:700;color:#124d7d}
a{color:#125bb4;text-decoration:none;overflow-wrap:anywhere}code{font-family:Consolas,monospace;font-size:.9em;overflow-wrap:anywhere;word-break:break-all;color:#335576}
table{width:100%;border-collapse:collapse;font-size:11.6px;line-height:1.48;margin:9px 0;table-layout:fixed}th,td{padding:5px 6px;border-bottom:1px solid #dbe5ef;text-align:left;vertical-align:top;overflow-wrap:anywhere}th{background:#eaf2ff;color:#125bb4;font-weight:700}tr:nth-child(even) td{background:#f8fafc}th:first-child,td:first-child{width:32%}table[data-cols="4"] th,table[data-cols="4"] td{width:auto}table[data-cols="4"] th:nth-child(odd),table[data-cols="4"] td:nth-child(odd){width:9%}
.map{margin:6px 0}.map svg{width:100%;height:auto;display:block}.caption{color:#607389;font-size:10px;margin:2px 0 5px}.footer{position:absolute;bottom:7mm;left:10mm;right:10mm;border-top:1px solid #dbe5ef;padding-top:5px;font-size:9px;color:#607389;display:flex;justify-content:space-between}.content{display:flow-root}#p01 h2{font-size:28px}#p01 table th,#p01 table td,#p04 table th,#p04 table td{padding-top:3px;padding-bottom:3px}#p13 table th,#p13 table td{padding-top:1.5px;padding-bottom:1.5px}#p25 li{margin:4px 0}#p23 table{font-size:10.8px}#p23 p{font-size:12px}#p27 p{margin:6px 0}#p22 p,#p22 li,#p24 p,#p24 li,#p25 p,#p25 li{font-size:12.5px}#p17 table th,#p17 table td{padding-top:3px;padding-bottom:3px}
@page{size:A5;margin:0}@media screen{.page{margin:18px auto;box-shadow:0 8px 28px #19304a18}}@media print{body{background:#fff}.page{margin:0;box-shadow:none}}
'''


def render_html(sec, svgs, version, issued, source):
    font = base64.b64encode(FONT.read_bytes()).decode('ascii')
    out = ['<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>9981 Pro 使用说明书 v' + html.escape(version) + '</title><style>@font-face{font-family:ManualHan;src:url(data:font/otf;base64,' + font + ') format("opentype");font-weight:400;font-display:block}' + STYLE + '</style></head><body>']
    for n, (title, content) in enumerate(sec, 1):
        out.append(f'<section class="page" id="p{n:02}"><div class="eyebrow">9981 PRO / USER MANUAL <span class="edition">v{html.escape(version)} · {html.escape(issued)}</span></div><div class="content"><h2>{inline(title)}</h2>')
        for kind, value in content:
            if kind in ('p', 'h3'): out.append(f'<{kind}>{inline(value)}</{kind}>')
            elif kind == 'image':
                out.append('<figure class="map">' + svgs[Path(value[1]).stem] + '<figcaption class="caption">小字＝实体位置；大字＝本层绑定。红色＝维护风险；—＝无输出，不透明回落。</figcaption></figure>')
            elif kind == 'table':
                out.append(f'<table data-cols="{len(value[0])}"><thead><tr>' + ''.join('<th>' + inline(c) + '</th>' for c in value[0]) + '</tr></thead><tbody>')
                out.extend('<tr>' + ''.join('<td>' + inline(c) + '</td>' for c in row) + '</tr>' for row in value[1:])
                out.append('</tbody></table>')
            else: out.append('<' + kind + '>' + ''.join('<li>' + inline(c) + '</li>' for c in value) + '</' + kind + '>')
        out.append(f'</div><div class="footer"><span>9981 Pro · 默认键图 · {source[:7]}</span><span>{n:02} / {len(sec):02}</span></div></section>')
    return '\n'.join(out) + '</body></html>'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output-dir', type=Path, required=True)
    ap.add_argument('--source-commit')
    ap.add_argument('--refresh-manifest', action='store_true')
    ap.add_argument('--review-note')
    ap.add_argument('--word-only', action='store_true')
    a = ap.parse_args()
    out = a.output_dir.resolve()
    today = dt.datetime.now(dt.timezone(dt.timedelta(hours=8))).strftime('%m%d')
    if out.parent.name != today:
        raise SystemExit('Export to the current Asia/Shanghai MMDD task directory, not a historical directory')
    out.mkdir(parents=True, exist_ok=True)
    md = DOC.read_text(encoding='utf-8')
    meta = re.search(r'手册 v([0-9.]+) · (\d{4}-\d{2}-\d{2})', md)
    if not meta: raise SystemExit('Manual must state version and YYYY-MM-DD date')
    version, issued = meta.groups()
    previous = json.loads(MANIFEST.read_text()) if MANIFEST.is_file() else {}
    source_arg = a.source_commit or previous.get('source_commit')
    if not source_arg: raise SystemExit('--source-commit is required for first export')
    source = subprocess.check_output(['git', 'rev-parse', source_arg + '^{commit}'], cwd=ROOT, text=True).strip()
    sec = sections(md)
    if not sec or [int(re.match(r'\d+', title).group()) for title, _ in sec] != list(range(1, len(sec)+1)):
        raise SystemExit('Manual section numbering must be consecutive')
    if a.word_only:
        from manual_word import export_word
        export_word(sec, out)
        return
    keys = bindings(KEYMAP.read_text())
    geometry = [tuple(map(int, x)) for x in re.findall(r'&key_physical_attrs\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)', LAYOUT.read_text())]
    assert len(geometry) == len(CAPS) == 42
    svgs = {name: diagram(layer, keys[layer], geometry) for layer, name in zip(LAYERS, DIAGRAM_NAMES)}
    for directory in [ROOT / 'docs/说明书图示', out / '说明书图示']:
        directory.mkdir(parents=True, exist_ok=True)
        for name, svg in svgs.items(): (directory / (name + '.svg')).write_text(svg, encoding='utf-8')
    (out / '03_9981Pro使用说明书_离线阅读版.html').write_text(render_html(sec, svgs, version, issued, source), encoding='utf-8')
    (out / '04_9981Pro使用说明书_正文.md').write_text(md, encoding='utf-8')
    if a.refresh_manifest:
        if not a.source_commit or not a.review_note:
            raise SystemExit('Manifest refresh requires --source-commit and --review-note after prose review')
        source = subprocess.check_output(['git', 'rev-parse', a.source_commit + '^{commit}'], cwd=ROOT, text=True).strip()
        check = subprocess.run(['git', 'diff', '--quiet', source, '--', 'config'], cwd=ROOT)
        if check.returncode != 0: raise SystemExit('Source commit must describe the current config; do not attribute uncommitted functionality to an old commit')
        artifact = ROOT / 'firmware-delivery' / source / 'validation.json'
        provenance = json.loads(artifact.read_text()) if artifact.is_file() else {}
        head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
        data = {'manual_version': version, 'manual_date': issued, 'updated': dt.datetime.now(dt.timezone(dt.timedelta(hours=8))).isoformat(timespec='seconds'), 'source_commit': source, 'reference_delivery_commit': (previous.get('reference_delivery_commit') if previous.get('source_commit') == source else head) if artifact.is_file() else None, 'source_run': provenance.get('source_run'), 'device_tested': provenance.get('device_tested', False), 'review_note': a.review_note, 'manual': str(DOC.relative_to(ROOT)), 'manual_sha256': sha(DOC.read_bytes()), 'source_sha256': source_hashes(), 'keymap_bindings': keys, 'diagrams_sha256': {str((ROOT / 'docs/说明书图示' / (name + '.svg')).relative_to(ROOT)): sha(svg.encode()) for name, svg in svgs.items()}}
        MANIFEST.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'sections': len(sec), 'layers': len(keys), 'bindings': sum(map(len, keys.values())), 'output': str(out)}, ensure_ascii=False))


if __name__ == '__main__':
    main()
