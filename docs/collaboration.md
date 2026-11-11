# Kaint collaboration protocol (kaint-collab v1)

Kaint (desktop) and KaintHTML (web) share one protocol so that both can join the same session.

## Roles and transport

- One **server** per session, reachable on the local network over **WebSocket** (`ws://address:47913/`).
  - Kaint (desktop) is a server itself when it hosts: its user is the **host** (participant `0`).
  - A browser can't open a server, so web hosts run the small relay in
    `KaintHTML/tools/collab-server.mjs` (`node tools/collab-server.mjs`) and connect to it as the host.
  - Both servers also serve the KaintHTML files over plain HTTP when they have them, so browsers on
    the network open `http://address:47913/` (a page loaded over HTTPS can't open `ws://` to the LAN).
    The `index.html` they serve carries `<meta name="kaint-collab" content="1">`; the page then reads
    `/kaint-collab.json` (`{kaint-collab, server, hosted, session, host, auth, port}`) and offers to join.
  - Desktop packages install KaintHTML in `share/kaint/KaintHTML` (CMake option `KAINT_INSTALL_WEB`);
    `KAINT_WEB_ROOT` points Kaint to another copy.
- The **host** owns the canonical image (only the host saves it; guests can export a copy).
- Desktop hosts announce themselves on the LAN with a UDP beacon to port **47914** every 2 s:
  `{"kaint-collab":1,"name":…,"host":…,"port":…,"auth":…,"app":…}`.
- All messages are JSON text frames with a type field `t`. Images travel as base64 PNG (straight alpha,
  the layers' own format on both sides).

## Joining

1. server → client: `hello {v, session, host, auth, nonce, server}`
   - `auth`: `free` | `approve` (the host admits each one) | `password` | `ids` (only the devices the
     host listed).
   - `nonce`: base64 of 16 random bytes.
2. client → server: `join {v, name, color?, platform: "desktop"|"web", app, device, proof?}`
   - `device`: first 16 hex digits of SHA-256 of this device's ID code (the code itself never travels).
   - `proof`: base64 HMAC-SHA256 of the raw nonce bytes, keyed with the password (`password`) or the
     device's ID code without dashes (`ids` and `approve`: listed devices skip the approval). The host
     compares it with the codes it was given. Browsers compute it in plain JS (`crypto.subtle` doesn't
     exist on `http://` LAN pages).
   - The relay's host joins with `role: "host"`, its `config` and, from another machine, the
     relay's `hostKey`.
3. server → client: `waiting` (while the host decides, `approve`), `denied {reason}` or
   `joined {you, color, mode}`. Reasons: `password`, `device`, `rejected`, `version`, and from the relay
   `nohost`, `busy`, `hostkey`.
4. host → new guest: `welcome {mode, autoAccept, peers, doc}` (the full image).

`peer {op: "add"|"update"|"remove", peer}` keeps everybody's list up to date. A peer is
`{id, name, color, platform, app, host, mode}`.

## Editing

- **live** (real time): only between the same app (desktop–desktop, web–web) when the host allows it.
  Every finished step of a live guest goes to the host as `change {label, ops}`; the host applies it and
  sends `commit` to everybody.
- **confirm** (default, and always between desktop and web): the guest edits a private draft with its own
  history and sends it with `propose {pid, label, ops}`. The host accepts or rejects it (or accepts all
  automatically if it chose so) and answers `proposal {pid, state}`; accepted drafts become a `commit`.
  The guest's draft history is cleared once it is accepted.
- The host's own steps (and the guests' changes it applies, shown in its history as
  "Cambio hecho por …") reach everybody as `commit {rev, from, name, label, ops}`.

Ops, applied in order:

- `{op: "pixels", layer, x, y, w, h, png, mask?}`: the changed pixels of a layer region. `mask` is
  base64 of one bit per pixel of the region (row by row, least significant bit first); only those
  pixels are written, so two people painting different parts of a layer don't erase each other.
  Without `mask`, the whole region.
- `{op: "layers", layers: [{id, name, visible, opacity, blend, png?}]}`: the whole ordered layer list
  (bottom first) with their properties; layers that aren't listed are deleted; new layers carry their
  full-size `png`. Blend modes: `normal multiply additive colorburn colordodge reflect glow overlay
  difference negation lighten darken screen xor`.
- `{op: "doc", doc}`: the whole image (a new size, for example).

A snapshot (`doc`) is `{rev, w, h, dpi, name, layers: [{id, name, visible, opacity, blend, png}]}`.

To avoid interfering with each other, changes that arrive while a participant is still working on
the same layer (a stroke, a shape or a text being edited) wait until that operation ends, and on a
guest the pixels it changed itself and hasn't sent yet are kept.

## Other messages

- `cursor {x, y, tool, inside}`: where each participant's pointer is (image coordinates) and which tool
  it uses; relayed to everybody (about 30 per second at most).
- `rename {name}`: a participant changes its display name.
- host → server: `admit {peer, ok}`, `kick {peer}`; host → guests: `settings {mode, autoAccept}`.
- server → all: `end {reason}` when the session finishes (`closed`, or `kicked` for one guest).

## The relay (`KaintHTML/tools/collab-server.mjs`)

- Routes between the web host and the guests; it applies the access rules itself (the host sends them
  in `join.config` and later `config {config}`: `name, session, access, password, allowedIds, realtime,
  autoAccept, announce`).
- relay → host: the guests' `change`, `propose`, `cursor` (with `from`), `peer` events and
  `request {peer}` for guests waiting for approval (answered with `admit`).
- host → relay: a message with `to: id` goes to that guest only (`welcome`, `proposal`); `admit`, `kick`,
  `config` and `end` are for the relay; anything else (`commit`, `cursor`) goes to every guest.
- Guests on the same platform as the host (web) are `live` when `realtime` is on; desktop guests of a
  web host use confirmations.

## Applying changes

- Every participant keeps the canonical copy and compares its document with it after each history step.
- A live guest's own commits come back as an echo: while newer own changes are still in flight, the
  echo only updates the canonical copy (the document is already ahead).
- Remote steps appear in each participant's history as "Cambio hecho por …"; switching layer or tool
  because of them is suppressed.
