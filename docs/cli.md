# Agent Redactor CLI

The Agent Redactor engine binary doubles as a command-line interface. On
Windows it ships as `agentredactor.exe` next to the GUI (the Microsoft Store
package also registers an `agentredactor` AppExecutionAlias); on Linux the
installer symlinks `agentredactor` into `~/.local/bin`, and AppImage
invocations are forwarded to the same binary.

Every command talks to the *running* engine over its localhost control API, so
the CLI and the GUI always agree on state — use either, interchangeably.

- Output goes to the console, or UTF-8 to stdout when piped (script-friendly).
- Exit codes: `0` success, `1` runtime error, `2` usage error.
- `agentredactor help` prints the built-in reference; it is always in sync
  with the binary you have installed.
- Quote regex patterns containing `{ } [ ]` etc. so the shell does not expand
  them — bash: single quotes, PowerShell/cmd: double quotes, e.g.
  `agentredactor regex add 'sk-[a-zA-Z0-9]{20,}'`.

## Commands

### Overview

```text
agentredactor status            engine + profile overview (ungated)
agentredactor languages         list supported UI language codes (ungated)
agentredactor download-model    download the AI model weights (first run; shows progress)
```

On Linux, `status` additionally checks the update feed and prints an
"update available" line when a newer release exists.

### Settings and profiles

```text
agentredactor get <key> [--profile P]          read a setting
agentredactor set <key> <value> [--profile P]  change a setting
agentredactor profiles list                    list profiles (with request/redaction stats)
agentredactor profiles add <alias> [--port N] [--upstream-url U] [--api-key K]
agentredactor profiles delete <id>
```

- `--profile P` selects a profile by list number, id, or alias (optional when
  only one profile exists).
- Without `--port`, `profiles add` picks the first free port from 8080 and
  prints the created profile's id.
- `profiles delete` accepts only the profile id (never an alias or list
  number) and refuses to delete the last profile.

Global keys: `start-on-boot`, `logging`, `show-sensitive`, `app-language`.

Profile keys: `alias`, `upstream-url`, `api-key`, `port`,
`confidence-threshold` (0..1), `use-ai-model`.

`app-language` must be an exact supported BCP-47 tag — the values are what
`agentredactor languages` prints (`en`, `de`, `zh-CN`, …); a partial tag like
`zh` is rejected.

### Redaction lists (profile-scoped)

```text
agentredactor regex list [--profile P]
agentredactor regex add <pattern> [--profile P]
agentredactor regex remove <n|pattern> [--profile P]
agentredactor keywords list [--profile P]
agentredactor keywords add <text> [--ignore-case] [--profile P]
agentredactor keywords remove <n|text> [--ignore-case] [--profile P]
agentredactor pii-types list [--profile P]
agentredactor pii-types enable | disable <type> [--profile P]
```

- `pii-types` deals in single PII types only (no categories): `account_number`,
  `private_address`, `private_date`, `private_email`, `private_person`,
  `private_phone`, `private_url`, `secret`.
- Duplicates are rejected on add: a keyword is a duplicate when both text and
  case-sensitivity match; a regex when the normalized pattern matches
  (`{,5}` == `{0,5}`).
- `keywords remove <text>` selects a case-insensitive entry with
  `--ignore-case`; without it, the first case-sensitive match, falling back to
  an ignore-case entry.

### Security

```text
agentredactor password enable
agentredactor password disable
```

- **Windows:** enables/disables Windows Hello protection. With protection on,
  every read/write command demands a fresh Hello consent (`status` and `help`
  stay open).
- **Linux:** enables/disables a typed master password. With protection on,
  every read/write command prompts for it (`status` and `help` stay open).

### Maintenance (Linux only)

```text
agentredactor update             check for and install AppImage updates
agentredactor uninstall [--yes]  remove Agent Redactor, settings, icons, and the AppImage
```

`update` swaps the AppImage for the latest release on its update channel;
restart to apply (`systemctl --user restart agentredactor` on headless
installs). `uninstall` asks for confirmation unless `--yes` is given.

## Scripting examples

```bash
# Is the engine up? (on Linux this also reports when an update is available)
agentredactor status

# Add a keyword from a script; a duplicate is rejected with a clear message
agentredactor keywords add "internal-codename" --ignore-case

# Turn off phone-number redaction on the default profile
agentredactor pii-types disable private_phone

# Headless server: point a new agent profile at OpenRouter
agentredactor profiles add openrouter \
  --upstream-url https://openrouter.ai/api/v1 --api-key sk-or-...

# Review the result
agentredactor profiles list
```

## Notes

- There is deliberately no `engine run` / `engine stop`: engine lifecycle
  belongs to the GUI (or the systemd user service on headless Linux).
- On Linux the model weights download automatically on first engine start;
  `download-model` exists so a fresh headless install can bootstrap without
  waiting (`install.sh` already runs it for you).
