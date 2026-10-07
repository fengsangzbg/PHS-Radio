'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { createBridge } = require('./server');

test('local bridge keeps platform sessions isolated and disables alternative audio sources', async () => {
  const calls = [];
  const api = Object.fromEntries(['login_qr_key', 'login_qr_create', 'login_qr_check',
    'login_status', 'recommend_songs', 'lyric', 'song_url_v1'].map(name => [name, async params => {
    calls.push({ name, params });
    if (params.id === 'reject') throw { status: 401, body: { code: 301, cookie: 'SECRET-fixture' } };
    return { status: 200, body: { code: 200, data: [] } };
  }]));
  const server = createBridge(api);
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const root = `http://127.0.0.1:${server.address().port}`;
  const post = (path, body = {}, headers = {}) => fetch(root + path, {
    method: 'POST', headers: { 'Content-Type': 'application/json', ...headers }, body: JSON.stringify(body),
  });
  try {
    const health = await fetch(root + '/health');
    assert.equal((await health.json()).service, 'phs-radio-netease');
    assert.equal(health.headers.get('cache-control'), 'no-store');
    assert.equal(health.headers.get('access-control-allow-origin'), null);
    await post('/song/url/v1', { id: '42', level: 'hires', unblock: 'true', source: 'other',
      randomCNIP: 'true', cookie: 'INJECTED' }, { Authorization: 'MUSIC_U=fixture-a' });
    assert.equal(calls[0].name, 'song_url_v1');
    assert.equal(calls[0].params.level, 'standard');
    assert.equal(calls[0].params.unblock, 'false');
    assert.equal(calls[0].params.randomCNIP, 'false');
    assert.equal(calls[0].params.cookie, 'MUSIC_U=fixture-a');
    assert.equal(calls[0].params.source, undefined);
    await post('/recommend/songs', {}, { Authorization: 'MUSIC_U=fixture-b' });
    await post('/login/qr/key');
    assert.equal(calls[1].params.cookie, 'MUSIC_U=fixture-b');
    assert.equal(calls[2].params.cookie, '');
    const rejected = await post('/song/url/v1', { id: 'reject' });
    assert.equal(rejected.status, 401);
    const errorBody = await rejected.text();
    assert.ok(errorBody.includes('301'));
    assert.ok(!errorBody.includes('SECRET') && !errorBody.includes('cookie'));
    assert.equal((await post('/song/url/match', { id: '42' })).status, 404);
    assert.equal((await post('/recommend/songs', {}, { Origin: 'https://untrusted.example' })).status, 403);
    const invalid = await fetch(root + '/lyric', { method: 'POST',
      headers: { 'Content-Type': 'application/json' }, body: '{invalid' });
    assert.equal(invalid.status, 502);
    assert.equal(calls.length, 4, 'Rejected browser/unsupported/malformed requests must not reach the platform.');
    assert.equal(process.env.ENABLE_GENERAL_UNBLOCK, 'false');
    assert.equal(process.env.ENABLE_RANDOM_CN_IP, 'false');
    assert.equal(process.env.NETEASE_COOKIE, '');
  } finally {
    server.closeAllConnections();
    await new Promise(resolve => server.close(resolve));
  }
});
