# agentredactor-api (Cloudflare Worker)

Worker that fronts update checks and the install bootstrapper for AgentRedactor (Velopack-based Windows app).

## Routes

- `GET /updates/:channel/:file` — serves Velopack update assets exclusively from the `RELEASES_BUCKET` R2 bucket at `<channel>/<file>` with HTTP Range support and conditional requests (ETag / 304). R2 is the only update source — there is no GitHub fallback; missing objects return 404. Channels: `win` (x64) and `win-arm64` (ARM64). Allowlisted files: `releases.<channel>.json`, `*.nupkg`, `*-Setup.exe`, `*-Portable.zip`. Versioned `*.nupkg` files are cached immutably; the fixed-name feed/installer files are `no-store`.
- `GET /install.ps1` — PowerShell one-line installer (`iex "& { $(irm https://api.agentredactor.negativestarinnovators.com/install.ps1) }"`). Detects CPU architecture (`AMD64` vs `ARM64`) and downloads the matching `*-Setup.exe`; on ARM64 it falls back to the x64 installer (runs under emulation) if no ARM64 build is published yet.
- `GET /models/:file` — serves the app's AI model weights from the `MODELS_BUCKET` R2 bucket with HTTP Range support (segmented/resumable downloads) and conditional requests (ETag / 304). Allowlisted files: `model_quantized.onnx_data`. Cached immutably (`Cache-Control: public, max-age=31536000, immutable`) since the model is content-versioned.
- `GET /health` — `{"ok":true,"service":"agentredactor-api"}`.
- Everything else — 404 JSON.

## Update channels

The self-release channel is split per architecture: x64 builds check `/updates/win/releases.win.json`, ARM64 builds check `/updates/win-arm64/releases.win-arm64.json`. CI publishes both channels to the `agentredactor-releases` R2 bucket (under `win/` and `win-arm64/` prefixes) via the `release-selfrelease.yml` publish job. R2 is the sole host — releases are not published to GitHub.

## Updates hosting (R2)

The Velopack update feeds and installers are served from the `agentredactor-releases` bucket, keyed `<channel>/<file>` (e.g. `win/releases.win.json`). This is the only update source.

1. Create the bucket: `npx wrangler r2 bucket create agentredactor-releases`
2. Create an R2 API token for CI: Cloudflare dashboard → R2 → Manage API tokens → create a token with **Object Read & Write** permission scoped to the `agentredactor-releases` bucket.
3. Add these GitHub repo secrets (Settings → Secrets and variables → Actions):
   - `R2_ACCOUNT_ID` — the Cloudflare account ID (forms the endpoint `https://<account-id>.r2.cloudflarestorage.com`)
   - `R2_ACCESS_KEY_ID` / `R2_SECRET_ACCESS_KEY` — the token credentials from step 2
4. `npx wrangler deploy` (binds `RELEASES_BUCKET`).

From then on, the `publish` job in `.github/workflows/release-selfrelease.yml` uploads each channel automatically via `vpk upload s3 --bucket agentredactor-releases --prefix <channel> ...` (see the workflow for the full command lines). No manual upload is needed.

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

1. Either have the `negativestarinnovators.com` zone active on Cloudflare, or deploy to the default `workers.dev` subdomain as a fallback (skip step 4 then).
2. `npx wrangler login`
3. `npx wrangler deploy`
4. Attach the custom domain: Cloudflare dashboard → Workers → `agentredactor-api` → Settings → Domains & Routes → add `api.agentredactor.negativestarinnovators.com` (DNS record and certificate are created automatically). Alternatively uncomment the `routes` block in `wrangler.toml`.

## Verify

Once a release has been published to R2:

```
curl -I https://api.agentredactor.negativestarinnovators.com/updates/win/releases.win.json
```

Expect `HTTP 200` from the bucket with `Accept-Ranges: bytes` and `Cache-Control: no-store`. Missing objects return `HTTP 404` JSON (no redirect).

```
curl https://api.agentredactor.negativestarinnovators.com/health
```

Expect `{"ok":true,"service":"agentredactor-api"}`.
