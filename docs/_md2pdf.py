"""Convert the two companion GDD markdowns to PDFs via reportlab.

Self-contained on Windows (no GTK/pango dependency). Handles:
- # / ## / ### headings
- Paragraphs with **bold**, *italic*, `inline code`
- Bullet lists (- or *) with hanging indent
- Pipe-delimited markdown tables
- Hard rule under H1 + subtitle

Usage: python _md2pdf.py
"""
from __future__ import annotations
import re
from pathlib import Path
from html import escape

from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.lib.colors import HexColor
from reportlab.lib.enums import TA_JUSTIFY, TA_LEFT
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, HRFlowable,
    KeepTogether,
)

HERE = Path(__file__).resolve().parent

DOCS = [
    ("companion-gdd-gameplay-2026-05-23.md", "companion-gdd-gameplay-2026-05-23.pdf"),
    ("companion-gdd-technical-2026-05-23.md", "companion-gdd-technical-2026-05-23.pdf"),
]

# Palette
TEXT = HexColor("#1f1f1f")
DARK = HexColor("#0d0d0d")
MUTED = HexColor("#666666")
RULE = HexColor("#1f1f1f")
TBL_HEAD_BG = HexColor("#1f1f1f")
TBL_HEAD_FG = HexColor("#ffffff")
TBL_ROW_ALT = HexColor("#f5f5f5")
TBL_GRID = HexColor("#dddddd")
CODE_BG = HexColor("#f3f3f3")

# Styles ---------------------------------------------------------------
title_style = ParagraphStyle(
    name="Title", fontName="Helvetica-Bold",
    fontSize=22, leading=27, textColor=DARK, spaceAfter=2,
)
subtitle_style = ParagraphStyle(
    name="Subtitle", fontName="Helvetica-Oblique",
    fontSize=10, leading=13, textColor=MUTED, spaceAfter=16,
)
h2_style = ParagraphStyle(
    name="H2", fontName="Helvetica-Bold",
    fontSize=14, leading=18, textColor=DARK,
    spaceBefore=18, spaceAfter=8, keepWithNext=True,
)
h3_style = ParagraphStyle(
    name="H3", fontName="Helvetica-Bold",
    fontSize=11.5, leading=15, textColor=TEXT,
    spaceBefore=12, spaceAfter=4, keepWithNext=True,
)
body_style = ParagraphStyle(
    name="Body", fontName="Helvetica",
    fontSize=10.5, leading=15.5, textColor=TEXT,
    spaceAfter=9, alignment=TA_JUSTIFY,
)
bullet_style = ParagraphStyle(
    name="Bullet", fontName="Helvetica",
    fontSize=10.5, leading=15, textColor=TEXT,
    leftIndent=16, firstLineIndent=-10,
    spaceAfter=5, alignment=TA_LEFT,
    bulletIndent=4,
)
table_cell_style = ParagraphStyle(
    name="TableCell", fontName="Helvetica",
    fontSize=9.5, leading=13, textColor=TEXT,
    alignment=TA_LEFT,
)
table_head_style = ParagraphStyle(
    name="TableHead", fontName="Helvetica-Bold",
    fontSize=9.5, leading=13, textColor=TBL_HEAD_FG,
    alignment=TA_LEFT,
)

# Inline markdown ------------------------------------------------------
_BOLD_RE = re.compile(r"\*\*(.+?)\*\*")
_CODE_RE = re.compile(r"`([^`]+)`")
_EMPH_RE = re.compile(r"(?<!\*)\*([^*]+)\*(?!\*)")

def inline(text: str) -> str:
    out = escape(text, quote=False)
    out = _BOLD_RE.sub(r"<b>\1</b>", out)
    out = _EMPH_RE.sub(r"<i>\1</i>", out)
    out = _CODE_RE.sub(
        r'<font name="Courier" size="9.5" backColor="#f3f3f3">\1</font>',
        out,
    )
    return out

# Table parsing --------------------------------------------------------
def is_table_separator(line: str) -> bool:
    s = line.strip()
    if not s.startswith("|") or not s.endswith("|"):
        return False
    inner = s.strip("|").split("|")
    return all(re.match(r"^\s*:?-{2,}:?\s*$", c) for c in inner)

def parse_table_row(line: str) -> list[str]:
    s = line.strip().strip("|")
    return [c.strip() for c in s.split("|")]

# Block parser ---------------------------------------------------------
def parse(md: str):
    lines = md.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        if not stripped:
            i += 1
            continue

        if stripped.startswith("# "):
            yield ("h1", stripped[2:].strip())
            i += 1
            if i < len(lines) and lines[i].strip() and not lines[i].strip().startswith(("#", "-", "*", "|")):
                yield ("subtitle", lines[i].strip())
                i += 1
            continue

        if stripped.startswith("## "):
            yield ("h2", stripped[3:].strip())
            i += 1
            continue

        if stripped.startswith("### "):
            yield ("h3", stripped[4:].strip())
            i += 1
            continue

        # Table: header row + separator row + data rows
        if stripped.startswith("|") and i + 1 < len(lines) and is_table_separator(lines[i + 1]):
            header = parse_table_row(lines[i])
            i += 2
            rows = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                rows.append(parse_table_row(lines[i]))
                i += 1
            yield ("table", (header, rows))
            continue

        if stripped.startswith(("- ", "* ")):
            items = []
            while i < len(lines) and lines[i].strip().startswith(("- ", "* ")):
                items.append(lines[i].strip()[2:])
                i += 1
            yield ("ul", items)
            continue

        # Paragraph
        buf = [stripped]
        i += 1
        while i < len(lines):
            nxt = lines[i].strip()
            if not nxt or nxt.startswith(("#", "- ", "* ", "|")):
                break
            buf.append(nxt)
            i += 1
        yield ("p", " ".join(buf))

# Flowable builders ----------------------------------------------------
def make_bullet(text: str) -> Paragraph:
    """Bullet with proper hanging indent — no overlapping."""
    return Paragraph(f"<font color='#1f1f1f'>•</font>&nbsp;&nbsp;{inline(text)}", bullet_style)

def make_table(header: list[str], rows: list[list[str]]) -> Table:
    data = [[Paragraph(inline(h), table_head_style) for h in header]]
    for r in rows:
        # Pad/truncate to match header length
        while len(r) < len(header):
            r.append("")
        r = r[:len(header)]
        data.append([Paragraph(inline(c), table_cell_style) for c in r])

    col_count = len(header)
    # Distribute widths evenly across the usable area (A4 - margins ~ 170mm)
    usable = 166 * mm
    col_widths = [usable / col_count] * col_count

    t = Table(data, colWidths=col_widths, hAlign="LEFT", repeatRows=1)
    style = [
        ("BACKGROUND", (0, 0), (-1, 0), TBL_HEAD_BG),
        ("TEXTCOLOR", (0, 0), (-1, 0), TBL_HEAD_FG),
        ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
        ("ALIGN", (0, 0), (-1, -1), "LEFT"),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 6),
        ("RIGHTPADDING", (0, 0), (-1, -1), 6),
        ("TOPPADDING", (0, 0), (-1, -1), 5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
        ("GRID", (0, 0), (-1, -1), 0.4, TBL_GRID),
    ]
    # Zebra striping on body rows
    for ri in range(1, len(data)):
        if ri % 2 == 0:
            style.append(("BACKGROUND", (0, ri), (-1, ri), TBL_ROW_ALT))
    t.setStyle(TableStyle(style))
    return t

def build(md_path: Path):
    md = md_path.read_text(encoding="utf-8")
    flow = []
    for kind, payload in parse(md):
        if kind == "h1":
            flow.append(Paragraph(inline(payload), title_style))
            flow.append(HRFlowable(
                width="100%", thickness=1.2, color=RULE,
                spaceBefore=4, spaceAfter=4,
            ))
        elif kind == "subtitle":
            flow.append(Paragraph(inline(payload), subtitle_style))
        elif kind == "h2":
            flow.append(Paragraph(inline(payload), h2_style))
        elif kind == "h3":
            flow.append(Paragraph(inline(payload), h3_style))
        elif kind == "p":
            flow.append(Paragraph(inline(payload), body_style))
        elif kind == "ul":
            for item in payload:
                flow.append(make_bullet(item))
            flow.append(Spacer(1, 4))
        elif kind == "table":
            header, rows = payload
            flow.append(Spacer(1, 2))
            flow.append(make_table(header, rows))
            flow.append(Spacer(1, 8))
    return flow

def add_page_number(canvas, doc):
    canvas.saveState()
    canvas.setFont("Helvetica", 8.5)
    canvas.setFillColor(MUTED)
    canvas.drawRightString(
        A4[0] - 20 * mm,
        12 * mm,
        f"{canvas.getPageNumber()}",
    )
    canvas.restoreState()

def convert(md_path: Path, pdf_path: Path):
    flow = build(md_path)
    doc = SimpleDocTemplate(
        str(pdf_path),
        pagesize=A4,
        leftMargin=22 * mm, rightMargin=22 * mm,
        topMargin=20 * mm, bottomMargin=20 * mm,
        title=pdf_path.stem.replace("-", " ").title(),
        author="Matthew Lowe",
    )
    doc.build(flow, onFirstPage=add_page_number, onLaterPages=add_page_number)
    print(f"Wrote {pdf_path.name}")

if __name__ == "__main__":
    for md_name, pdf_name in DOCS:
        convert(HERE / md_name, HERE / pdf_name)
