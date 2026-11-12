// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Tommy McLeroy. Part of sf3comp; from cps2recomp's online/.
// A loopback-only, in-memory service for local integration tests. Never a public deployment.
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
const source = await readFile(new URL('./src/index.js', import.meta.url), 'utf8');
const mod = await import('data:text/javascript;base64,' + Buffer.from(source).toString('base64'));
const env = {};
for (const [name, type] of [['MATCHES','Match'], ['LOBBIES','Lobby'], ['ROOMS','Room'], ['STATS','Stats']]) {
  const instances = new Map();
  env[name] = { idFromName: n => n, get(id) {
    if (!instances.has(id)) {
      const values = new Map();
      const storage = { get: async k => structuredClone(values.get(k)), put: async (k,v) => values.set(k, structuredClone(v)),
        delete: async k => values.delete(k), deleteAll: async () => values.clear(), setAlarm: async () => {},
        list: async () => new Map(values) };
      instances.set(id, new mod[type]({ storage }, env));
    }
    return { fetch: (url, init) => instances.get(id).fetch(new Request(url, init)) };
  }};
}
const port = Number(process.env.PORT || 8787);
createServer(async (req,res) => {
  try {
    const chunks = [];
    for await (const chunk of req) chunks.push(chunk);
    const request = new Request(`http://127.0.0.1:${port}${req.url}`, { method:req.method,
      ...(req.method !== 'GET' && req.method !== 'HEAD' ? { body:Buffer.concat(chunks) } : {}) });
    const response = await mod.default.fetch(request, env, { waitUntil: p => p.catch(console.error) });
    res.writeHead(response.status, Object.fromEntries(response.headers));
    res.end(Buffer.from(await response.arrayBuffer()));
  } catch (e) { console.error(e); res.writeHead(500); res.end('local test service failed'); }
}).listen(port, '127.0.0.1', () => console.log(`Local spectator test service: http://127.0.0.1:${port}`));
