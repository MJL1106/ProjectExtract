"""Generate eqs_followslot_overview.pdf — high-level overview of the FollowSlot EQS."""

from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, KeepTogether
)
from reportlab.platypus.flowables import HRFlowable

OUT = "eqs_followslot_overview.pdf"

ACCENT = colors.HexColor("#1f6feb")
OK     = colors.HexColor("#1a7f37")
MUTED  = colors.HexColor("#57606a")
PANEL  = colors.HexColor("#f6f8fa")
BORDER = colors.HexColor("#d0d7de")
TEXT   = colors.HexColor("#1f2328")

doc = SimpleDocTemplate(
    OUT, pagesize=A4,
    leftMargin=22 * mm, rightMargin=22 * mm,
    topMargin=20 * mm, bottomMargin=20 * mm,
    title="Companion EQS — FollowSlot",
    author="ProjectExtract / AI-Companion-Prototype",
)

base = getSampleStyleSheet()

styles = {
    "title": ParagraphStyle(
        "title", parent=base["Title"], fontName="Helvetica-Bold",
        fontSize=22, leading=26, textColor=TEXT, spaceAfter=2,
    ),
    "subtitle": ParagraphStyle(
        "subtitle", parent=base["Normal"], fontName="Helvetica",
        fontSize=11, leading=14, textColor=MUTED, spaceAfter=14,
    ),
    "h2": ParagraphStyle(
        "h2", parent=base["Heading2"], fontName="Helvetica-Bold",
        fontSize=13, leading=16, textColor=TEXT,
        spaceBefore=14, spaceAfter=6,
    ),
    "body": ParagraphStyle(
        "body", parent=base["Normal"], fontName="Helvetica",
        fontSize=10.5, leading=15, textColor=TEXT, spaceAfter=6,
    ),
    "bullet": ParagraphStyle(
        "bullet", parent=base["Normal"], fontName="Helvetica",
        fontSize=10.5, leading=14, textColor=TEXT,
        leftIndent=14, bulletIndent=2, spaceAfter=3,
    ),
    "mono": ParagraphStyle(
        "mono", parent=base["Normal"], fontName="Courier",
        fontSize=9.5, leading=12, textColor=TEXT,
    ),
}

def para(text, style="body"):
    return Paragraph(text, styles[style])

def bullet(text):
    return Paragraph(f"&bull;&nbsp;&nbsp;{text}", styles["bullet"])

story = []

story.append(para("Companion EQS &mdash; FollowSlot", "title"))
story.append(para("High-level overview &middot; AI-Companion-Prototype branch", "subtitle"))

status_data = [[
    Paragraph("<b>Status</b>", styles["body"]),
    Paragraph('<font color="#1a7f37"><b>WORKING</b></font>', styles["body"]),
    Paragraph("<b>Asset</b>", styles["body"]),
    Paragraph('<font name="Courier">EQS_FollowSlot.uasset</font>', styles["body"]),
]]
status_tbl = Table(status_data, colWidths=[22*mm, 32*mm, 22*mm, None])
status_tbl.setStyle(TableStyle([
    ("BACKGROUND", (0, 0), (-1, -1), PANEL),
    ("BOX", (0, 0), (-1, -1), 0.5, BORDER),
    ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
    ("LEFTPADDING", (0, 0), (-1, -1), 10),
    ("RIGHTPADDING", (0, 0), (-1, -1), 10),
    ("TOPPADDING", (0, 0), (-1, -1), 8),
    ("BOTTOMPADDING", (0, 0), (-1, -1), 8),
]))
story.append(status_tbl)
story.append(Spacer(1, 10))

story.append(para("What it does", "h2"))
story.append(para(
    "Picks where the companion should stand relative to the player while following. "
    "Every half-second, the BT task asks the EQS &quot;given the player, what&rsquo;s "
    "the best slot to occupy right now?&quot; and the EQS returns one location. The "
    "companion walks to that slot instead of a hardcoded rear-right formation offset."
))

story.append(para("How it runs", "h2"))
for b in [
    "Assigned on the <i>Follow Player</i> BT node, under the <b>EQS</b> category.",
    "Queries every 0.5&thinsp;s (designer-tunable), throttled so only one is in flight at a time.",
    "Only fires while the companion is actively moving toward the player &mdash; idles when close.",
    "Receives the player actor as its anchor via <font name=\"Courier\">EnvQueryContext_Player</font>.",
    "Runs in <b>SingleResult</b> mode &mdash; returns one winning location, not a ranked list.",
]:
    story.append(bullet(b))

story.append(para("How it decides the best slot", "h2"))
story.append(para(
    "The query asset (designer-authored) runs three stages on candidate points around the player:"
))
for b in [
    "<b>Generator</b> &mdash; spits out candidate positions (typically a donut around the player at formation radius).",
    "<b>Filters</b> &mdash; hard reject points: must be on the navmesh, must not overlap actors, etc.",
    "<b>Scorers</b> &mdash; soft weights for distance-to-player, line-of-sight, distance-from-self (anti-oscillation), and optionally &lsquo;behind player&rsquo;s movement direction&rsquo;.",
]:
    story.append(bullet(b))
story.append(para(
    "Each surviving candidate gets a total score from the weighted scorers. The highest "
    "score wins. Tuning the weights changes personality: heavy LOS = always visible to player; "
    "heavy distance-from-self = sticky slot, no ping-ponging; heavy behind-velocity = covers the player&rsquo;s six."
))

story.append(para("Fallback (why it can&rsquo;t fully break)", "h2"))
story.append(para(
    "If the query asset is unset, in-flight, or returns no items, the task falls back to C++ "
    "formation math &mdash; rear-right offset of the player&rsquo;s velocity vector. So a "
    "mistuned or missing EQS degrades into &ldquo;decent fixed formation&rdquo; rather than "
    "broken follow behaviour."
))

story.append(para("Tuning surface", "h2"))
for b in [
    "<b>C++ side</b> &mdash; <font name=\"Courier\">FollowSlotQuery</font> (which asset) and <font name=\"Courier\">EqsQueryInterval</font> (how often) on the BT node.",
    "<b>Asset side</b> &mdash; generator radius, filter strictness, scorer weights, all editable in the EQS editor.",
    "<b>Anchor side</b> &mdash; <font name=\"Courier\">EnvQueryContext_Player</font> reads the player from the blackboard; no tuning needed.",
]:
    story.append(bullet(b))

story.append(Spacer(1, 6))
story.append(HRFlowable(width="100%", thickness=0.4, color=BORDER))
story.append(Spacer(1, 4))
story.append(Paragraph(
    '<font color="#57606a" size="9">Generated for the AI-Companion-Prototype branch &middot; throwaway reference</font>',
    styles["body"],
))

doc.build(story)
print(f"Wrote {OUT}")
