// Serves the app's AI model weights from the MODELS_BUCKET R2 binding.
// The app's downloader does segmented/resumable downloads, so Range and
// conditional requests are passed straight through to R2.
const FILE_PATTERN = /^(model_quantized\.onnx_data)$/;

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

export default async function models(request, env, ctx, params) {
  const file = params.file || '';

  // Basename-only: reject anything with slashes, backslashes, or traversal.
  if (file.includes('..') || file.includes('/') || file.includes('\\') || !FILE_PATTERN.test(file)) {
    return jsonError('not found', 404);
  }

  const object = await env.MODELS_BUCKET.get(file, {
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
  headers.set('Content-Type', 'application/octet-stream');
  headers.set('Cache-Control', 'public, max-age=31536000, immutable');

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
