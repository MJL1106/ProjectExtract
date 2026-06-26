#!/bin/sh
# roadmap-update.sh — auto-reconcile agent_docs/project_roadmap.md after a commit.
#
# Invoked from .git/hooks/post-commit (wired by .githooks/install.sh). All
# reasoning is driven by the COMMIT itself (message + changed files + diff), not
# by any chat session — so it works no matter which chat/session/tool made the
# commit, including a manual terminal commit or one chat committing another
# chat's work.
#
# HARD RULE: never block or break a commit. Every failure path exits 0 and
# leaves the commit untouched; worst case it simply doesn't update this time.

MARKER='[roadmap-skip]'
CHECKLIST='agent_docs/project_roadmap.md'
SENTINEL='ROADMAP-CHECKLIST'

ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || exit 0
cd "$ROOT" 2>/dev/null || exit 0

# 1. Don't recurse on our own auto-update commit.
case "$(git log -1 --format=%s 2>/dev/null)" in
  *"$MARKER"*) exit 0 ;;
esac

# 2. Need the checklist file and the Claude CLI; otherwise quietly do nothing.
[ -f "$CHECKLIST" ] || exit 0
command -v claude >/dev/null 2>&1 || exit 0

# 3. Single-flight lock so overlapping commits can't stack runs.
LOCK="$ROOT/.git/roadmap-hook.lock"
[ -e "$LOCK" ] && exit 0
( echo $$ > "$LOCK" ) 2>/dev/null
trap 'rm -f "$LOCK"' EXIT INT TERM

printf >&2 '%s\n' "roadmap: reconciling checklist from this commit (a few seconds; will not affect the commit)..."

# 4. Evidence from the latest commit — message + file stats + truncated diff,
#    excluding the checklist itself so the model reasons about real work.
EXCL=":(exclude)$CHECKLIST"
EVIDENCE="$(git show -1 --stat --format='Commit %h: %s%n%n%b' -- . "$EXCL" 2>/dev/null)"
DIFF="$(git show -1 --format='' -- . "$EXCL" 2>/dev/null | head -c 50000)"
CURRENT="$(cat "$CHECKLIST" 2>/dev/null)"

PROMPT="You maintain a markdown build-progress checklist for a UE5 game (ProjectExtract).
Reconcile ONLY the checkbox statuses against evidence from the latest git commit.

Status markers: '- [ ]' to-do, '- [~]' in progress, '- [x]' done.
Rules:
- Flip a checkbox ONLY when the commit clearly supports it. Be conservative: prefer [~] over [x] for partial work. Never downgrade an existing [x] unless the commit clearly reverts that feature.
- Do NOT change any wording, headings, ordering, week tags, or the sentinel comment. Change nothing except the checkbox characters.
- Output the ENTIRE file verbatim with only those checkbox edits. No code fences, no commentary, nothing before or after the file.

===== CURRENT FILE =====
$CURRENT

===== COMMIT (stat + message) =====
$EVIDENCE

===== COMMIT DIFF (truncated) =====
$DIFF"

OUT="$ROOT/.git/roadmap-hook.out"
if command -v timeout >/dev/null 2>&1; then
  timeout 120 claude -p "$PROMPT" --model sonnet >"$OUT" 2>/dev/null
else
  claude -p "$PROMPT" --model sonnet >"$OUT" 2>/dev/null
fi

# 5. Strip any stray code fences, then validate before trusting the output.
sed -i '/^```/d' "$OUT" 2>/dev/null
[ -s "$OUT" ] || { rm -f "$OUT"; exit 0; }
grep -q "$SENTINEL" "$OUT" 2>/dev/null || { rm -f "$OUT"; exit 0; }

# 6. No change -> nothing to do.
if cmp -s "$OUT" "$CHECKLIST"; then printf >&2 '%s\n' "roadmap: no status changes from this commit."; rm -f "$OUT"; exit 0; fi

# Dry-run (ROADMAP_DRYRUN=1): show what WOULD change; touch nothing, commit nothing.
if [ -n "$ROADMAP_DRYRUN" ]; then
  printf >&2 '%s\n' "roadmap (dry-run): would apply these checklist changes:"
  diff "$CHECKLIST" "$OUT" >&2 2>/dev/null
  rm -f "$OUT"; exit 0
fi

# 7. Apply, and record as its own tidy commit that rides along in your next push.
mv "$OUT" "$CHECKLIST" 2>/dev/null || { rm -f "$OUT"; exit 0; }
git add -f "$CHECKLIST" 2>/dev/null
git commit --no-verify -m "chore(roadmap): sync checklist from commit $MARKER" >/dev/null 2>&1
printf >&2 '%s\n' "roadmap: checklist updated + committed (rides along in your next push)."

exit 0
