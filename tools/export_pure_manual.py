#!/usr/bin/env python3
"""Export the diagnostic supplement using existing local tools; never install."""
from pathlib import Path
import argparse
import base64
import hashlib
import json
import re
import subprocess
import tempfile
from zipfile import ZipFile, ZIP_DEFLATED
import xml.etree.ElementTree as ET
import markdown
from build_manual import ROOT, FONT, sections, blocks
from manual_word import paragraph, table, HEADER, W

def export_text_word(doc_sections, target):
    # The default manual exporter requires four key diagrams. This supplement
    # has no images: reuse its formatting primitives, not its four-image API.
    body=[]
    for title, items in doc_sections:
        body.append(paragraph(title,'Heading1'))
        for kind,value in items:
            if kind=='table': body.append(table(value))
            elif kind in ('ul','ol'):
                for index,text in enumerate(value,1):
                    body.append(paragraph((str(index)+'. ' if kind=='ol' else '• ')+text))
            elif kind in ('p','h3'): body.append(paragraph(value,'Heading2' if kind=='h3' else 'Normal'))
            else: raise ValueError('unexpected supplement block: '+kind)
    body.append('<w:sectPr><w:pgSz w:w="11906" w:h="16838"/><w:pgMar w:top="1020" w:right="1020" w:bottom="1020" w:left="1020"/></w:sectPr>')
    document=HEADER+'<w:document xmlns:w="'+W+'"><w:body>'+''.join(body)+'</w:body></w:document>'
    styles=HEADER+'<w:styles xmlns:w="'+W+'"><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Arial" w:eastAsia="Source Han Sans SC"/><w:sz w:val="22"/></w:rPr></w:rPrDefault></w:docDefaults>'
    for name,size in [('Normal',22),('Heading1',32),('Heading2',26),('TableText',20)]:
        styles+='<w:style w:type="paragraph" w:styleId="'+name+'"><w:name w:val="'+name+'"/><w:pPr><w:spacing w:after="100"/>'+('<w:keepNext/>' if name.startswith('Heading') else '')+'</w:pPr><w:rPr><w:sz w:val="'+str(size)+'"/>'+('<w:b/><w:color w:val="125BB4"/>' if name.startswith('Heading') else '')+'</w:rPr></w:style>'
    styles+='</w:styles>'
    package={'[Content_Types].xml':HEADER+'<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/><Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/></Types>',
             '_rels/.rels':HEADER+'<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>',
             'word/_rels/document.xml.rels':HEADER+'<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rIdStyles" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/></Relationships>',
             'word/document.xml':document,'word/styles.xml':styles}
    with ZipFile(target,'x',ZIP_DEFLATED) as archive:
        for name,xml in package.items():
            ET.fromstring(xml);archive.writestr(name,xml)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--validation',type=Path,help='independently verified Source 2 ARM/UF2 validation JSON')
    args=parser.parse_args()
    day=subprocess.check_output(['date','+%m%d'],text=True).strip()
    out=args.out.resolve()
    if '/output/'+day+'/' not in str(out) or out.exists():
        raise ValueError('use a NEW directory in the current-day output task')
    source=ROOT/'docs/9981Pro_纯BLE诊断候选说明.md'
    md=source.read_text()
    validation=None
    if args.validation:
        validation=json.loads(args.validation.read_text())
        if (validation.get('device_tested') is not False or
            validation.get('bluetooth_keyboard_mouse') is not False or
            validation.get('pnp',{}).get('vendor_source')!=2 or
            validation.get('pnp',{}).get('raw_hex')!='02501d5e610100' or
            not isinstance(validation.get('source_run'),int) or validation['source_run']<=0 or
            not all(str(value) in md for value in [validation.get('source_commit',''),
                                                   validation.get('source_run',''),
                                                   validation.get('uf2',{}).get('sha256','')]) or
            not re.fullmatch(r'[0-9a-f]{40}',validation.get('source_commit','')) or
            not re.fullmatch(r'[0-9a-f]{64}',validation.get('uf2',{}).get('sha256',''))):
            raise ValueError('validation JSON does not establish the Source 2 candidate described by the manual')
    elif 'Source 2 已通过真实ARM及独立静态验收' in md:
        raise ValueError('built Source 2 manual requires its independent validation JSON')
    font=base64.b64encode(FONT.read_bytes()).decode()
    css='''@page{size:A4;margin:17mm 16mm}body{font-family:ManualHan,sans-serif;font-size:10.5pt;line-height:1.6;color:#19304a;max-width:850px;margin:24px auto}h1{font-size:24pt;color:#125bb4}h2{font-size:16pt;margin-top:22px;break-after:avoid}h3{break-after:avoid}table{width:100%;border-collapse:collapse;font-size:9pt;margin:12px 0}th,td{padding:6px;border:1px solid #dbe5ef;text-align:left;vertical-align:top}th{background:#eaf2ff}thead{display:table-header-group}tr{break-inside:avoid}code,a{overflow-wrap:anywhere}a{color:#125bb4}code{font-family:monospace;font-size:.9em}strong{color:#124d7d}@media print{body{margin:0;max-width:none}}'''
    html=markdown.markdown(md,extensions=['tables','fenced_code'])
    html=html.replace('href="9981Pro_使用说明书.md"','href="04_默认手册_只读参考.md"')
    html=html.replace('href="../experiments/gamepad_protocol/README.md"','href="参考源码/experiments/gamepad_protocol/README.md"')
    html=html.replace('href="../tools/stage_pure_gamepad.py"','href="参考源码/tools/stage_pure_gamepad.py"')
    html='<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><title>9981 Pro 纯BLE诊断与PnP来源对照</title><style>@font-face{font-family:ManualHan;src:url(data:font/otf;base64,'+font+')}'+css+'</style></head><body>'+html+'</body></html>'
    out.mkdir(parents=True,exist_ok=False)
    htmlpath=out/'03_纯BLE诊断候选说明_离线阅读.html'
    htmlpath.write_text(html)
    (out/'04_默认手册_只读参考.md').write_bytes((ROOT/'docs/9981Pro_使用说明书.md').read_bytes())
    (out/'05_纯BLE诊断候选说明_正文.md').write_text(md)
    # Keep linked candidate references readable offline; never copy a UF2 or
    # pretend that this source snapshot is an ARM/device validation.
    reference=out/'参考源码'
    experiment=reference/'experiments/gamepad_protocol';experiment.mkdir(parents=True)
    for path in (ROOT/'experiments/gamepad_protocol').iterdir():
        if path.is_symlink(): raise ValueError('reference source symlink is not allowed')
        if path.is_file(): (experiment/path.name).write_bytes(path.read_bytes())
    (reference/'tools').mkdir()
    (reference/'tools/stage_pure_gamepad.py').write_bytes((ROOT/'tools/stage_pure_gamepad.py').read_bytes())
    assert (experiment/'README.md').is_file() and (reference/'tools/stage_pure_gamepad.py').is_file()
    diagrams=out/'说明书图示';diagrams.mkdir()
    for path in (ROOT/'docs/说明书图示').glob('*.svg'):
        (diagrams/path.name).write_bytes(path.read_bytes())
    pdf=out/'01_纯BLE诊断候选说明.pdf'
    with tempfile.TemporaryDirectory(prefix='9981-manual-') as directory:
        temp=Path(directory)
        title='纯BLE诊断候选 · P1及来源2构建候选' if validation else '纯BLE诊断候选 · P1及来源对照预研'
        doc_sections=[(title,blocks(md.split('\n',1)[1].split('\n## ',1)[0]))]+sections(md)
        docx=out/'02_纯BLE诊断候选说明_可编辑版.docx'
        export_text_word(doc_sections,docx)
        subprocess.run(['chromium','--headless','--disable-gpu','--no-sandbox',
                        '--user-data-dir='+str(temp/'chromium'),'--no-pdf-header-footer',
                        '--print-to-pdf='+str(pdf),htmlpath.as_uri()],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        text=temp/'pdf.txt'
        subprocess.run(['pdftotext','-layout',str(pdf),str(text)],check=True)
        pdftext=''.join(text.read_text().split())
        with ZipFile(docx) as archive:
            xml=ET.fromstring(archive.read('word/document.xml'))
            wordtext=''.join(xml.itertext())
        for phrase in ['GCController.extendedGamepad','未刷写','蓝牙','PnP','1e277b08428984a6120c42310c34a6ad40eaaecdf491cbfb1d706e58c3359264']:
            assert phrase in pdftext and phrase in wordtext,phrase
        assert 'Source2' in pdftext and 'Source 2' in wordtext
        info=subprocess.check_output(['pdfinfo',str(pdf)],text=True)
        pages=int(re.search(r'^Pages:\s+(\d+)',info,re.M)[1]);assert 1<=pages<=15
    metadata=dict(version='P1+PnP-source2-arm-validated' if validation else 'P1+PnP-source2-research',pages=pages,
                  p1_user_reported_test=True,p1_flash_identity_independently_verified=False,
                  pnp_source2_arm_built=validation is not None,pnp_source2_device_tested=False,
                  source_manual_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),files={})
    if validation:
        metadata.update(source2_commit=validation['source_commit'],source2_run=validation['source_run'],
                        source2_uf2_sha256=validation['uf2']['sha256'],
                        validation_sha256=hashlib.sha256(args.validation.read_bytes()).hexdigest())
    for path in sorted(out.iterdir()):
        if path.is_file():
            metadata['files'][path.name]=dict(bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    (out/'06_文档验证.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(metadata,ensure_ascii=False,indent=2))

if __name__=='__main__':
    main()
