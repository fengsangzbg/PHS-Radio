'use strict';

// This bridge calls the platform's account APIs only. It never enables alternate
// sources, geolocation spoofing or the upstream project's optional unblocking.
process.env.ENABLE_GENERAL_UNBLOCK = 'false';
process.env.ENABLE_RANDOM_CN_IP = 'false';
process.env.ENABLE_PROXY = 'false';
process.env.DEBUG = '';
process.env.NETEASE_COOKIE = '';
process.env.DOTENV_CONFIG_QUIET = 'true';

const http = require('node:http');
const operations = {
  '/login/qr/key': ['login_qr_key', []],
  '/login/qr/create': ['login_qr_create', ['key', 'qrimg']],
  '/login/qr/check': ['login_qr_check', ['key']],
  '/login/status': ['login_status', []],
  '/recommend/songs': ['recommend_songs', []],
  '/lyric': ['lyric', ['id']],
  '/song/url/v1': ['song_url_v1', ['id']],
};

function createBridge(api) {
  return http.createServer(async (request, response) => {
    response.setHeader('Content-Type', 'application/json; charset=utf-8');
    response.setHeader('Cache-Control', 'no-store');
    const reply = (status, body) => {
      response.writeHead(status);
      response.end(JSON.stringify(body));
    };
    const route = new URL(request.url, 'http://127.0.0.1').pathname;
    if (route === '/health' && request.method === 'GET')
      return reply(200, { code: 200, service: 'phs-radio-netease' });
    if (request.method !== 'POST' || !operations[route])
      return reply(404, { code: 404, message: 'Unsupported local API route.' });
    if (!(request.headers['content-type'] || '').startsWith('application/json'))
      return reply(415, { code: 415, message: 'JSON request data is required.' });
    if (request.headers.origin)
      return reply(403, { code: 403, message: 'Browser origins are not accepted.' });
    let source = '';
    try {
      for await (const chunk of request) {
        source += chunk.toString('utf8');
        if (Buffer.byteLength(source) > 65536)
          return reply(413, { code: 413, message: 'Request too large.' });
      }
      const body = source ? JSON.parse(source) : {};
      const [operation, allowedKeys] = operations[route];
      const params = { cookie: request.headers.authorization || '', timestamp: Date.now() };
      for (const key of allowedKeys)
        if (body[key] !== undefined) params[key] = body[key];
      if (route === '/song/url/v1') {
        params.level = 'standard';
        params.unblock = 'false';
        params.randomCNIP = 'false';
      }
      const result = await api[operation](params);
      const payload = result.body || {};
      // Cookie responses are returned only to the login caller; there is no
      // process-wide account session, persisted plaintext or response cache.
      return reply(result.status >= 400 ? result.status : 200, payload);
    } catch (error) {
      // Never print the upstream error object: it can contain account cookies.
      const code = Number(error?.body?.code) || Number(error?.status) || 502;
      const upstreamStatus = Number(error?.status);
      const status = upstreamStatus >= 400 && upstreamStatus <= 599 ? upstreamStatus
        : code === 301 || code === 302 ? 401 : code >= 400 && code <= 599 ? code : 502;
      return reply(status,
                   { code, message: 'The platform request could not be completed.' });
    }
  });
}

if (require.main === module) {
  // The upstream modules log entire rejected HTTP responses, including cookies.
  // Keep this private service quiet; all request errors are reported by JSON.
  const startupLog = console.log.bind(console);
  const startupError = console.error.bind(console);
  for (const method of ['log', 'info', 'debug', 'warn', 'error'])
    console[method] = () => {};
  const api = require('@neteasecloudmusicapienhanced/api');
  createBridge(api).listen(3738, '127.0.0.1', () => {
    startupLog('PHS Radio NetEase bridge listening on 127.0.0.1:3738');
  }).on('error', () => {
    startupError('Cannot start the local NetEase bridge.');
    process.exitCode = 1;
  });
}

module.exports = { createBridge };
