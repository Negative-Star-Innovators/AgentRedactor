import { getLatestReleaseTag } from '../lib/github.js';

// Self-release update channels: x64 builds use the original 'win' channel,
// ARM64 builds use 'win-arm64'. Both redirect to assets on the same GitHub
// release — the channel only selects which feed/files a build requests.
const CHANNEL_PATTERN = /^(win|win-arm64)$/;

// Strict allowlist for files Velopack requests from the update feed:
//   releases.<channel>.json, *.nupkg, *-Setup.exe, *-Portable.zip
// Case-sensitive, matched against the basename only.
const FILE_PATTERN = /^(releases\.[a-z0-9-]+\.json|[^/\\]+\.nupkg|[^/\\]+-Setup\.exe|[^/\\]+-Portable\.zip)$/;

function jsonError(message, status) {
  return new Response(JSON.stringify({ error: message }), {
    status,
    headers: { 'Content-Type': 'application/json; charset=utf-8' },
  });
}

export default async function updates(request, env, ctx, params) {
  const channel = params.channel || '';
  const file = params.file || '';

  // Basename-only: reject anything with slashes, backslashes, or traversal.
  if (!CHANNEL_PATTERN.test(channel) ||
      file.includes('..') || file.includes('/') || file.includes('\\') || !FILE_PATTERN.test(file)) {
    return jsonError('not found', 404);
  }

  const repo = env.GITHUB_REPO || 'Negative-Star-Innovators/AgentRedactor';

  let tag;
  try {
    tag = await getLatestReleaseTag(repo, env, ctx);
  } catch (err) {
    return jsonError('failed to look up latest release', 502);
  }

  if (!tag) {
    return jsonError('no published release found', 502);
  }

  return new Response(null, {
    status: 302,
    headers: {
      Location: `https://github.com/${repo}/releases/download/${tag}/${file}`,
      'Cache-Control': 'no-store',
    },
  });
}
