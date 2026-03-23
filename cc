#!/usr/bin/env bash
# cc — Launch Claude Code with a per-repo Discord bot
#
# Each repo gets its own bot token and channel. This script overrides HOME
# per session so the Discord plugin sees an isolated config instead of the
# global one, enabling concurrent sessions across multiple repos.
#
# Prerequisites per repo:
#   .claude/channels/discord/.env         — DISCORD_BOT_TOKEN=...
#   .claude/channels/discord/access.json  — channel access rules (see README)
#
# Usage: cc [-y] [-- claude-args...]
#   -y   pass --permission-mode bypassPermissions to claude (auto-approve tools)
#
# First run / after crashes:
#   pkill -f 'bun server.ts'   — kill stale bot processes from previous sessions

set -euo pipefail
shopt -s nullglob

REAL_HOME="$HOME"
export BUN_INSTALL="$REAL_HOME/.bun"
export PATH="$BUN_INSTALL/bin:$PATH"

REPO_ENV=".claude/channels/discord/.env"
REPO_ACCESS=".claude/channels/discord/access.json"
REAL_DISCORD="$REAL_HOME/.claude/channels/discord"

# --- Flags ---

BYPASS_PERMS=false
if [[ "${1:-}" == "-y" ]]; then
  BYPASS_PERMS=true
  shift
fi

# --- Preflight ---

# Warn if stale discord server.ts processes exist — they hold bot connections
# from previous sessions. Symptoms: multiple bots appear online, wrong-channel
# typing indicators. Fix: pkill -f 'bun server.ts'
STALE=$(pgrep -f 'bun server.ts' 2>/dev/null | tr '\n' ' ' || true)
if [[ -n "$STALE" ]]; then
  echo "warning: stale discord server.ts process(es) detected: $STALE" >&2
  echo "         run: pkill -f 'bun server.ts'  to clear before starting" >&2
fi

if [[ ! -f "$REPO_ENV" ]]; then
  echo "error: no .claude/channels/discord/.env in this repo" >&2
  exit 1
fi

if ! grep -q '^DISCORD_BOT_TOKEN=.' "$REPO_ENV"; then
  echo "error: $REPO_ENV missing or empty DISCORD_BOT_TOKEN" >&2
  exit 1
fi

if [[ ! -f "$REPO_ACCESS" ]]; then
  echo "error: no .claude/channels/discord/access.json in this repo" >&2
  exit 1
fi

if ! python3 -c "import json; json.load(open('$REPO_ACCESS'))" 2>/dev/null; then
  echo "error: $REPO_ACCESS is not valid JSON" >&2
  exit 1
fi

python3 -c "
import json, sys
a = json.load(open('$REPO_ACCESS'))
warn = lambda msg: print('warning: ' + msg, file=sys.stderr)
if a.get('dmPolicy') == 'disabled':
    warn('access.json dmPolicy is disabled — all DMs will be dropped')
if not a.get('groups'):
    warn('access.json has no channel groups — bot will ignore all channel messages')
" || true

# --- Build fake HOME ---

FAKE_HOME=$(mktemp -d "/tmp/cc-XXXXXX")
FAKE_DISCORD="$FAKE_HOME/.claude/channels/discord"

cleanup() {
  shred -u "$FAKE_DISCORD/.env" 2>/dev/null || rm -f "$FAKE_DISCORD/.env"
  rm -rf "$FAKE_HOME"
}
trap cleanup EXIT INT TERM HUP

# Level 1: HOME except .claude
for item in "$REAL_HOME"/.* "$REAL_HOME"/*; do
  name=$(basename "$item")
  [[ "$name" == "." || "$name" == ".." || "$name" == ".claude" || "$name" == ".local" ]] && continue
  ln -sf "$item" "$FAKE_HOME/$name"
done

# Level 1.5: .local — create as real directory, symlink contents
mkdir -p "$FAKE_HOME/.local"
for item in "$REAL_HOME/.local"/.* "$REAL_HOME/.local"/*; do
  name=$(basename "$item")
  [[ "$name" == "." || "$name" == ".." ]] && continue
  ln -sf "$item" "$FAKE_HOME/.local/$name"
done

# Level 2: .claude except channels
mkdir -p "$FAKE_HOME/.claude"
for item in "$REAL_HOME/.claude"/.* "$REAL_HOME/.claude"/*; do
  name=$(basename "$item")
  [[ "$name" == "." || "$name" == ".." || "$name" == "channels" ]] && continue
  ln -sf "$item" "$FAKE_HOME/.claude/$name"
done

# Level 3: channels except discord
mkdir -p "$FAKE_HOME/.claude/channels"
for item in "$REAL_HOME/.claude/channels"/.* "$REAL_HOME/.claude/channels"/*; do
  name=$(basename "$item")
  [[ "$name" == "." || "$name" == ".." || "$name" == "discord" ]] && continue
  ln -sf "$item" "$FAKE_HOME/.claude/channels/$name"
done

# Level 4: discord — isolated .env with static mode forced
mkdir -p "$FAKE_DISCORD"

# Strip any existing DISCORD_ACCESS_MODE from repo .env and force static.
# Static mode snapshots access.json at boot, disables polling, makes
# saveAccess() a no-op — safe for concurrent sessions.
sed '/^DISCORD_ACCESS_MODE=/d' "$REPO_ENV" > "$FAKE_DISCORD/.env"
echo 'DISCORD_ACCESS_MODE=static' >> "$FAKE_DISCORD/.env"

mkdir -p "$REAL_DISCORD/inbox" "$REAL_DISCORD/approved"
ln -sf "$(pwd)/$REPO_ACCESS" "$FAKE_DISCORD/access.json"
ln -sf "$REAL_DISCORD/approved" "$FAKE_DISCORD/approved"
ln -sf "$REAL_DISCORD/inbox" "$FAKE_DISCORD/inbox"

# --- Launch ---

# Unset any env vars that would override the fake-home .env
unset DISCORD_BOT_TOKEN DISCORD_ACCESS_MODE

PERM_FLAG=""
$BYPASS_PERMS && PERM_FLAG="--permission-mode bypassPermissions"

# shellcheck disable=SC2086
HOME="$FAKE_HOME" claude --channels plugin:discord@claude-plugins-official \
  $PERM_FLAG "$@"
