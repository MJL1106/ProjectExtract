"""Convert the enemy as-built + gap/setup markdown docs to styled PDFs (reportlab).

Generalised from _md_to_pdf.py with two fixes that doc set needs:
 - sanitise glyphs outside WinAnsi (arrows, checkmarks, box-drawing) that the
   standard Helvetica/Courier fonts render as black boxes
 - XML-escape & < > before applying inline markup (the docs contain '<40' etc.)
"""
import re
from pathlib import Path
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import cm
from reportlab.lib import colors
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, HRFlowable
)
from reportlab.lib.enums import TA_LEFT

BASE = Path(r"C:\Users\matth\Documents\Github\ProjectExtract\agent_docs")

JOBS = [
    (BASE / "enemy_gameplay_as_built.md", BASE / "enemy_gameplay_as_built.pdf",
     "Extraction - Enemy AI: As-Built Gameplay Doc"),
    (BASE / "enemy_gaps_and_setup.md", BASE / "enemy_gaps_and_setup.pdf",
     "Extraction - Enemy AI: Gap Closure & In-Engine Setup"),
]

# Glyphs the PDF base-14 fonts can't draw -> ASCII/WinAnsi stand-ins
SANITISE = {
    '→': '->', '←': '<-', '↔': '<->',
    '✓': 'OK', '✔': 'OK', '✗': 'x', '✘': 'x',
    '≤': '<=', '≥': '>=', '−': '-', '≈': '~',
    '├': '+', '└': '+', '┌': '+', '┐': '+',
    '┘': '+', '┤': '+', '─': '-', '│': '|',
    '●': '*', '○': 'o', '…': '...',
    ' ': ' ', '‑': '-',
    '\\*': '*',
}
# WinAnsi-safe glyphs we keep: — – • · ° × ± ½ § …(mapped anyway)

WINANSI_OK = set(
    '—–•·°×±½§‘’“”é£'
)


def sanitise(text: str) -> str:
    for k, v in SANITISE.items():
        text = text.replace(k, v)
    # last-resort: replace any remaining non-latin-1 char so nothing renders as a box
    out = []
    for ch in text:
        if ord(ch) < 256 or ch in WINANSI_OK:
            out.append(ch)
        else:
            out.append('?')
    return ''.join(out)


def xml_escape(text: str) -> str:
    return text.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def inline_format(text: str) -> str:
    """Markdown inline -> reportlab paragraph XML (escape first, then add tags)."""
    text = xml_escape(text)
    text = re.sub(r'`([^`]+)`', r'<font name="Courier" backColor="#f4f4f4">\1</font>', text)
    text = re.sub(r'\*\*([^*]+)\*\*', r'<b>\1</b>', text)
    text = re.sub(r'(?<!\*)\*([^*\n]+)\*(?!\*)', r'<i>\1</i>', text)
    return text


styles = getSampleStyleSheet()
H1 = ParagraphStyle('H1', parent=styles['Heading1'], fontSize=19, textColor=colors.HexColor('#1a1a1a'),
                    spaceAfter=8, spaceBefore=10, fontName='Helvetica-Bold')
H2 = ParagraphStyle('H2', parent=styles['Heading2'], fontSize=14, textColor=colors.HexColor('#2c5aa0'),
                    spaceAfter=6, spaceBefore=12, fontName='Helvetica-Bold')
H3 = ParagraphStyle('H3', parent=styles['Heading3'], fontSize=12, textColor=colors.HexColor('#444'),
                    spaceAfter=4, spaceBefore=8, fontName='Helvetica-Bold')
H4 = ParagraphStyle('H4', parent=styles['Heading3'], fontSize=10.5, textColor=colors.HexColor('#2c5aa0'),
                    spaceAfter=3, spaceBefore=7, fontName='Helvetica-Bold')
BODY = ParagraphStyle('Body', parent=styles['BodyText'], fontSize=10, leading=14, alignment=TA_LEFT)
BULLET = ParagraphStyle('Bullet', parent=BODY, leftIndent=14, bulletIndent=2)
CODE = ParagraphStyle('Code', parent=BODY, fontName='Courier', fontSize=8.5, leading=11,
                      textColor=colors.HexColor('#333'), leftIndent=8, backColor=colors.HexColor('#f4f4f4'))
CELL = ParagraphStyle('Cell', parent=BODY, fontSize=8, leading=10.5)
CELL_HDR = ParagraphStyle('CellHdr', parent=CELL, fontName='Helvetica-Bold', textColor=colors.white)


def make_table(rows):
    n_cols = max(len(r) for r in rows)
    rows = [r + [''] * (n_cols - len(r)) for r in rows]
    wrapped = [[Paragraph(inline_format(c), CELL_HDR if i == 0 else CELL) for c in row]
               for i, row in enumerate(rows)]
    usable = A4[0] - 4 * cm
    t = Table(wrapped, repeatRows=1, hAlign='LEFT', colWidths=[usable / n_cols] * n_cols)
    t.setStyle(TableStyle([
        ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#2c5aa0')),
        ('GRID', (0, 0), (-1, -1), 0.5, colors.HexColor('#ccc')),
        ('VALIGN', (0, 0), (-1, -1), 'TOP'),
        ('LEFTPADDING', (0, 0), (-1, -1), 4),
        ('RIGHTPADDING', (0, 0), (-1, -1), 4),
        ('TOPPADDING', (0, 0), (-1, -1), 3),
        ('BOTTOMPADDING', (0, 0), (-1, -1), 3),
        ('ROWBACKGROUNDS', (0, 1), (-1, -1), [colors.white, colors.HexColor('#f5f7fa')]),
    ]))
    return t


def parse_markdown(md_text: str):
    flowables = []
    lines = sanitise(md_text).split('\n')
    i = 0
    while i < len(lines):
        line = lines[i].rstrip()

        if line.strip() in ('---', '***'):
            flowables += [Spacer(1, 4), HRFlowable(width="100%", thickness=0.5,
                                                   color=colors.HexColor('#ccc')), Spacer(1, 4)]
            i += 1
            continue

        if line.startswith('#### '):
            flowables.append(Paragraph(inline_format(line[5:]), H4)); i += 1; continue
        if line.startswith('### '):
            flowables.append(Paragraph(inline_format(line[4:]), H3)); i += 1; continue
        if line.startswith('## '):
            flowables.append(Paragraph(inline_format(line[3:]), H2)); i += 1; continue
        if line.startswith('# '):
            flowables.append(Paragraph(inline_format(line[2:]), H1)); i += 1; continue

        if line.startswith('|') and i + 1 < len(lines) and re.match(r'^\|[\s\-:|]+\|$', lines[i + 1].strip()):
            rows = [[c.strip() for c in line.strip('|').split('|')]]
            i += 2
            while i < len(lines) and lines[i].strip().startswith('|'):
                rows.append([c.strip() for c in lines[i].strip('|').split('|')])
                i += 1
            flowables += [Spacer(1, 4), make_table(rows), Spacer(1, 8)]
            continue

        if line.startswith('```'):
            i += 1
            while i < len(lines) and not lines[i].startswith('```'):
                cl = xml_escape(lines[i]).replace(' ', '&nbsp;') or '&nbsp;'
                flowables.append(Paragraph(cl, CODE))
                i += 1
            i += 1
            flowables.append(Spacer(1, 6))
            continue

        if line.startswith('- '):
            flowables.append(Paragraph(f'• {inline_format(line[2:])}', BULLET)); i += 1; continue

        m = re.match(r'^(\d+)\.\s+(.*)', line)
        if m:
            flowables.append(Paragraph(f'{m.group(1)}. {inline_format(m.group(2))}', BULLET)); i += 1; continue

        if not line.strip():
            flowables.append(Spacer(1, 4)); i += 1; continue

        flowables.append(Paragraph(inline_format(line), BODY)); i += 1

    return flowables


def build(src: Path, out: Path, title: str):
    doc = SimpleDocTemplate(
        str(out), pagesize=A4,
        leftMargin=2 * cm, rightMargin=2 * cm, topMargin=2 * cm, bottomMargin=2 * cm,
        title=title, author="Extraction Project",
    )
    doc.build(parse_markdown(src.read_text(encoding='utf-8')))
    print(f"Wrote {out.name} ({out.stat().st_size:,} bytes)")


if __name__ == '__main__':
    for src, out, title in JOBS:
        build(src, out, title)
