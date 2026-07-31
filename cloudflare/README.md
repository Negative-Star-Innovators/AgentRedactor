# agentredactor-api (Cloudflare Worker)

Worker that fronts update checks and the install bootstrapper for AgentRedactor (Velopack-based Windows app).

## Routes

- `GET /updates/win/:file` — 302 redirect to the matching asset on the latest published GitHub release (tag cached 5 min). Allowlisted files: `releases.win.json`, `*.nupkg`, `*-Setup.exe`, `*-Portable.zip`.
- `GET /install.ps1` — PowerShell one-line installer (`iex "& { $(irm https://api.agentredactor.negativestarinnovators.com/install.ps1) }"`).
- `GET /health` — `{"ok":true,"service":"agentredactor-api"}`.
- Everything else — 404 JSON.

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
