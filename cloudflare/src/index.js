import updates from './routes/updates.js';
import { INSTALL_PS1 } from './static/install.ps1.js';

// Tiny hand-rolled router. To add a route module later, import it and
// add one entry: { pattern: '/feedback/:id?', handler: feedback }.
// Patterns are '/'-separated; segments starting with ':' capture a param,
// and a trailing '?' makes the segment optional.
const routes = [
  { pattern: '/updates/:channel/:file', handler: updates },
  { pattern: '/install.ps1', handler: installPs1 },
  { pattern: '/health', handler: health },
];

function matchRoute(pattern, path) {
  const pSegs = pattern.split('/').filter(Boolean);
  const uSegs = path.split('/').filter(Boolean);
  const params = {};

  let i = 0;
  for (; i < pSegs.length; i++) {
    const seg = pSegs[i];
    const optional = seg.endsWith('?');
    const name = optional ? seg.slice(0, -1) : seg;
    const value = uSegs[i];

    if (value === undefined) {
      if (optional) continue;
      return null;
    }
    if (name.startsWith(':')) {
      params[name.slice(1)] = value;
    } else if (name !== value) {
      return null;
    }
  }
  if (i < uSegs.length) return null; // unmatched trailing segments
  return params;
}

function json(body, status = 200) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { 'Content-Type': 'application/json; charset=utf-8' },
  });
}

function health() {
  return json({ ok: true, service: 'agentredactor-api' });
}

function installPs1() {
  return new Response(INSTALL_PS1, {
    headers: { 'Content-Type': 'text/plain; charset=utf-8' },
  });
}

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);

    if (request.method !== 'GET' && request.method !== 'HEAD') {
      return json({ error: 'not found' }, 404);
    }

    for (const route of routes) {
      const params = matchRoute(route.pattern, url.pathname);
      if (params) {
        return route.handler(request, env, ctx, params);
      }
    }
    return json({ error: 'not found' }, 404);
  },
};
