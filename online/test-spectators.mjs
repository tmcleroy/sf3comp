// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of sf3comp; from cps2recomp's online/.
// Run with node online/test-spectators.mjs; no deployed service or game files needed.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
const source = await readFile(new URL('./src/index.js', import.meta.url), 'utf8');
const { Match, Lobby } = await import('data:text/javascript;base64,' + Buffer.from(source).toString('base64'));
const data = new Map();
const storage = { get: async k => structuredClone(data.get(k)), put: async (k,v) => data.set(k, structuredClone(v)),
  deleteAll: async () => data.clear(), setAlarm: async () => {} };
const lobby = new Lobby({});
const stub = handler => ({ idFromName: x => x, get: () => ({ fetch: handler }) });
const env = { LOBBIES: stub((url, init) => lobby.fetch(new Request(url, init))),
  STATS: stub(async () => new Response('{}')), ROOMS: stub(async () => new Response('{}')) };
const match = new Match({ storage }, env);
const request = (path, body) => match.fetch(new Request('https://match/' + path,
  body ? { method:'POST', body: JSON.stringify(body) } : undefined));
const info = { game:'example', build:'123', version:'test', protocol:8 };
const host = await (await request('host?code=ABC234', { ...info, listed:true, spectators:true, name:'Host', addresses:['127.0.0.1:7845'] })).json();
const viewer = { ...info, addresses:['127.0.0.1:9000'] };
assert.equal((await request('watch', viewer)).status, 409, 'waiting hosts are not watchable');
assert.equal((await request('join', { ...info, addresses:['127.0.0.1:8000'] })).status, 200);
assert.equal((await request('poll?secret=wrong&active=1')).status, 404);
match.told = 0;
await request('poll?secret=' + host.secret + '&active=1');
const list = await (await lobby.fetch(new Request('https://lobby/list?protocol=8'))).json();
assert.equal(list.matches.length, 1);
assert.equal(list.matches[0].watching, true);
assert.equal(JSON.stringify(list).includes('127.0.0.1'), false, 'lobby never exposes peer addresses');
assert.equal((await request('watch', { ...viewer, build:'different' })).status, 409);
assert.equal((await request('watch', { ...viewer, addresses:[] })).status, 400);
const first = await (await request('watch', viewer)).json();
assert.ok(first.token);
assert.deepEqual(first.addresses, ['127.0.0.1:7845']);
assert.equal((await (await request('watch', { ...viewer, token:first.token })).json()).token, first.token);
for (let n=1; n<8; n++) assert.equal((await request('watch', viewer)).status, 200);
assert.equal((await request('watch', viewer)).status, 429, 'bounded viewer admission');
const poll = await (await request('poll?secret=' + host.secret + '&active=1')).json();
assert.equal(poll.viewers.length, 8);
assert.deepEqual(poll.addresses, ['127.0.0.1:8000'], 'watching cannot replace player 2');
assert.equal((await request('join', viewer)).status, 409);
await request('poll?secret=' + host.secret);
assert.equal((await request('watch', viewer)).status, 409, 'inactive matches refuse viewers');
// A player who joined the host by UDP address never calls the service's /join route.
const direct = await (await request('host?code=ABC234', { ...info, secret:host.secret,
  listed:true, spectators:true, addresses:['127.0.0.1:7845'] })).json();
match.told = 0;
await request('poll?secret=' + direct.secret + '&active=1');
const directList = await (await lobby.fetch(new Request('https://lobby/list?protocol=8'))).json();
assert.equal(directList.matches[0].watching, true, 'direct-address player still produces a Watch listing');
assert.equal((await request('watch', viewer)).status, 200);
assert.equal((await request('join', viewer)).status, 409, 'active direct match cannot accept another player');
console.log('ok: watchable lobby, host authentication, build checks, bounded viewers, and player isolation');
