"""Generate the Companion AI briefing PDF for examiner/tutor presentation."""

from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import cm
from reportlab.lib import colors
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle,
    Preformatted, PageBreak, KeepTogether
)
from reportlab.lib.enums import TA_LEFT

OUTPUT = r"C:\Users\matth\Documents\Github\ProjectExtract\CompanionAI_Briefing.pdf"

# ---------- styles ----------
styles = getSampleStyleSheet()
body_style = ParagraphStyle(
    "Body", parent=styles["BodyText"],
    fontName="Helvetica", fontSize=10.5, leading=14, spaceAfter=6,
)
h1_style = ParagraphStyle(
    "H1", parent=styles["Heading1"],
    fontName="Helvetica-Bold", fontSize=18, leading=22,
    textColor=colors.HexColor("#1a3d5c"), spaceBefore=0, spaceAfter=10,
)
h2_style = ParagraphStyle(
    "H2", parent=styles["Heading2"],
    fontName="Helvetica-Bold", fontSize=13, leading=16,
    textColor=colors.HexColor("#1a3d5c"), spaceBefore=12, spaceAfter=6,
)
pitch_style = ParagraphStyle(
    "Pitch", parent=body_style,
    fontName="Helvetica-Oblique", fontSize=11.5, leading=16,
    textColor=colors.HexColor("#333333"),
    leftIndent=12, rightIndent=12, spaceBefore=4, spaceAfter=10,
    borderColor=colors.HexColor("#cccccc"), borderWidth=0, borderPadding=6,
    backColor=colors.HexColor("#f4f7fa"),
)
mono_style = ParagraphStyle(
    "Mono", parent=body_style,
    fontName="Courier", fontSize=9, leading=12,
)
footer_note = ParagraphStyle(
    "Footer", parent=body_style,
    fontSize=8.5, textColor=colors.HexColor("#666666"), alignment=TA_LEFT,
)

doc = SimpleDocTemplate(
    OUTPUT, pagesize=A4,
    leftMargin=1.8*cm, rightMargin=1.8*cm,
    topMargin=1.6*cm, bottomMargin=1.6*cm,
    title="Companion AI - System Briefing",
    author="ProjectExtract",
)

story = []

# ---------- title ----------
story.append(Paragraph("Companion AI &mdash; System Briefing", h1_style))
story.append(Paragraph(
    "ProjectExtract &nbsp;&bull;&nbsp; Unreal Engine 5.7 &nbsp;&bull;&nbsp; C++ / Behavior Tree",
    footer_note,
))
story.append(Spacer(1, 8))

# ---------- one-sentence pitch ----------
story.append(Paragraph(
    "An AI companion teammate that <b>perceives enemies</b>, <b>fights them in bursts</b>, "
    "<b>follows the player in formation</b>, and <b>revives the player when downed</b> &mdash; "
    "built on Unreal's standard AI stack (AIController + Behavior Tree + Blackboard + Perception).",
    pitch_style,
))

# ---------- behaviours ----------
story.append(Paragraph("What the companion does", h2_style))

behaviour_data = [
    ["Behaviour", "Trigger", "What happens"],
    ["Formation follow",
     "Default / nothing else active",
     "Walks in a slot 350 back, 200 right of the player. Sprints to catch up if distance exceeds threshold."],
    ["Combat",
     "An enemy is sighted",
     "Turns toward target, fires in 1.5 s bursts with a 0.5 s pause, reloads when empty. Inaccuracy 8&deg; at first, tightens to 1.5&deg; over 1.5 s as the companion settles."],
    ["Revive",
     "Player goes DBNO",
     "Highest priority &mdash; aborts whatever else is running, sprints to the player, holds for 4 s, restores health to 30% and starts shield regen."],
]

wrapped = [[Paragraph(str(cell), body_style) for cell in row] for row in behaviour_data]
behaviour_tbl = Table(wrapped, colWidths=[3.4*cm, 4.8*cm, 9.3*cm])
behaviour_tbl.setStyle(TableStyle([
    ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#1a3d5c")),
    ("TEXTCOLOR", (0, 0), (-1, 0), colors.white),
    ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
    ("ALIGN", (0, 0), (-1, -1), "LEFT"),
    ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ("GRID", (0, 0), (-1, -1), 0.4, colors.HexColor("#bbbbbb")),
    ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#f7f9fb")]),
    ("TOPPADDING", (0, 0), (-1, -1), 6),
    ("BOTTOMPADDING", (0, 0), (-1, -1), 6),
    ("LEFTPADDING", (0, 0), (-1, -1), 6),
    ("RIGHTPADDING", (0, 0), (-1, -1), 6),
]))
story.append(behaviour_tbl)
story.append(Spacer(1, 4))
story.append(Paragraph(
    "Enemy AI is the target dummy: simpler &mdash; finds closest non-enemy character and fires within range.",
    body_style,
))

# ---------- architecture ----------
story.append(Paragraph("Architecture (the layers)", h2_style))

arch_diagram = (
    "ACompanionAIController   &larr;  the \"brain\": perception + BT runner<br/>"
    "&nbsp;&nbsp;&nbsp;&nbsp;|<br/>"
    "&nbsp;&nbsp;&nbsp;&nbsp;+-- AIPerceptionComponent  (sight 30 m, hearing 20 m)<br/>"
    "&nbsp;&nbsp;&nbsp;&nbsp;+-- BlackboardComponent    (PlayerActor, CombatTarget, PlayerNeedsRevive, ...)<br/>"
    "&nbsp;&nbsp;&nbsp;&nbsp;+-- Runs &rarr; BT_Companion (the behavior tree asset)<br/>"
    "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;|<br/>"
    "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;+-- Service:  UpdateCompanionState   (0.25 s heartbeat)<br/>"
    "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;+-- Decorators: CombatTarget is Set, PlayerNeedsRevive<br/>"
    "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;+-- Tasks:  FollowPlayer, CompanionCombat, RevivePlayer, MoveToCover"
)
story.append(Paragraph(arch_diagram, mono_style))
story.append(Spacer(1, 6))

story.append(Paragraph("<b>Four roles to remember:</b>", body_style))
roles = [
    "<b>Service</b> &mdash; heartbeat that updates shared state (\"who should I fight right now?\").",
    "<b>Decorator</b> &mdash; conditional gate on a branch (\"only run this if CombatTarget is set\").",
    "<b>Task</b> &mdash; the actual action (\"fire a burst\", \"revive the player\").",
    "<b>Blackboard</b> &mdash; shared whiteboard the service writes and tasks read.",
]
for r in roles:
    story.append(Paragraph("&bull; " + r, body_style))

# ---------- design decisions ----------
story.append(Paragraph("Key design decisions", h2_style))

decisions = [
    ("Gameplay Tags for identity, not class checks.",
     "Enemies carry <b>Character.Enemy</b>, companion has <b>Character.Companion</b>. "
     "The target filter just asks \"do you have the enemy tag?\" &mdash; new enemy types don't need new code."),
    ("Data-driven tuning.",
     "Ranges, fire rates, inaccuracy, revive duration are all <b>UPROPERTY(EditAnywhere)</b> &mdash; "
     "tuned in Blueprint, not recompiled."),
    ("Decoupled body rotation from weapon aim.",
     "The body rotates on yaw only (capsules stay upright). The weapon traces directly from muzzle to target "
     "location. When a skeletal mesh replaces the cylinder, an aim-offset anim can tilt the arms up/down "
     "with no code change."),
    ("Authority model.",
     "Gameplay state (health, ammo, damage, revive) happens on the server via <b>HasAuthority()</b> checks and "
     "replicates to clients. Multiplayer-ready while testing in PIE."),
    ("Composition over inheritance.",
     "Health is a Component bolted onto both player and companion &mdash; same damage/death/regen logic, "
     "no duplication."),
]
for title, body in decisions:
    story.append(Paragraph(f"<b>{title}</b>  {body}", body_style))

story.append(PageBreak())

# ---------- flow walkthrough ----------
story.append(Paragraph("Combat flow (end-to-end walk-through)", h2_style))

steps = [
    ("Enemy spawns.",
     "Its constructor adds the <b>Character.Enemy</b> gameplay tag."),
    ("Companion perceives the enemy.",
     "AIPerception catches it within 30 m sight and a 90&deg; cone."),
    ("<b>BTService_UpdateCompanionState</b> ticks every 0.25 s.",
     "Iterates perceived actors &rarr; filters by tag &rarr; picks closest alive one "
     "&rarr; writes it to the <b>CombatTarget</b> blackboard key."),
    ("Blackboard decorator fires.",
     "\"<b>CombatTarget is Set</b>\" activates the combat branch and aborts lower-priority "
     "branches (so follow is interrupted)."),
    ("<b>BTTask_CompanionCombat</b> takes over.",
     "Checks distance and line-of-sight, rotates body yaw toward the target (smoothly "
     "interpolated), calls <b>StartWeaponFire()</b> in bursts."),
    ("<b>AWeaponBase::PerformHitscan</b> runs.",
     "Detects AI owner, looks up the companion's aim target, traces from muzzle &rarr; target "
     "location with a small random spread, applies damage."),
    ("Enemy dies &rarr; service clears CombatTarget.",
     "Decorator drops the branch, BT falls back to follow-player."),
    ("Player goes DBNO mid-fight &rarr; revive branch.",
     "<b>PlayerNeedsRevive</b> flips true, the revive decorator <i>aborts both</i> "
     "(even combat), companion sprints to revive, holds 4 s, restores health, restarts shield regen."),
]
for i, (title, body) in enumerate(steps, start=1):
    story.append(Paragraph(f"<b>{i}.</b> {title}  {body}", body_style))

# ---------- status ----------
story.append(Paragraph("Working today", h2_style))
working = [
    "Perception, target acquisition, gameplay-tag filtering",
    "Combat: rotate, burst-fire, reload, inaccuracy settling",
    "Weapon aim: muzzle &rarr; target location (handles ramps, height differences)",
    "Revive: DBNO detection &rarr; sprint &rarr; hold &rarr; restore &rarr; shield regen resumes",
    "Branch priorities: revive interrupts combat, combat interrupts follow",
    "Diagnostic log category LogCompanionAI + debug-draw lines (green / red / yellow)",
]
for w in working:
    story.append(Paragraph("&#10003; " + w, body_style))

# ---------- trade-offs ----------
story.append(Paragraph("Known trade-offs / next steps", h2_style))
story.append(Paragraph(
    "Showing awareness of what isn't done and why:",
    body_style,
))
todos = [
    ("Cover is disabled.",
     "<b>MoveToCover</b> task works, but the current BT uses a Sequence instead of a Simple Parallel, "
     "so combat holds the branch and cover never runs alongside. Fix: swap to Simple Parallel."),
    ("No move-to-engage.",
     "If the enemy is sighted but out of range, the combat task fails instead of closing distance. "
     "Trivial fix &mdash; call <b>MoveToActor</b> in the out-of-range branch."),
    ("LOS trace doesn't ignore the player.",
     "The player's body can block the companion's line of fire."),
    ("Weapon has no muzzle socket.",
     "Shots originate from the weapon root. Cosmetic &mdash; fixed by giving the assault rifle a "
     "skeletal mesh with a <b>Muzzle</b> socket."),
]
for title, body in todos:
    story.append(Paragraph(f"&bull; <b>{title}</b>  {body}", body_style))

# ---------- learnings ----------
story.append(Paragraph("What I learned", h2_style))
lessons = [
    "How UE5's BT + Blackboard + Perception compose cleanly &mdash; each piece has one job.",
    "How to diagnose AI behaviour by instrumenting decision gates (<b>bDebugLogging</b> uproperty + "
    "the built-in BT debugger tool).",
    "When to use <b>Sequence</b> vs <b>Selector</b> vs <b>Simple Parallel</b> "
    "(the cover bug was a concrete lesson).",
    "How to decouple character rotation from weapon aim so gameplay code is future-proof "
    "against rigged meshes.",
]
for l in lessons:
    story.append(Paragraph("&bull; " + l, body_style))

# ---------- build ----------
doc.build(story)
print(f"PDF written: {OUTPUT}")
