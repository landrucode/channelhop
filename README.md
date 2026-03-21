# channelhop

Run a separate Discord bot for every Claude Code project — simultaneously, on the same machine.

Out of the box, Claude Code's Discord integration is one-bot-per-machine. Every session shares the same token and the same channel. If you're running Claude across multiple projects on a server and want to check in on any of them from your phone, that's a problem. This solves it.

---

## How it works

The Discord plugin finds its config by calling `os.homedir()` at runtime. You can't override this with a flag. But you *can* set `HOME` to a different directory before launching Claude — and that's exactly what the `cc` script does.

For each session, it:

1. Creates a temporary directory in `/tmp`
2. Symlinks your entire real home into it — dotfiles, config, everything
3. Replaces only `.claude/channels/discord/.env` with the repo's bot token; `access.json` symlinks to the repo file, `approved/` and `inbox/` symlink to shared global dirs
4. Launches Claude with `HOME` pointing at the temp directory

The Discord plugin sees a different config. Everything else sees your normal home. On normal exit, the temp directory and bot token are best-effort shredded.

### Static mode

The plugin can write back to `access.json` — which would corrupt the symlinks in a concurrent setup. The `cc` script forces `DISCORD_ACCESS_MODE=static`, which makes the plugin snapshot config at boot and never write to it again. This is safe for as many concurrent sessions as you have bot tokens.

### Per-repo isolation

Each repo has its own `access.json` listing only its channel. Even if Discord permissions were misconfigured, the wrong bot won't respond — it'll hit its own allowlist first and drop the message.

---

## Setup

### 1. Create one Discord bot per project

In the [Discord Developer Portal](https://discord.com/developers/applications), create an application and bot for each project. In the **Bot** tab, click **Reset Token** to reveal the token — copy it now, you won't see it again. Enable the **Message Content** privileged intent. Invite each bot to your server with at minimum `Send Messages` and `Read Message History` permissions.

### 2. Set channel permissions

Make each channel visible to only you and its bot. In Discord channel settings, deny access for `@everyone` (or your members role), then explicitly allow your user and the bot.

### 3. Add config to each repo

In each project repo, create `.claude/channels/discord/.env`:

```
DISCORD_BOT_TOKEN=your-bot-token-here
```

And `.claude/channels/discord/access.json`:

```json
{
  "dmPolicy": "allowlist",
  "allowFrom": ["YOUR_DISCORD_USER_ID"],
  "groups": {
    "YOUR_CHANNEL_ID": {
      "allowFrom": ["YOUR_DISCORD_USER_ID"],
      "requireMention": false
    }
  },
  "pending": {}
}
```

To find your Discord user ID: Settings → Advanced → enable Developer Mode, then right-click your username → Copy User ID.

To find a channel ID: right-click the channel → Copy Channel ID.

Add `.claude/channels/discord/.env` to your `.gitignore`. Don't commit bot tokens.

### 4. Install the script

```bash
cp cc ~/.local/bin/cc
chmod +x ~/.local/bin/cc
```

---

## Usage

```bash
cd my-project
cc
```

That's it. Claude starts with the correct bot for this repo, listening on the correct channel.

```bash
cc -y   # also pass --permission-mode bypassPermissions (auto-approve tool use)
```

If you always want `bypassPermissions`, remove the flag check in the script and hardcode it into the final `claude` invocation.

To run multiple projects concurrently, open a terminal or tmux session per project and run `cc` in each.

---

## Two gotchas

**Kill stale bot processes before starting.** When a Claude session ends, the Discord plugin's `bun server.ts` process is not always cleaned up. It keeps the bot connection alive on Discord's gateway. If you restart a session without killing the old process, two instances of the same bot will be running — you'll see duplicate responses and typing indicators. Before your first run, or after any crash:

```bash
pkill -f 'bun server.ts'
```

The `cc` script warns you if stale processes are detected, but doesn't kill them automatically. Note: this pattern will match any process named `bun server.ts` on your machine, not just the Discord plugin. If you're running other Bun servers, check first.

**Watch out for shell aliases.** If you previously configured Claude's Discord integration via a shell alias in `.bashrc` or `.zshrc`, that alias will silently override the `cc` script and the HOME isolation never runs. Remove it.

---

## What gets isolated

| Config | Isolated per session? |
|--------|----------------------|
| Bot token (`.env`) | Yes — repo-specific, shredded on exit |
| Channel access (`access.json`) | Yes — repo-specific, plugin read-only (external writes take effect on restart) |
| Claude settings, sessions, tools | No — shared via symlinks (intentional) |
| Approved senders, inbox | No — shared via symlinks (intentional) |

---

## Stability

This script relies on two implementation details of the Claude Code Discord plugin: the config path being resolved via `os.homedir()`, and `DISCORD_ACCESS_MODE=static` being honoured. These are not public APIs. A plugin update could change either behaviour without notice. If something breaks after an update, check those two assumptions first.

## Requirements

- Claude Code with the Discord channels plugin installed
- [Bun](https://bun.sh) (the plugin runs on Bun)
- Python 3 (used for preflight JSON validation)
- Linux/macOS

---

## License

MIT
