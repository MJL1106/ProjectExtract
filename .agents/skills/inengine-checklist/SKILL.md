---
name: inengine-checklist
description: Invoke EVERY time you are about to tell the user to do manual work in the Unreal editor — place an actor, set a property, create a BP child class, add a component to an existing BP, edit a DataAsset's fields, populate a DataTable row, configure a level actor. Produces a tight numbered checklist with exact menu paths, property names, and verbatim values. Strips all code commentary, architecture rationale, and recap prose. Use whenever the response would otherwise contain phrases like "in the editor, do X" / "open the BP and set Y" / "place Z in the level" / "set the property to". Use even for a single-step instruction. If the work is large/repetitive (BP graph edits, >5 reference wires, bulk same-property-across-many-assets), use `inengine-prompt` instead.
---

# In-Engine Checklist Format

The user is competent in the Unreal editor. They do not want code explanations or architectural commentary when they're about to do editor work — they want the exact clicks. This skill forces that output shape.

## Output rules

1. **No preamble.** Do not write "Here's what you need to do" or "Next, open the editor and...". The first character of the response is the bold title or the first numbered step.
2. **No closing summary.** Do not recap what the steps will achieve. End with the last step, OR with at most ONE sentence that names a verification signal ("Verify: console emits `[Foo] some line`.").
3. **One action per numbered step.** If a step requires two things (set property AND save), split it.
4. **Exact menu paths.** Use the editor's actual path strings, e.g.:
   - `Content Browser → /Game/Path/To/Asset → double-click`
   - `Details panel → Category|Sub → PropertyName → set to "Value"`
   - `Place Actors panel → search "ActorName" → drag into ViewportLevel`
5. **Verbatim values** for strings, tag paths, asset paths. Use backticks or quotes.
6. **Group by asset / panel.** If the work spans multiple assets, give one numbered list per asset under a bold title.
7. **Table for bulk same-property edits.** If setting the same property on N assets, use a 2-column table (asset → value) instead of N numbered steps.

## What this skill does NOT produce

- C++ code blocks the user does not need to read
- "Why we're doing this" paragraphs
- File:line citations to source files (unless the user is editing source)
- Architecture diagrams or explanations of how the system works
- Suggested-next-steps lists (the user knows what they're doing)
- "Let me know when done" / "Ping me when ready" / "Happy to help with..." closers

## Required structure

```
**<Action title — short noun phrase>**
1. <Path/action> → <value>
2. <Path/action> → <value>
3. ...

**<Next action title>** (only if work spans different assets/panels)
1. ...
```

Optional final line: ONE sentence naming what to watch for in PIE. Skip if obvious.

## Examples

### Example 1 — single-asset edit (good)

```
**Author display variants on IA_SomeItem**
1. `Content Browser → /Game/Items/IA_SomeItem → double-click`
2. `Details → Item|Variants → DisplayName_Hot` → `"Some Item (HOT)"`
3. `Details → Item|Variants → Description_Hot` → `"Wait for it to cool."`
4. `File → Save`
```

### Example 2 — multi-asset placement (good)

```
**Add component to actor BP**
1. `Content Browser → BP_MyActor → double-click`
2. `Components panel → + Add → search "MyComponent" → click`
3. Compile + Save

**Place actor in level**
1. `Place Actors panel → search "BP_MyActor" → drag into the level`
2. Position roughly where needed
3. `File → Save Current Level`

Verify: console prints expected log line on PIE start.
```

### Example 3 — what NOT to write (bad)

```
Now we need to wire up the actor. The mechanic works by ... [paragraph of architecture commentary].

To set this up, please follow these steps:
1. Open the editor
2. ...
```

The bad version has a paragraph of architecture commentary that the user does not need to read. Strip everything before the bold action title.

## Decision: this skill or `inengine-prompt`?

| Situation | Use |
|---|---|
| ≤~5 manual editor actions, all concrete (property edits, single placements, one component add) | **inengine-checklist** (this) |
| BP graph edits (event graph, function bodies, variable bindings, delegate wires) | `inengine-prompt` |
| Same property set across >5 assets with different values | `inengine-prompt` (table-driven by an agent) |
| New asset creation that requires N reference wires the user might mis-paste | `inengine-prompt` |
| Mix of code + editor work | Code in its own block; editor work via THIS skill |

## When the user invokes this skill mid-response

If the user says "use the checklist skill" or "give me a checklist for X" mid-conversation, switch immediately. Do not finish the current prose paragraph — restart with the checklist format.

## Output to the chat after invoking

The first line of the next assistant message is the bold action title or first numbered step. Nothing before it.
