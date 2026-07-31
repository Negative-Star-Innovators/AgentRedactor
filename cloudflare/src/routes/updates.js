import { getLatestReleaseTag } from '../lib/github.js';

// Strict allowlist for files Velopack requests from the update feed:
//   releases.win.json, *.nupkg, *-Setup.exe, *-Portable.zip
// Case-sensitive, matched against the basename only.
const FILE_PATTERN = /^(releases\.win\.json|[^/\\]+\.nupkg|[^/\\]+-Setup\.exe|[^/\\]+-Portable\.zip)$/;

function jsonError(message, status) {
  return new Response(JSON.stringify({ error: message }), {
    status,
    headers: { 'Content-Type': 'application/json; charset=utf-8' },
  });
}

export default async function updates(request, env, ctx, params) {
  const file = params.file || '';

  // Basename-only: reject anything with slashes, backslashes, or traversal.
  if (file.includes('..') || file.includes('/') || file.includes('\\') || !FILE_PATTERN.test(file)) {
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
