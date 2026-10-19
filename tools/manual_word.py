"""Editable DOCX export using standard-library OOXML, no installation needed."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
from xml.sax.saxutils import escape
import datetime
import re
import struct
import xml.etree.ElementTree as ET

W = 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'
NS = {'w': W}
HEADER = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'


def plain(text):
    return re.sub(r'\[([^\]]+)\]\(<([^>]+)>\)', r'\1（\2）', text).replace('\\|', '|')


def runs(text):
    text = plain(text)
    out = []
    for part in re.split(r'(\*\*.*?\*\*|`[^`]+`)', text):
        bold = part.startswith('**') and part.endswith('**')
        code = part.startswith('`') and part.endswith('`') and len(part) > 1
        value = part[2:-2] if bold else part[1:-1] if code else part
        props = ('<w:b/>' if bold else '') + ('<w:rFonts w:ascii="Consolas" w:eastAsia="Source Han Sans SC"/>' if code else '')
        out.append('<w:r><w:rPr>' + props + '</w:rPr><w:t xml:space="preserve">' + escape(value) + '</w:t></w:r>')
    return ''.join(out)


def paragraph(text, style='Normal', before=False):
    return '<w:p><w:pPr><w:pStyle w:val="' + style + '"/>' + ('<w:pageBreakBefore/>' if before else '') + '</w:pPr>' + runs(text) + '</w:p>'


def table(rows):
    cols, total = len(rows[0]), 9860
    widths = ([3155, 6705] if cols == 2 else [900, 4030, 900, 4030] if cols == 4 else [total // cols] * cols)
    out = ['<w:tbl><w:tblPr><w:tblW w:w="9860" w:type="dxa"/><w:tblLayout w:type="fixed"/><w:tblBorders><w:bottom w:val="single" w:sz="4" w:color="DBE5EF"/><w:insideH w:val="single" w:sz="4" w:color="DBE5EF"/></w:tblBorders><w:tblCellMar><w:top w:w="75" w:type="dxa"/><w:bottom w:w="75" w:type="dxa"/><w:left w:w="80" w:type="dxa"/><w:right w:w="80" w:type="dxa"/></w:tblCellMar></w:tblPr><w:tblGrid>' + ''.join(f'<w:gridCol w:w="{v}"/>' for v in widths) + '</w:tblGrid>']
    for n, row in enumerate(rows):
        out.append('<w:tr><w:trPr><w:cantSplit/>' + ('<w:tblHeader/>' if n == 0 else '') + '</w:trPr>')
        for col, cell in enumerate(row):
            shade = 'EAF2FF' if n == 0 else 'F8FAFC' if n % 2 == 0 else 'FFFFFF'
            value = '**' + cell.replace('**', '') + '**' if n == 0 else cell
            out.append(f'<w:tc><w:tcPr><w:tcW w:w="{widths[col]}" w:type="dxa"/><w:shd w:fill="{shade}"/><w:vAlign w:val="top"/></w:tcPr>' + paragraph(value, 'TableText') + '</w:tc>')
        out.append('</w:tr>')
    out.append('</w:tbl>')
    return ''.join(out)


def picture(rid, idx, path, caption):
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    width, height = struct.unpack('>II', data[16:24])
    cx, cy = 6200000, int(6200000 * height / width)
    return '<w:p><w:pPr><w:jc w:val="center"/></w:pPr><w:r><w:drawing><wp:inline distT="0" distB="0" distL="0" distR="0"><wp:extent cx="' + str(cx) + '" cy="' + str(cy) + '"/><wp:docPr id="' + str(idx) + '" name="' + escape(caption) + '"/><a:graphic><a:graphicData uri="http://schemas.openxmlformats.org/drawingml/2006/picture"><pic:pic><pic:nvPicPr><pic:cNvPr id="' + str(idx) + '" name="' + path.name + '"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed="' + rid + '"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="' + str(cx) + '" cy="' + str(cy) + '"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>'


def export_word(sections, out):
    target = out / '02_9981Pro使用说明书_可编辑版.docx'
    body, media, image_rels = [], {}, []
    for n, (title, blocks) in enumerate(sections, 1):
        body.append(paragraph('9981 PRO / 使用说明书', 'Eyebrow', before=n > 1))
        body.append(paragraph(title, 'Heading1'))
        for kind, value in blocks:
            if kind == 'table': body.append(table(value))
            elif kind == 'image':
                path = out / '说明书图示' / (Path(value[1]).stem + '.png')
                if not path.is_file(): raise SystemExit('Render the PDF/keymap PNG first: ' + str(path))
                idx = len(media) + 1
                rid, name = f'rIdImage{idx}', f'map{idx}.png'
                media[name] = path.read_bytes()
                image_rels.append(f'<Relationship Id="{rid}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="media/{name}"/>')
                body.append(picture(rid, idx, path, value[0]))
                body.append(paragraph('小字为实体键位，大字为当前层功能；—为显式无输出。', 'Small'))
            elif kind in ('ul', 'ol'):
                for i, text in enumerate(value, 1): body.append(paragraph((str(i) + '. ' if kind == 'ol' else '• ') + text))
            else: body.append(paragraph(value, 'Heading2' if kind == 'h3' else 'Normal'))
    # A4 editable source; the fixed A5 PDF remains the page-number authority.
    section = '<w:sectPr><w:footerReference w:type="default" r:id="rIdFooter"/><w:pgSz w:w="11906" w:h="16838"/><w:pgMar w:top="1020" w:right="1020" w:bottom="1020" w:left="1020" w:header="425" w:footer="425"/><w:pgNumType w:start="1"/></w:sectPr>'
    document = HEADER + '<w:document xmlns:w="' + W + '" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture"><w:body>' + ''.join(body) + section + '</w:body></w:document>'
    styles = HEADER + '<w:styles xmlns:w="' + W + '"><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Arial" w:hAnsi="Arial" w:eastAsia="Source Han Sans SC"/><w:sz w:val="22"/><w:color w:val="19304A"/><w:lang w:val="en-US" w:eastAsia="zh-CN"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after="100" w:line="320" w:lineRule="auto"/></w:pPr></w:pPrDefault></w:docDefaults><w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/></w:style>'
    for name, size, color in [('Heading1', 34, '125BB4'), ('Heading2', 26, '125BB4'), ('Eyebrow', 18, '607389'), ('Small', 18, '607389'), ('TableText', 20, '19304A')]:
        styles += f'<w:style w:type="paragraph" w:styleId="{name}"><w:name w:val="{name}"/><w:basedOn w:val="Normal"/><w:pPr>' + ('<w:keepNext/><w:spacing w:before="160" w:after="120"/>' if name.startswith('Heading') else '<w:spacing w:after="70" w:line="280" w:lineRule="auto"/>') + f'</w:pPr><w:rPr><w:sz w:val="{size}"/><w:color w:val="{color}"/>' + ('<w:b/>' if name.startswith('Heading') else '') + '</w:rPr></w:style>'
    styles += '</w:styles>'
    rel_ns = 'http://schemas.openxmlformats.org/package/2006/relationships'
    relationships = HEADER + '<Relationships xmlns="' + rel_ns + '"><Relationship Id="rIdStyles" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/><Relationship Id="rIdFooter" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer" Target="footer1.xml"/>' + ''.join(image_rels) + '</Relationships>'
    footer = HEADER + '<w:ftr xmlns:w="' + W + '"><w:p><w:pPr><w:jc w:val="right"/></w:pPr><w:r><w:rPr><w:sz w:val="16"/><w:color w:val="607389"/></w:rPr><w:t>9981 Pro 使用说明书 · </w:t></w:r><w:fldSimple w:instr="PAGE"/></w:p></w:ftr>'
    content_types = HEADER + '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Default Extension="png" ContentType="image/png"/>' + ''.join(f'<Override PartName="/{p}" ContentType="{c}"/>' for p, c in [('word/document.xml', 'application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml'), ('word/styles.xml', 'application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml'), ('word/footer1.xml', 'application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml'), ('docProps/core.xml', 'application/vnd.openxmlformats-package.core-properties+xml')]) + '</Types>'
    root_rels = HEADER + '<Relationships xmlns="' + rel_ns + '"><Relationship Id="rIdDocument" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/><Relationship Id="rIdCore" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/></Relationships>'
    core = HEADER + '<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>9981 Pro 使用说明书</dc:title><dc:language>zh-CN</dc:language><dc:description>由当前可维护Markdown与源码键图导出。Word分页随字体和软件变化，页码参照固定PDF。</dc:description></cp:coreProperties>'
    package = {'[Content_Types].xml': content_types, '_rels/.rels': root_rels, 'word/document.xml': document, 'word/styles.xml': styles, 'word/_rels/document.xml.rels': relationships, 'word/footer1.xml': footer, 'docProps/core.xml': core}
    for text in package.values(): ET.fromstring(text)
    with ZipFile(target, 'w', compression=ZIP_DEFLATED) as z:
        for name, text in package.items(): z.writestr(name, text.encode('utf-8'))
        for name, data in media.items(): z.writestr('word/media/' + name, data)
    with ZipFile(target) as z:
        assert z.testzip() is None
        d = ET.fromstring(z.read('word/document.xml'))
        assert len(d.findall('.//w:tbl', NS)) == sum(k == 'table' for _, bs in sections for k, _ in bs)
        assert len(d.findall('.//w:drawing', NS)) == 4
        titles = [p for p in d.findall('.//w:p', NS) if p.find('w:pPr/w:pStyle', NS) is not None and p.find('w:pPr/w:pStyle', NS).get('{' + W + '}val') == 'Heading1']
        assert len(titles) == len(sections)
    print('DOCX_VALIDATED', target.name, 'chapters', len(sections), 'images', len(media), 'bytes', target.stat().st_size)
