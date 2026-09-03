# agentredactor-api (Cloudflare Worker)

Worker that fronts update checks and the install bootstrapper for AgentRedactor (Velopack-based Windows and Linux app).

## Routes

- `GET /updates/:channel/:file` — serves Velopack update assets exclusively from the `RELEASES_BUCKET` R2 bucket at `<channel>/<file>` with HTTP Range support and conditional requests (ETag / 304). R2 is the only update source — there is no GitHub fallback; missing objects return 404. Channels: `win` (Windows x64), `win-arm64` (Windows ARM64), `linux` (Linux x64), and `linux-arm64` (Linux ARM64). Allowlisted files: `releases.<channel>.json`, `*.nupkg`, `*-Setup.exe`, `*-Portable.zip`, `*.AppImage`. Versioned `*.nupkg` files are cached immutably; the fixed-name feed/installer files are `no-store`.
- `GET /install.ps1` — PowerShell one-line installer (`iex "& { $(irm https://api.agentredactor.negativestarinnovators.com/install.ps1) }"`). Detects CPU architecture (`AMD64` vs `ARM64`) and downloads the matching `*-Setup.exe`; on ARM64 it falls back to the x64 installer (runs under emulation) if no ARM64 build is published yet.
- `GET /models/:file` — serves the app's AI model weights from the `MODELS_BUCKET` R2 bucket with HTTP Range support (segmented/resumable downloads) and conditional requests (ETag / 304). Allowlisted files: `model_quantized.onnx_data`. Cached immutably (`Cache-Control: public, max-age=31536000, immutable`) since the model is content-versioned.
- `GET /health` — `{"ok":true,"service":"agentredactor-api"}`.
- Everything else — 404 JSON.

## Update channels

The self-release channel is split per OS and architecture:
- Windows x64 checks `/updates/win/releases.win.json`
- Windows ARM64 checks `/updates/win-arm64/releases.win-arm64.json`
- Linux x64 checks `/updates/linux/releases.linux.json`
- Linux ARM64 checks `/updates/linux-arm64/releases.linux-arm64.json`

CI publishes all four channels to the `agentredactor-releases` R2 bucket (under
`win/`, `win-arm64/`, `linux/`, and `linux-arm64/` prefixes) via the
`release-selfrelease.yml` and `build-linux.yml` publish jobs. R2 is the sole
host — releases are not published to GitHub.

## Updates hosting (R2)

The Velopack update feeds and installers are served from the `agentredactor-releases` bucket, keyed `<channel>/<file>` (e.g. `win/releases.win.json`). This is the only update source.

1. Create the bucket: `npx wrangler r2 bucket create agentredactor-releases`
2. Create an R2 API token for CI: Cloudflare dashboard → R2 → Manage API tokens → create a token with **Object Read & Write** permission scoped to the `agentredactor-releases` bucket.
3. Add these GitHub repo secrets (Settings → Secrets and variables → Actions):
   - `R2_ACCOUNT_ID` — the Cloudflare account ID (forms the endpoint `https://<account-id>.r2.cloudflarestorage.com`)
   - `R2_ACCESS_KEY_ID` / `R2_SECRET_ACCESS_KEY` — the token credentials from step 2
4. `npx wrangler deploy` (binds `RELEASES_BUCKET`).

From then on, the `publish` job in `.github/workflows/release-selfrelease.yml` uploads each channel automatically via `vpk upload s3 --bucket agentredactor-releases --prefix <channel> ...` (see the workflow for the full command lines). No manual upload is needed.

### Seeding the first release manually (chicken-and-egg)

Some tests (the upgrade E2E's "previous live release", and the `verify-live`
one-liner install canary) need *something* already live in R2. Before the
first tagged release exists, seed the bucket manually.

**Yes — you run these commands on your own PC (Windows, PowerShell).** They
take the files GitHub already built (downloaded from a successful workflow
run) and push them into the R2 bucket. Nothing runs on GitHub's side; your PC
is just the courier. From then on the api worker serves them at
`/updates/<channel>/...`, exactly as if a real release had happened.

1. Download the vpk output artifacts from a successful **Self-Release
   (Velopack)** run (Actions → the run → Artifacts at the bottom of the page),
   or with the `gh` CLI from the repo root:

   ```powershell
   # find the run id of the latest successful Self-Release run on your branch
   gh run list --workflow release-selfrelease.yml --branch feature/self-release --status success --limit 1

   # download both channels' vpk output (substitute the run id and version)
   gh run download <RUN_ID> -n AgentRedactor-velopack-<version>-x64   -D seed\velopack-x64
   gh run download <RUN_ID> -n AgentRedactor-velopack-<version>-arm64 -D seed\velopack-arm64
   ```

   Each folder then contains that channel's vpk output
   (`releases.<channel>.json`, `*-full.nupkg`, `*-Setup.exe`).

2. Install the Velopack CLI once:

   ```powershell
   dotnet tool install -g vpk
   ```

3. Upload each channel to R2 with your R2 credentials (Cloudflare dashboard →
   R2 → Manage API tokens; same values as the `R2_*` GitHub secrets):

   ```powershell
   # x64 channel (prefix win)
   vpk upload s3 --bucket agentredactor-releases --endpoint https://<R2_ACCOUNT_ID>.r2.cloudflarestorage.com --region auto --keyId <KEY> --secret <SECRET> --prefix win --outputDir seed\velopack-x64
   # ARM64 channel (prefix win-arm64)
   vpk upload s3 --bucket agentredactor-releases --endpoint https://<R2_ACCOUNT_ID>.r2.cloudflarestorage.com --region auto --keyId <KEY> --secret <SECRET> -c win-arm64 --prefix win-arm64 --outputDir seed\velopack-arm64
   ```

4. Verify the worker now serves the feeds:

   ```
   curl -I https://api.agentredactor.negativestarinnovators.com/updates/win/releases.win.json
   curl -I https://api.agentredactor.negativestarinnovators.com/updates/win-arm64/releases.win-arm64.json
   ```

   Both should return `HTTP 200`.

#### Seeding the first Linux release manually

The Linux workflow artifacts are named `AgentRedactor-velopack-linux-x64` and
`AgentRedactor-velopack-linux-arm64`. Download them from a successful
**Build Linux** run, then upload with the same `vpk upload s3` command using
prefixes `linux` and `linux-arm64`:

```bash
# x64 channel (prefix linux)
vpk upload s3 --bucket agentredactor-releases --endpoint https://<R2_ACCOUNT_ID>.r2.cloudflarestorage.com --region auto --keyId <KEY> --secret <SECRET> -c linux --prefix linux --outputDir seed/velopack-linux-x64
# ARM64 channel (prefix linux-arm64)
vpk upload s3 --bucket agentredactor-releases --endpoint https://<R2_ACCOUNT_ID>.r2.cloudflarestorage.com --region auto --keyId <KEY> --secret <SECRET> -c linux-arm64 --prefix linux-arm64 --outputDir seed/velopack-linux-arm64
```

Verify:

```
curl -I https://api.agentredactor.negativestarinnovators.com/updates/linux/releases.linux.json
curl -I https://api.agentredactor.negativestarinnovators.com/updates/linux-arm64/releases.linux-arm64.json
```

Both should return `HTTP 200`.

WARNING: this is a *live* publish — every installed self-release instance
will auto-update to whatever you seed. Only do this pre-launch (or with a
build you're happy to ship).

## Model hosting (R2)

The quantized ONNX weights (`model_quantized.onnx_data`, ~1.6 GB) are served from an R2 bucket via `/models/:file`, with Range support for the app's segmented/resumable downloader. This is the app's only model source.

1. Create the bucket: `npx wrangler r2 bucket create agentredactor-models`
2. Upload the weights. Do NOT use `wrangler r2 object put` for this file — wrangler caps uploads at 300 MiB (and without `--remote` it silently writes to local dev state under `.wrangler/state/`). Use rclone against R2's S3-compatible endpoint with an R2 API token (dash → R2 → Manage API tokens, **Object Read & Write**); `no_check_bucket=true` is required because object-scoped tokens can't perform bucket-level operations:

   ```
   winget install Rclone.Rclone
   rclone config create r2 s3 provider=Cloudflare access_key_id=<ACCESS_KEY_ID> secret_access_key=<SECRET> endpoint=https://<account-id>.r2.cloudflarestorage.com no_check_bucket=true
   rclone copyto AgentRedactor/models/onnx/model_quantized.onnx_data r2:agentredactor-models/model_quantized.onnx_data --progress
   ```

   (run from the repo root; the repo copy of the file is at `AgentRedactor/models/onnx/model_quantized.onnx_data`)

3. `npx wrangler deploy`

Verify:

```
curl -I https://api.agentredactor.negativestarinnovators.com/models/model_quantized.onnx_data
```

Expect `HTTP 200` with `Accept-Ranges: bytes` and a `Content-Length` matching the file size.

```
curl -H "Range: bytes=0-99" https://api.agentredactor.negativestarinnovators.com/models/model_quantized.onnx_data -o range.bin
```

Expect `HTTP 206` with a `Content-Range: bytes 0-99/<size>` header and exactly 100 bytes in `range.bin`.

## Deploy

Deploys run from CI (`.github/workflows/deploy-worker.yml`): PRs touching
`cloudflare/**` get a `wrangler deploy --dry-run` validation plus an
`install.ps1` PowerShell parse check; if the secrets below are configured, PRs
also upload an **inactive** Worker version (`wrangler versions upload`) — it
proves credentials + bundle work without touching what production serves.
Pushes to `main` (and manual dispatches) deploy for real and then smoke-test
the live endpoints (`/health`, `/install.ps1`, a `/models` Range request, the
update feeds).

Required repo secrets (GitHub repo → **Settings → Secrets and variables →
Actions → New repository secret**):

- `CLOUDFLARE_ACCOUNT_ID` — Cloudflare dashboard → any zone/overview page →
  the **Account ID** shown in the right-hand sidebar (also in the dashboard
  URL: `dash.cloudflare.com/<account-id>/...`).
- `CLOUDFLARE_API_TOKEN` — Cloudflare dashboard → top-right profile menu →
  **My Profile → API Tokens → Create Token** → use the **"Edit Cloudflare
  Workers"** template (or a custom token with **Account → Workers Scripts →
  Edit** scoped to this account) → create, copy the value (shown once).

Manual deploy (fallback / first-time setup):

1. Either have the `negativestarinnovators.com` zone active on Cloudflare, or deploy to the default `workers.dev` subdomain as a fallback (skip step 4 then).
2. `npx wrangler login`
3. `npx wrangler deploy`
4. Attach the custom domain: Cloudflare dashboard → Workers → `agentredactor-api` → Settings → Domains & Routes → add `api.agentredactor.negativestarinnovators.com` (DNS record and certificate are created automatically). Alternatively uncomment the `routes` block in `wrangler.toml`.

## Verify

Once a release has been published to R2:

```
curl -I https://api.agentredactor.negativestarinnovators.com/updates/win/releases.win.json
curl -I https://api.agentredactor.negativestarinnovators.com/updates/linux/releases.linux.json
```

Expect `HTTP 200` from the bucket with `Accept-Ranges: bytes` and `Cache-Control: no-store`. Missing objects return `HTTP 404` JSON (no redirect).

```
curl https://api.agentredactor.negativestarinnovators.com/health
```

Expect `{"ok":true,"service":"agentredactor-api"}`.
