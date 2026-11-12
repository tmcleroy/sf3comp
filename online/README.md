# The online service

Online play (`cps3recomp/port/netplay.h`) is two copies of the game sending each other their players' controls directly. This
service only introduces them: a host asks it for a code; the other player gives the code, and each is told where the
other can be reached. Nothing of a match goes through it. It is a [Cloudflare Worker](https://developers.cloudflare.com/workers/)
with a [Durable Object](https://developers.cloudflare.com/durable-objects/) for each match (`src/index.js`), within
Cloudflare's free plan.

How the two meet:

1. Each game asks a public STUN server (Cloudflare's, or Google's) what address and port its UDP socket is seen from
   on the internet: the router's, for a machine behind one.
2. The host sends that and its addresses on its own networks to `POST /v1/host`, and is given a code (`K7Q-M2X`,
   six letters and digits; no 0, O, 1, I or L). It asks `GET /v1/host/CODE` every 2 seconds, every 5 after two
   minutes and every 10 after ten (the joiner waits 30 seconds to hear from it).
3. The other player sends the code and its own addresses to `POST /v1/join/CODE`, and is given the host's. The host's
   next question gives it theirs. A code lasts 30 minutes (a host still waiting then has the same code again), and can
   be joined once; a code for another game, or for
   another build of the game, is turned away.
4. Each side sends to every address of the other's. The host's packets open its router to the other's, whose hellos
   then come through (UDP hole punching); the first address that answers is the match's.

A host that asks for it (`--net-list`, with `--net-name`) is also in the **lobby**: a Durable Object keeping every
game's listed matches whose host has asked about them in the last 25 seconds, until someone joins, in memory only
(a match tells it at most every 10 seconds).
`GET /v1/lobby?protocol=N` lists them all, each with its game (the launcher's Online page); `GET /v1/lobby/GAME?protocol=N`
one game's (a game page's Online card).

The **waiting room** is the players whose launcher has its Online page open: a Durable Object (Room) they check in at
every 10 seconds, or 20 while the launcher isn't the window in front (`POST /v1/room`, with a name, the games they
have and whether they can be invited; with `lobby: true` the answer has every game's open matches too, so the Online
page asks once), gone 30 seconds after the last check-in, and kept in memory only (a room let go is made again by the next check-ins). Each is
known by an id the service makes from a secret only their launcher has, and shown by name only. A host can name one
to invite (`invite` and `from` in `POST /v1/host`): the Match gives the invitation to the room while the host asks
about its match, and takes it back when someone joins; a player declines with `POST /v1/room/decline`, which the
host's next question says (`invite: "declined"`, or `"away"` if it couldn't be given). Players block each other with
ids their launchers keep, sent with each check-in.

Two routers that each give every destination a port of its own ("symmetric" NAT, as some mobile networks do) can't be
met this way; a relay would be needed, which the service doesn't have yet. Then the host's address, with its port
forwarded, or a VPN such as Tailscale, still works.

**Stats** for whoever runs the service: the Stats Durable Object counts matches hosted and joined per UTC day, by
game (90 days kept), and knows the hosts asking about a match in the last 30 seconds; nothing about who. `GET
/v1/stats?key=KEY` shows them, with how many have the Online page open, if the Worker has the secret `STATS_KEY`
(without it the page is closed).

## Running it

### Spectator development

The spectator protocol adds `POST /v1/watch/CODE`. Only a live match whose host opted in is watchable.
The viewer supplies its game, build, version, protocol and addresses and receives the host's addresses and
an admission token. Authenticated host polls with `active=1` keep the running lobby entry alive and deliver
viewer addresses and tokens for UDP introduction. Ordinary joins still consume only player 2's slot.
The lobby says `watching: true` for a running match and never publishes addresses or tokens.

Run `node online/test-spectators.mjs` for service tests. `node online/dev-service.mjs` starts a loopback-only
in-memory development server; it is not a replacement for a production deployment. With it running, set
`SERVICE=http://127.0.0.1:8787` and run `python tools/check_spectators.py` from the repository root after
building Marvel vs. Capcom. With no SERVICE, that test connects by local address instead. It checks a late
viewer against confirmed hashes and verifies that viewer input and departure do not affect the players.

Watch from the lobby needs this service deployed (below); a copy of the game on an older service still hosts and
joins as before.

```sh
cd online
npx wrangler login     # once: Cloudflare's tool, signed in to the account in a browser
npx wrangler deploy    # https://sf3comp-online.<the account's subdomain>.workers.dev
npx wrangler secret put STATS_KEY   # once, for the stats page
npx wrangler dev       # or on this machine, at http://localhost:8787, for tests
```

The games ask `Netplay::kService` (`cps3recomp/port/netplay.h`) unless started with `--net-service URL` (or `off`: no codes,
addresses only).
