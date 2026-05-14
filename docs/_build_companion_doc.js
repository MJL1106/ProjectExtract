// Builds docs/companion_movement_plan.docx from the same content as the .md
// Run: NODE_PATH=$(npm root -g) node docs/_build_companion_doc.js
const {
  Document, Packer, Paragraph, TextRun, AlignmentType, HeadingLevel,
  LevelFormat, BorderStyle
} = require('docx');
const fs = require('fs');
const path = require('path');

const FONT = 'Arial';

const run = (text, extra = {}) => new TextRun({ text, font: FONT, size: 22, ...extra });

const p = (text, opts = {}) => new Paragraph({
  spacing: { after: 120, line: 280 },
  ...opts,
  children: (Array.isArray(text) ? text : [run(text)])
});

const body = (runs, opts = {}) => new Paragraph({
  spacing: { after: 120, line: 280 },
  ...opts,
  children: runs
});

const h1 = (text) => new Paragraph({
  heading: HeadingLevel.HEADING_1,
  spacing: { before: 0, after: 200 },
  children: [new TextRun({ text, font: FONT, size: 36, bold: true })]
});

const subtitle = (text) => new Paragraph({
  spacing: { before: 0, after: 200 },
  children: [new TextRun({ text, font: FONT, size: 22, italics: true, color: '555555' })]
});

const h2 = (text) => new Paragraph({
  heading: HeadingLevel.HEADING_2,
  spacing: { before: 260, after: 120 },
  children: [new TextRun({ text, font: FONT, size: 28, bold: true })]
});

const h3 = (text) => new Paragraph({
  heading: HeadingLevel.HEADING_3,
  spacing: { before: 180, after: 80 },
  children: [new TextRun({ text, font: FONT, size: 24, bold: true })]
});

const bullet = (runs) => new Paragraph({
  numbering: { reference: 'bullets', level: 0 },
  spacing: { after: 60, line: 280 },
  children: Array.isArray(runs) ? runs : [run(runs)]
});

const hr = () => new Paragraph({
  spacing: { before: 80, after: 160 },
  border: { bottom: { style: BorderStyle.SINGLE, size: 6, color: '999999', space: 1 } },
  children: [new TextRun({ text: '', font: FONT, size: 2 })]
});

// --- content --------------------------------------------------------------

const children = [];

children.push(h1('AI Companion — Movement and Next Steps'));
children.push(subtitle('By Matthew Lowe'));
children.push(hr());

children.push(h2('Purpose'));
children.push(p(
  "The AI companion prototype is now functional end-to-end: it follows the player, engages enemies, and revives the player when downed. This document outlines the prototype's current state, the issues that have surfaced through development and informal playtesting, and three directions the next phase of work could take. The aim is to discuss the direction before I commit to a detailed build plan."
));

// §1
children.push(h2('1. Where the Companion Stands'));

children.push(h3('1.1 What it does'));
children.push(bullet([
  run('Formation follow. ', { bold: true }),
  run('The companion holds a configurable offset behind and to the right of the player, predicts where the player is heading from their velocity, and sprints to catch up if it falls behind a threshold distance. It has idle hysteresis so it does not jitter when the player rotates in place.')
]));
children.push(bullet([
  run('Combat engagement. ', { bold: true }),
  run('It detects enemies via sight and hearing, picks the closest, fires in short bursts with inaccuracy that settles the longer it stays on the same target — giving a believable ramp-on-target feel rather than instant headshots. It reloads when its magazine empties and resumes.')
]));
children.push(bullet([
  run('Revive. ', { bold: true }),
  run('When the player goes down, the companion drops what it is doing, sprints to the player, holds proximity for four seconds, and revives. The task handles awkward edge cases — revived by someone else mid-task, bleedout expiry, path blocked, player moves out of range.')
]));
children.push(bullet([
  run('Shared traversal. ', { bold: true }),
  run('Vault, climb, and mantle logic now lives in a shared component used by both the player and the companion. When the player traverses an obstacle, the companion mirrors the traversal on the same wall.')
]));
children.push(bullet([
  run('Designer tuning. ', { bold: true }),
  run('Most knobs (formation distances, sprint threshold, mirror trigger range, recovery timeouts) live in a Data Asset rather than C++, so changes do not require a rebuild.')
]));

children.push(h3('1.2 Recent progress'));
children.push(bullet('Traversal logic now lives in a shared component, so any future improvement to player traversal automatically benefits the companion.'));
children.push(bullet('The sprint-state latching bug — where the companion would catch up but stay stuck in sprint animation forever — is fixed, along with the construction-order trap that caused it.'));
children.push(bullet("Weapon fire now traces from muzzle to target instead of along the companion's forward vector, eliminating misses caused by height differences between the companion and its target."));
children.push(bullet('A robust revive task with complete edge-case handling has landed.'));

children.push(h3('1.3 Outstanding issues, grouped by theme'));
children.push(p('The remaining problems sit cleanly in three themes, which map directly to the three directions in §2.'));
children.push(body([
  run('Theme A — Movement and traversal fidelity. ', { bold: true }),
  run('Mirror traversal still fails at certain wall positions, likely a wall-edge / collision-shape interaction. The companion approaches obstacles at an angle rather than head-on, so traces against the wall start from suboptimal positions. Locomotion animation is forward-only and tightly coupled to specific speed values, so tweaking the speeds breaks the visuals. There is no drop-down behaviour for getting off ledges.')
]));
children.push(body([
  run('Theme B — Tactical depth. ', { bold: true }),
  run('The companion has a single combat pattern: get in range and shoot. There is no suppressive fire, no flanking, and no fall-back when low on health. Follow behaviour does not respect threat — the companion will pull out of position to maintain formation while the player is being shot at. There is no architectural slot for a second companion or any form of squad coordination.')
]));
children.push(body([
  run('Theme C — Player agency and robustness. ', { bold: true }),
  run('The companion is fully autonomous — the player cannot direct it, hold it back, or point it at a specific target. There are no automation tests; the only safety net is a written set of manual test scenarios. No performance profiling pass has been done.')
]));

// §2
children.push(h2('2. Three Directions for the Next Phase'));

// Direction A
children.push(h3('Direction A — Stabilise and Polish'));
children.push(body([
  run('Frame. ', { bold: true }),
  run('Focus on stabilising the existing systems before extending them. The traversal and animation issues will continue to surface in any feature work layered on top, and become more expensive to debug once buried under additional behaviour.')
]));
children.push(p('Concrete work:', { spacing: { after: 60 } }));
children.push(bullet('Fix the remaining traversal clearance failures. A one-line approach-angle fix is already identified, and a focused debug session with existing trace visualisation should resolve the rest.'));
children.push(bullet("Audit the companion's collision shape against the player's to rule out a size mismatch as the source of the wall-edge failures."));
children.push(bullet('Add strafe and back-step locomotion poses; decouple the animation blendspace from raw speed values so future tuning does not break visuals.'));
children.push(bullet('A first pass of automation tests, mirroring the manual scenarios that already exist in writing.'));
children.push(body([
  run('What it offers: ', { bold: true }),
  run('Confidence the companion behaves consistently across machines and after refactors. Removes the class of "works in editor, fails in playtest" surprises that erode trust in the prototype.')
]));
children.push(body([
  run('What it does not offer: ', { bold: true }),
  run('Any new gameplay surface. The companion will appear the same to anyone watching — there are simply fewer bugs, not new features.')
]));

// Direction B
children.push(h3('Direction B — Behavioural Depth'));
children.push(body([
  run('Frame. ', { bold: true }),
  run("Extend the companion's combat behaviour beyond a single engagement pattern. Today the companion is a competent autonomous actor; the next step is making its decisions visibly tactical.")
]));
children.push(p('Concrete work:', { spacing: { after: 60 } }));
children.push(bullet('Multiple engagement patterns — suppress, flank, push — chosen based on enemy count and the layout of the environment.'));
children.push(bullet('Threat-aware follow: drop into a combat posture and prioritise positioning when the player is taking fire, even between engagements.'));
children.push(bullet('Architecture pass for a second companion, including role assignment and arbitration over who responds when both companions could act (for example, who revives the player).'));
children.push(body([
  run('What it offers: ', { bold: true }),
  run('The companion becomes a more distinct design element. A player can tell the difference between "follower with a gun" and "AI that makes tactical choices" — the second is much more memorable.')
]));
children.push(body([
  run('What it does not offer: ', { bold: true }),
  run('Any improvement to the underlying movement, traversal, or animation issues. A more tactical companion that still trips over walls reads worse than a simpler companion that moves cleanly.')
]));

// Direction C
children.push(h3('Direction C — Player Agency (Command System)'));
children.push(body([
  run('Frame. ', { bold: true }),
  run('Let the player drive the companion rather than the companion guessing. A control surface turns the companion from a passive sidekick into a resource the player engages with deliberately.')
]));
children.push(p('Concrete work:', { spacing: { after: 60 } }));
children.push(bullet('A command ping — hold a key, point at the world, issue an order: "go here", "hold this position", "focus that target", "revive me".'));
children.push(bullet("Internal state values for player intent; the companion's behaviour tree gains a top-level branch that respects commands and overrides the autonomous logic."));
children.push(bullet('A minimal UI affordance, such as an indicator on the world and a command icon on the heads-up display.'));
children.push(bullet('A clear hand-back rule — when an order completes or is overridden by a higher-priority event (player goes down), the companion returns to autonomous mode.'));
children.push(body([
  run('What it offers: ', { bold: true }),
  run('The largest gameplay-loop change of the three. The companion becomes something the player commands, not just something that exists alongside them — a change in how the game is '),
  run('played', { italics: true }),
  run(', not just how it '),
  run('runs', { italics: true }),
  run('.')
]));
children.push(body([
  run('What it does not offer: ', { bold: true }),
  run('Any improvement to the underlying behaviour quality. A weak follower with commands is still a weak follower; the command system rides on top of the existing AI rather than replacing it.')
]));

// §3
children.push(h2('3. Recommendation and Open Questions'));

children.push(h3('3.1 Recommended order'));
children.push(body([
  run('I would prioritise '),
  run('Direction A first.', { bold: true }),
  run(' Without it, B and C are built on shifting sand. The traversal and locomotion issues will surface in any feature work — every new behaviour layered on top inherits them, and every new bug report needs to be triaged against the existing instability before the new feature itself can be addressed.')
]));
children.push(body([
  run('Then Direction C. ', { bold: true }),
  run("A bigger perceived impact per hour of work than B. The command system also acts as a multiplier on B's later behaviours, because the player can see the companion respond to an order — the depth becomes visible rather than ambient.")
]));
children.push(body([
  run('Direction B last. ', { bold: true }),
  run('Behavioural depth and squad-level coordination is the most expensive of the three and depends on a stable, controllable solo companion. Bringing it forward risks doubling down on systems that have not yet been validated.')
]));

children.push(h3('3.2 Open questions'));
children.push(p('These are the points where guidance would most help the direction:'));
children.push(bullet([
  run('Visible feature progress this term, or invisible stability progress? ', { bold: true }),
  run('If a demoable feature is the priority for the next review point, Direction C ahead of A is defensible — the command system gives a clear talking point. If the priority is reliability ahead of a wider playtest, A is the right order.')
]));
children.push(bullet([
  run('Is a second companion or squad in scope for the project at all? ', { bold: true }),
  run("If yes, Direction B's architecture work should start earlier so the design does not commit to single-AI assumptions. If no, B drops down the list and Direction C absorbs more of the cycle.")
]));

children.push(h3('3.3 Open risks'));
children.push(bullet([
  run('Traversal mirroring may need a deeper rework. ', { bold: true }),
  run("If the clearance failures turn out to be collision-shape-based rather than trace-based, Direction A's scope grows. This is a known unknown; the first focused debug session should give a confident answer.")
]));
children.push(bullet([
  run('Animation polish is gated on art availability. ', { bold: true }),
  run("Direction A's strafe and back-step poses depend on animation assets, not just on engineering time. If that resource is not available, the visual half of Direction A defers without blocking the rest.")
]));
children.push(bullet([
  run('The command system needs design input before code. ', { bold: true }),
  run('Direction C should not start with engineering — it should start with a brief design pass on what commands exist, how they are issued, and how the world communicates them back.')
]));

// --- document -------------------------------------------------------------

const doc = new Document({
  creator: 'Matthew Lowe',
  title: 'AI Companion — Movement and Next Steps',
  styles: {
    default: { document: { run: { font: FONT, size: 22 } } },
    paragraphStyles: [
      { id: 'Heading1', name: 'Heading 1', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { size: 36, bold: true, font: FONT },
        paragraph: { spacing: { before: 0, after: 200 }, outlineLevel: 0 } },
      { id: 'Heading2', name: 'Heading 2', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { size: 28, bold: true, font: FONT },
        paragraph: { spacing: { before: 260, after: 120 }, outlineLevel: 1 } },
      { id: 'Heading3', name: 'Heading 3', basedOn: 'Normal', next: 'Normal', quickFormat: true,
        run: { size: 24, bold: true, font: FONT },
        paragraph: { spacing: { before: 180, after: 80 }, outlineLevel: 2 } }
    ]
  },
  numbering: {
    config: [
      { reference: 'bullets',
        levels: [{ level: 0, format: LevelFormat.BULLET, text: '•', alignment: AlignmentType.LEFT,
          style: { paragraph: { indent: { left: 540, hanging: 270 } } } }]
      }
    ]
  },
  sections: [{
    properties: {
      page: {
        size: { width: 12240, height: 15840 },
        margin: { top: 1080, right: 1080, bottom: 1080, left: 1080 }
      }
    },
    children
  }]
});

Packer.toBuffer(doc).then((buffer) => {
  const out = path.join(__dirname, 'companion_movement_plan.docx');
  fs.writeFileSync(out, buffer);
  console.log('OK:', out);
});
