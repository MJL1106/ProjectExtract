"""Convert companion_testing.md to a styled PDF using reportlab."""
import re
from pathlib import Path
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import cm
from reportlab.lib import colors
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle,
    PageBreak, KeepTogether, HRFlowable
)
from reportlab.lib.enums import TA_LEFT

SRC = Path(r"C:\Users\matth\Documents\Github\ProjectExtract\agent_docs\companion_testing.md")
OUT = Path(r"C:\Users\matth\Documents\Github\ProjectExtract\agent_docs\companion_testing.pdf")

styles = getSampleStyleSheet()
H1 = ParagraphStyle('H1', parent=styles['Heading1'], fontSize=20, textColor=colors.HexColor('#1a1a1a'),
                    spaceAfter=8, spaceBefore=10, fontName='Helvetica-Bold')
H2 = ParagraphStyle('H2', parent=styles['Heading2'], fontSize=14, textColor=colors.HexColor('#2c5aa0'),
                    spaceAfter=6, spaceBefore=12, fontName='Helvetica-Bold')
H3 = ParagraphStyle('H3', parent=styles['Heading3'], fontSize=12, textColor=colors.HexColor('#444'),
                    spaceAfter=4, spaceBefore=8, fontName='Helvetica-Bold')
BODY = ParagraphStyle('Body', parent=styles['BodyText'], fontSize=10, leading=14, alignment=TA_LEFT)
BULLET = ParagraphStyle('Bullet', parent=BODY, leftIndent=14, bulletIndent=2)
CODE = ParagraphStyle('Code', parent=BODY, fontName='Courier', fontSize=9, textColor=colors.HexColor('#444'),
                      leftIndent=8, backColor=colors.HexColor('#f4f4f4'))


def inline_format(text: str) -> str:
    """Convert markdown inline formatting to reportlab XML."""
    # Code spans
    text = re.sub(r'`([^`]+)`', r'<font name="Courier" backColor="#f4f4f4">\1</font>', text)
    # Bold
    text = re.sub(r'\*\*([^*]+)\*\*', r'<b>\1</b>', text)
    # Italic
    text = re.sub(r'(?<!\*)\*([^*]+)\*(?!\*)', r'<i>\1</i>', text)
    # Escape unsupported
    return text


def parse_markdown(md_text: str):
    flowables = []
    lines = md_text.split('\n')
    i = 0
    while i < len(lines):
        line = lines[i].rstrip()

        # Horizontal rule
        if line.strip() in ('---', '***'):
            flowables.append(Spacer(1, 4))
            flowables.append(HRFlowable(width="100%", thickness=0.5, color=colors.HexColor('#ccc')))
            flowables.append(Spacer(1, 4))
            i += 1
            continue

        # Headings
        if line.startswith('### '):
            flowables.append(Paragraph(inline_format(line[4:]), H3))
            i += 1
            continue
        if line.startswith('## '):
            flowables.append(Paragraph(inline_format(line[3:]), H2))
            i += 1
            continue
        if line.startswith('# '):
            flowables.append(Paragraph(inline_format(line[2:]), H1))
            i += 1
            continue

        # Tables (markdown style)
        if line.startswith('|') and i + 1 < len(lines) and re.match(r'^\|[\s\-:|]+\|$', lines[i + 1].strip()):
            table_rows = []
            header = [c.strip() for c in line.strip('|').split('|')]
            table_rows.append(header)
            i += 2  # skip header + separator
            while i < len(lines) and lines[i].strip().startswith('|'):
                row = [c.strip() for c in lines[i].strip('|').split('|')]
                table_rows.append(row)
                i += 1
            # Build paragraph cells for word wrapping
            wrapped = [[Paragraph(inline_format(cell), BODY) for cell in row] for row in table_rows]
            t = Table(wrapped, repeatRows=1, colWidths=None, hAlign='LEFT')
            t.setStyle(TableStyle([
                ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#2c5aa0')),
                ('TEXTCOLOR', (0, 0), (-1, 0), colors.white),
                ('FONTNAME', (0, 0), (-1, 0), 'Helvetica-Bold'),
                ('GRID', (0, 0), (-1, -1), 0.5, colors.HexColor('#ccc')),
                ('VALIGN', (0, 0), (-1, -1), 'TOP'),
                ('LEFTPADDING', (0, 0), (-1, -1), 6),
                ('RIGHTPADDING', (0, 0), (-1, -1), 6),
                ('TOPPADDING', (0, 0), (-1, -1), 4),
                ('BOTTOMPADDING', (0, 0), (-1, -1), 4),
                ('ROWBACKGROUNDS', (0, 1), (-1, -1), [colors.white, colors.HexColor('#f9f9f9')]),
            ]))
            flowables.append(Spacer(1, 4))
            flowables.append(t)
            flowables.append(Spacer(1, 8))
            continue

        # Code blocks
        if line.startswith('```'):
            i += 1
            code_lines = []
            while i < len(lines) and not lines[i].startswith('```'):
                code_lines.append(lines[i])
                i += 1
            i += 1  # skip closing ```
            for cl in code_lines:
                flowables.append(Paragraph(cl.replace(' ', '&nbsp;') or '&nbsp;', CODE))
            flowables.append(Spacer(1, 6))
            continue

        # Bullet lists
        if line.startswith('- '):
            flowables.append(Paragraph(f'• {inline_format(line[2:])}', BULLET))
            i += 1
            continue

        # Numbered lists
        m = re.match(r'^(\d+)\.\s+(.*)', line)
        if m:
            flowables.append(Paragraph(f'{m.group(1)}. {inline_format(m.group(2))}', BULLET))
            i += 1
            continue

        # Blank line
        if not line.strip():
            flowables.append(Spacer(1, 4))
            i += 1
            continue

        # Plain paragraph
        flowables.append(Paragraph(inline_format(line), BODY))
        i += 1

    return flowables


def build_pdf():
    md_text = SRC.read_text(encoding='utf-8')
    flowables = parse_markdown(md_text)
    doc = SimpleDocTemplate(
        str(OUT), pagesize=A4,
        leftMargin=2 * cm, rightMargin=2 * cm,
        topMargin=2 * cm, bottomMargin=2 * cm,
        title="AI Companion Testing Plan",
        author="Extraction Project",
    )
    doc.build(flowables)
    print(f"Wrote {OUT} ({OUT.stat().st_size:,} bytes)")


if __name__ == '__main__':
    build_pdf()
