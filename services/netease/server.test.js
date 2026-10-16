'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { createBridge } = require('./server');

test('library and catalog routes forward pagination and each caller account independently', async () => {
  const calls = [];
  const api = Object.fromEntries(['user_playlist', 'playlist_detail', 'song_detail', 'cloudsearch'].map(name => [name, async params => {
    calls.push({ name, params }); return { status: 200, body: { code: 200 } };
  }]));
  const server = createBridge(api);
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const root = `http://127.0.0.1:${server.address().port}`;
  try {
    for (const [route, body, cookie] of [
      ['/user/playlist', { uid: '42', offset: 100, limit: 100, cookie: 'wrong' }, 'MUSIC_U=account-a'],
      ['/playlist/detail', { id: '11' }, 'MUSIC_U=account-b'],
      ['/song/detail', { ids: '1,2,3' }, 'MUSIC_U=account-a'],
      ['/cloudsearch', { keywords: '初音未来', offset: 30, limit: 30, type: 1000 }, 'MUSIC_U=account-b'],
    ]) {
      const response = await fetch(root + route, { method: 'POST', headers: {
        'Content-Type': 'application/json', Authorization: cookie }, body: JSON.stringify(body) });
      assert.equal(response.status, 200);
      assert.equal(calls.at(-1).params.cookie, cookie);
    }
    assert.equal(calls[0].params.offset, 100);
    assert.equal(calls[0].params.uid, '42');
    assert.equal(calls[2].params.ids, '1,2,3');
    assert.equal(calls[3].params.keywords, '初音未来');
    assert.equal(calls[3].params.offset, 30);
    assert.equal(calls[3].params.type, undefined);
  } finally {
    server.closeAllConnections(); await new Promise(resolve => server.close(resolve));
  }
});

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
    await post('/song/url/v1', { id: '42', level: 'lossless', unblock: 'true', source: 'other',
      randomCNIP: 'true', cookie: 'INJECTED' }, { Authorization: 'MUSIC_U=fixture-a; os=android' });
    assert.equal(calls[0].name, 'song_url_v1');
    assert.equal(calls[0].params.level, 'lossless');
    assert.equal(calls[0].params.crypto, 'eapi');
    assert.equal(calls[0].params.unblock, 'false');
    assert.equal(calls[0].params.randomCNIP, 'false');
    assert.equal(calls[0].params.cookie, 'MUSIC_U=fixture-a; os=pc');
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

test('playback uses supported EAPI without external XEAPI keys and forwards all allowed qualities', async () => {
  const calls = [];
  const server = createBridge({song_url_v1: async params => {
    calls.push(params);
    if (params.id === 'fail') throw new Error('xeapi public key is missing');
    return {status:200, body:{code:200, data:[]}};
  }});
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const root = 'http://127.0.0.1:' + server.address().port;
  const post = body => fetch(root + '/song/url/v1', {method:'POST',
    headers:{'Content-Type':'application/json', Authorization:'MUSIC_U=fixture'}, body:JSON.stringify(body)});
  try {
    assert.equal((await fetch(root + '/health').then(r => r.json())).protocol, 3);
    for (const level of ['standard', 'exhigh', 'lossless']) {
      assert.equal((await post({id:'42', level, crypto:'xeapi'})).status, 200);
      assert.equal(calls.at(-1).crypto, 'eapi');
      assert.equal(calls.at(-1).level, level);
    }
    assert.equal((await post({id:'42', level:'invalid'})).status, 400);
    assert.equal(calls.length, 3);
    await post({id:'42'});
    assert.equal(calls.at(-1).level, 'standard');
    const failure = await post({id:'fail'});
    assert.equal(failure.status, 502);
    const body = await failure.json();
    assert.equal(body.reason, 'playback_protocol_unavailable');
    assert.ok(!JSON.stringify(body).includes('MUSIC_U'));
  } finally { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
});
