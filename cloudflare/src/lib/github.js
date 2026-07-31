// Latest-release lookup against the GitHub API, with the tag name cached
// for 5 minutes in the Cache API (caches.default) so update checks don't
// burn through the GitHub rate limit.

const CACHE_TTL_SECONDS = 300; // 5 minutes

export async function getLatestReleaseTag(repo, env, ctx) {
  const apiUrl = `https://api.github.com/repos/${repo}/releases/latest`;

  // Cache API keys must be valid request URLs; use our own host as the key.
  const cacheKey = new Request(`https://agentredactor-api.internal/cache/release-tag/${repo}`);
  const cache = caches.default;

  const cached = await cache.match(cacheKey);
  if (cached) {
    return await cached.text();
  }

  const headers = {
    'User-Agent': 'agentredactor-api-worker',
    Accept: 'application/vnd.github+json',
  };
  if (env.GH_TOKEN) {
    headers.Authorization = `Bearer ${env.GH_TOKEN}`;
  }

  const resp = await fetch(apiUrl, { headers });
  if (!resp.ok) {
    throw new Error(`GitHub API responded ${resp.status}`);
  }

  const data = await resp.json();
  const tag = data.tag_name;
  if (!tag) {
    throw new Error('GitHub API response missing tag_name');
  }

  // /releases/latest only returns published, non-draft, non-prerelease releases.
  const cachedResp = new Response(tag, {
    headers: { 'Cache-Control': `public, max-age=${CACHE_TTL_SECONDS}` },
  });
  ctx.waitUntil(cache.put(cacheKey, cachedResp));

  return tag;
}
