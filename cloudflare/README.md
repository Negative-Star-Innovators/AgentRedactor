# agentredactor-api (Cloudflare Worker)

Worker that fronts update checks and the install bootstrapper for AgentRedactor (Velopack-based Windows app).

## Routes

- `GET /updates/:channel/:file` — 302 redirect to the matching asset on the latest published GitHub release (tag cached 5 min). Channels: `win` (x64) and `win-arm64` (ARM64); both point at the same release — the channel only selects which files a build requests. Allowlisted files: `releases.<channel>.json`, `*.nupkg`, `*-Setup.exe`, `*-Portable.zip`.
- `GET /install.ps1` — PowerShell one-line installer (`iex "& { $(irm https://api.agentredactor.negativestarinnovators.com/install.ps1) }"`). Detects CPU architecture (`AMD64` vs `ARM64`) and downloads the matching `*-Setup.exe`; on ARM64 it falls back to the x64 installer (runs under emulation) if no ARM64 build is published yet.
- `GET /health` — `{"ok":true,"service":"agentredactor-api"}`.
- Everything else — 404 JSON.

## Update channels

The self-release channel is split per architecture: x64 builds check `/updates/win/releases.win.json`, ARM64 builds check `/updates/win-arm64/releases.win-arm64.json`. Both sets of assets live on the same GitHub release (published by the `release-selfrelease.yml` matrix), so this worker just redirects to the latest release either way.

## Deploy

1. Either have the `negativestarinnovators.com` zone active on Cloudflare, or deploy to the default `workers.dev` subdomain as a fallback (skip step 4 then).
2. `npx wrangler login`
3. `npx wrangler deploy`
4. Attach the custom domain: Cloudflare dashboard → Workers → `agentredactor-api` → Settings → Domains & Routes → add `api.agentredactor.negativestarinnovators.com` (DNS record and certificate are created automatically). Alternatively uncomment the `routes` block in `wrangler.toml`.
5. Optional but recommended: `npx wrangler secret put GH_TOKEN` — a read-only GitHub token, to avoid the 60 req/hr unauthenticated API rate limit.

## Verify

Once a GitHub release exists:

```
curl -I https://api.agentredactor.negativestarinnovators.com/updates/win/releases.win.json
```

Expect `HTTP 302` with a `Location` pointing at `github.com/.../releases/download/<tag>/releases.win.json`.

```
curl https://api.agentredactor.negativestarinnovators.com/health
```

Expect `{"ok":true,"service":"agentredactor-api"}`.
