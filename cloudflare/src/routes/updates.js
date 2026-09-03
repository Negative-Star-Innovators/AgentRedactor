// Self-release update channels: x64 builds use the original 'win' / 'linux'
// channels, ARM64 builds use 'win-arm64' / 'linux-arm64'. Releases are
// served exclusively from the RELEASES_BUCKET R2 binding at <channel>/<file>
// (R2 is the single host — no GitHub fallback). CI uploads each release via
// `vpk upload s3`.
const CHANNEL_PATTERN = /^(win|win-arm64|linux|linux-arm64)$/;

// Strict allowlist for files Velopack requests from the update feed:
//   releases.<channel>.json, *.nupkg, *-Setup.exe, *-Portable.zip, *.AppImage
// Case-sensitive, matched against the basename only.
const FILE_PATTERN = /^(releases\.[a-z0-9-]+\.json|[^/\\]+\.nupkg|[^/\\]+-Setup\.exe|[^/\\]+-Portable\.zip|[^/\\]+\.AppImage)$/;

// Versioned nupkgs are immutable; the fixed-name feed/installer files
// (releases.*.json, *-Setup.exe, *-Portable.zip) change every release.
const IMMUTABLE_CACHE = 'public, max-age=31536000, immutable';
const MUTABLE_CACHE = 'no-store';

function jsonError(message, status) {
  return new Response(JSON.stringify({ error: message }), {
    status,
    headers: { 'Content-Type': 'application/json; charset=utf-8' },
  });
}

// Normalize an R2Range ({offset, length?} | {suffix}) against the object
// size into concrete [start, end] byte bounds for Content-Range.
function rangeBounds(range, size) {
  if ('suffix' in range) {
    const start = Math.max(0, size - range.suffix);
    return { start, end: size - 1 };
  }
  const start = range.offset;
  const length = range.length === undefined
    ? size - start
    : Math.min(range.length, size - start);
  return { start, end: start + length - 1 };
}

export default async function updates(request, env, ctx, params) {
  const channel = params.channel || '';
  const file = params.file || '';

  // Basename-only: reject anything with slashes, backslashes, or traversal.
  if (!CHANNEL_PATTERN.test(channel) ||
      file.includes('..') || file.includes('/') || file.includes('\\') || !FILE_PATTERN.test(file)) {
    return jsonError('not found', 404);
  }

  const object = await env.RELEASES_BUCKET.get(`${channel}/${file}`, {
    range: request.headers,
    onlyIf: request.headers,
  });

  if (object === null) {
    return jsonError('not found', 404);
  }

  const headers = new Headers();
  object.writeHttpMetadata(headers);
  headers.set('etag', object.httpEtag);
  headers.set('Accept-Ranges', 'bytes');
  headers.set('Cache-Control', file.endsWith('.nupkg') ? IMMUTABLE_CACHE : MUTABLE_CACHE);

  // No body means a conditional header (If-None-Match etc.) failed.
  if (!('body' in object)) {
    return new Response(null, {
      status: request.headers.has('if-none-match') ? 304 : 412,
      headers,
    });
  }

  let status = 200;
  let contentLength = object.size;
  if (object.range && request.headers.has('range')) {
    const { start, end } = rangeBounds(object.range, object.size);
    status = 206;
    contentLength = end - start + 1;
    headers.set('Content-Range', `bytes ${start}-${end}/${object.size}`);
  }
  headers.set('Content-Length', String(contentLength));

  return new Response(request.method === 'HEAD' ? null : object.body, {
    status,
    headers,
  });
}
