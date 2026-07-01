#!/bin/sh
# install.sh — wire the roadmap auto-update into this clone's git hooks.
#
# Idempotent and non-destructive: it APPENDS a guarded call to the existing
# .git/hooks/post-commit (preserving Git LFS / code-review-graph hooks) rather
# than replacing anything. Safe to re-run if those tools ever regenerate hooks.
#
# Run once per clone:   sh .githooks/install.sh

ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || { echo "not inside a git repo"; exit 1; }
HOOK="$ROOT/.git/hooks/post-commit"
MARK='# >>> roadmap-checklist auto-update >>>'

mkdir -p "$ROOT/.git/hooks"

# Ensure the hook exists with a shebang (don't clobber an existing one).
if [ ! -f "$HOOK" ]; then
  printf '%s\n' '#!/bin/sh' > "$HOOK"
fi

if grep -qF "$MARK" "$HOOK" 2>/dev/null; then
  chmod +x "$HOOK" 2>/dev/null
  echo "roadmap hook already installed -> $HOOK"
  exit 0
fi

cat >> "$HOOK" <<'EOF'

# >>> roadmap-checklist auto-update >>>
# Reconciles agent_docs/project_roadmap.md from each commit. See .githooks/roadmap-update.sh.
__rm_root="$(git rev-parse --show-toplevel 2>/dev/null)"
[ -n "$__rm_root" ] && [ -f "$__rm_root/.githooks/roadmap-update.sh" ] && sh "$__rm_root/.githooks/roadmap-update.sh" || true
# <<< roadmap-checklist auto-update <<<
EOF

chmod +x "$HOOK" 2>/dev/null
echo "installed roadmap hook -> $HOOK"
