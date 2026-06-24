# Vanilla 1.12.1 (build 5875) online flow: logon to seeing the world

Research note for WorldForge. Read-only survey. Sources are cross-checked between
wowdev.wiki, GTKer's WoW-message docs/SRP6 guide, and the CMaNGOS `mangos-classic`
(vanilla) source. TrinityCore (3.3.5) is cited only as an analogue where the vanilla
shape differs.

---

## 1. Summary

This note documents the complete end-to-end network flow a real 1.12.1 client performs
to go from "launcher login" to "standing in the world and seeing entities move", and maps
it onto what WorldForge already has (`src/srp6`, `src/crypto`, `src/worldproto`,
`src/server/*`) plus what is missing (TCP transport, a logon/realm client, an auth-session
builder, and an `SMSG_UPDATE_OBJECT`/`SMSG_MONSTER_MOVE` decoder that feeds the offline
renderer / `WorldSim`).

Why it matters for WorldForge: the project already renders terrain, doodads and WMOs
offline and already has the two hardest crypto pieces correct (SRP6 with both roles, and
the vanilla rolling header cipher in `WorldHeaderCrypt`). The remaining work is mostly
*plumbing and packet (de)serialization*, which can be validated against the bundled stub
server (`StubBridgeServer` + `WorldSim`) before ever touching a real CMaNGOS realm. The
goal is a `LiveWorldClient` that decodes object/movement opcodes into `SimObject`-shaped
state and hands it to the existing scene/`world_view` so the engine can spawn and animate
real server entities on top of the already-rendered map.

There are **two separate TCP connections / two protocols**:

1. **Logon server (realmd / authserver), default port 3724** — speaks the *auth protocol*
   (1-byte opcodes, little-endian, no header cipher). Does SRP6, returns the realm list.
2. **World server (mangosd / worldserver), port from the realm list (commonly 8085)** —
   speaks the *world protocol* (2/4/6-byte framed headers, header cipher seeded from the
   SRP6 session key `K`). Everything in `src/worldproto.hpp` is about this second link.

---

## 2. Concrete format/protocol facts (vanilla build 5875)

### 2.1 Auth (logon) protocol — port 3724, NOT header-encrypted

Opcodes are a single leading byte (`enum eAuthCmd`, CMaNGOS `realmd/AuthCodes.h`):

| Name | Value |
|---|---|
| `CMD_AUTH_LOGON_CHALLENGE`     | `0x00` |
| `CMD_AUTH_LOGON_PROOF`         | `0x01` |
| `CMD_AUTH_RECONNECT_CHALLENGE` | `0x02` |
| `CMD_AUTH_RECONNECT_PROOF`     | `0x03` |
| `CMD_REALM_LIST`               | `0x10` |
| `CMD_XFER_INITIATE`            | `0x30` |
| `CMD_XFER_DATA`                | `0x31` |

`AuthLogonResult` (the result byte): `WOW_SUCCESS = 0x00`, `…_BANNED = 0x03`,
`…_UNKNOWN_ACCOUNT = 0x04`, `…_INCORRECT_PASSWORD = 0x05`, `…_ALREADY_ONLINE = 0x06`,
`…_VERSION_INVALID = 0x09`, `…_VERSION_UPDATE = 0x0A`, `…_SUSPENDED = 0x0C`.
(Source: CMaNGOS `src/realmd/AuthCodes.h`; wowdev.wiki *Login*.)

**CMD_AUTH_LOGON_CHALLENGE (client → server).** All multi-byte fields little-endian
unless noted. Layout (CMaNGOS `sAuthLogonChallenge_C` / wowdev
*CMD_AUTH_LOGON_CHALLENGE_Client*):

```
u8   cmd            = 0x00
u8   error          = 0x03   (protocol version constant)
u16  size           = length of the remaining body (LE)
u8   gamename[4]    = "WoW\0"
u8   version1       = 1
u8   version2       = 12
u8   version3       = 1
u16  build          = 5875        (LE: 0xF3 0x16)
u8   platform[4]    = "68x\0"     (i.e. "x86" reversed + nul)
u8   os[4]          = "niW\0"     ("Win" reversed + nul)
u8   country[4]     = "BGne"      ("enGB" reversed) or "SUne" ("enUS")
u32  timezone_bias
u32  ip             (client IP, big-endian per wowdev)
u8   I_len          (account name length)
u8   I[I_len]       (account name, UPPERCASE, ASCII, NOT nul-terminated)
```
Note the `gamename`/`platform`/`os`/`country` 4-byte tags are stored **reversed**
because the client writes them as a little-endian 4-char code. Account name max 16 chars.

**CMD_AUTH_LOGON_CHALLENGE (server → client)** (wowdev *…_Server*, CMaNGOS
`_HandleLogonChallenge`):
```
u8   cmd   = 0x00
u8   error = 0x00
u8   result            (WOW_SUCCESS = 0x00; on failure only cmd+error+result are sent)
u8   B[32]             (server public ephemeral, LE)
u8   g_len = 1
u8   g     = 7
u8   N_len = 32
u8   N[32]             (the safe prime, LE)
u8   s[32]             (salt, LE)
u8   crc_salt[16]      ("version challenge", unused by emus -> zeros ok)
u8   security_flags    (0x00 normally; bits 0x01 PIN / 0x02 matrix / 0x04 authenticator)
```

**CMD_AUTH_LOGON_PROOF (client → server)**:
```
u8   cmd   = 0x01
u8   A[32]             (client public ephemeral, LE)
u8   M1[20]            (client proof)
u8   crc_hash[20]      (client integrity hash; emus ignore)
u8   number_of_keys = 0
u8   security_flags = 0
```

**CMD_AUTH_LOGON_PROOF (server → client)**:
```
u8   cmd   = 0x01
u8   error = 0x00      (WOW_SUCCESS)
u8   M2[20]            (server proof)
u32  account_flags = 0
u32  survey_id = 0     (these two are TBC+; vanilla 1.12 server sends only cmd+error+M2,
                        then optionally a u16 unk. Validate against the live server.)
```

**CMD_REALM_LIST (client → server)**: `u8 cmd=0x10` then `u32 = 0` (padding).

**CMD_REALM_LIST (server → client)** (CMaNGOS `_HandleRealmList`, 1.12 branch):
```
u8   cmd = 0x10
u16  packet_size            (size of everything after this field, LE)
u32  unused = 0
u8   number_of_realms       (vanilla: u8; 2.4.3+: u16 — this is a key version split)
repeat per realm:
    u32   realm_icon        (0=normal,1=PvP,6=RP,8=RP-PvP)
    u8    realm_flags       (0x01 invalid, 0x02 offline, 0x04 specify-build…)
    cstr  name              (nul-terminated)
    cstr  address           ("host:port", e.g. "127.0.0.1:8085")
    f32   population
    u8    number_of_characters   (on this realm for this account)
    u8    category/timezone
    u8    realm_id
u16  unused = 0
```

### 2.2 SRP6 (the math behind the two challenge/proof exchanges)

Already implemented in `src/srp6` + `src/crypto`. Confirmed values (GTKer SRP6 guide;
matches `src/srp6.hpp` header comment):

- `N = 0x894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7` (big-endian), 32 bytes.
- `g = 7`, `k = 3`, SHA-1, all numbers serialized **little-endian** on the wire.
- `x = SHA1( s | SHA1( UPPER(USER) | ":" | UPPER(PASS) ) )`, interpreted LE.
- verifier `v = g^x mod N`.
- `u = SHA1(A | B)`; client `S = (B - k·g^x)^(a + u·x) mod N`.
- Session key `K` (40 bytes) = **interleaved SHA1** of `S`: strip trailing zero byte-pair,
  split into even/odd byte streams, SHA1 each, interleave the two 20-byte digests.
- `M1 = SHA1( (SHA1(N) xor SHA1(g)) | SHA1(USER) | s | A | B | K )`.
- `M2 = SHA1( A | M1 | K )`.

This `K` is reused verbatim as the world header-cipher key (next section) and as the
session-key input to the world auth digest.

### 2.3 World protocol framing and the header cipher

Already in `src/worldproto.hpp` and confirmed correct against wowdev *World Packet* +
GTKer:

- **Server→client header (SMSG): 4 bytes** = `u16 size` (big-endian) + `u16 opcode` (LE).
- **Client→server header (CMSG): 6 bytes** = `u16 size` (big-endian) + `u32 opcode` (LE).
- `size` counts the opcode bytes + body, **not** the size field itself.
- The **rolling add/xor header cipher** (NOT RC4 — RC4/HMAC is TBC 2.x+):
  - encrypt byte `i`: `E = (x ^ K[i % 40]) + L`, then `L = E` (L = previous encrypted byte).
  - decrypt byte `i`: `x = (E - L) ^ K[i % 40]`, then `L = E`.
  - Send and receive directions keep **independent** index and `L`. This is exactly what
    `WorldForge`'s `WorldHeaderCrypt::encryptSend`/`decryptRecv` already do.
- Only the **header** is enciphered; the body is plaintext.
- `SMSG_AUTH_CHALLENGE` and `CMSG_AUTH_SESSION` themselves are sent **before** the cipher
  is keyed, so those two headers are plaintext; the cipher engages for every packet after.

### 2.4 World auth handshake

Opcodes (CMaNGOS `Opcodes.cpp`; already in `worldproto.hpp`):
`SMSG_AUTH_CHALLENGE = 0x1EC`, `CMSG_AUTH_SESSION = 0x1ED`, `SMSG_AUTH_RESPONSE = 0x1EE`.

**SMSG_AUTH_CHALLENGE (server → client)** — body is just `u32 server_seed` (random).
(CMaNGOS `WorldSocket::SendAuthChallenge`: `WorldPacket packet(SMSG_AUTH_CHALLENGE, 4); packet << m_seed;`.)

**CMSG_AUTH_SESSION (client → server)** — vanilla field order (GTKer `cmsg_auth_session`,
CMaNGOS `HandleAuthSession`):
```
u32   build = 5875
u32   server_id (a.k.a. unk2 / login-server-id; 0)
cstr  username (UPPERCASE account name)
u32   client_seed   (random, chosen by client)
u8    digest[20]    (the auth proof)
... addon_info (zlib-compressed addon list; emus tolerate empty)
```
The **digest** is (CMaNGOS, confirmed):
```
SHA1( username | u32(0) | client_seed | server_seed | K )
```
where `username` is the raw account-name bytes, `u32(0)` is a literal zero dword (`t`),
`server_seed` is the value from `SMSG_AUTH_CHALLENGE`, and `K` is the 40-byte SRP6 session
key fed in as a big-number. After verifying, the server calls `m_crypt.Init(&K)` — i.e.
**both sides key the header cipher from `K` here**.

**SMSG_AUTH_RESPONSE (server → client)** — `u8 result` (`AUTH_OK = 0x0C`), and on OK the
server also appends billing/queue fields (`u32 billingTimeRemaining, u8 billingFlags,
u32 billingTimeRested`). On queue it sends `AUTH_WAIT_QUEUE = 0x1B` + `u32 position`.
(CMaNGOS `WorldSession::SendAuthOk`.)

### 2.5 Entering the world — opcode sequence

Relevant opcode values (CMaNGOS `Opcodes.cpp`, confirmed):

| Name | Value |
|---|---|
| `CMSG_CHAR_ENUM`               | `0x037` |
| `SMSG_CHAR_ENUM`               | `0x03B` |
| `CMSG_PLAYER_LOGIN`            | `0x03D` |
| `SMSG_LOGIN_VERIFY_WORLD`      | `0x236` |
| `SMSG_TUTORIAL_FLAGS`          | `0x0FD` |
| `SMSG_ACCOUNT_DATA_TIMES`      | `0x209` |
| `SMSG_INITIAL_SPELLS`          | `0x12A` |
| `SMSG_BINDPOINTUPDATE`         | `0x155` |
| `SMSG_SET_REST_START`          | `0x21E` |
| `SMSG_UPDATE_OBJECT`           | `0x0A9` |
| `SMSG_COMPRESSED_UPDATE_OBJECT`| `0x1F6` |
| `SMSG_MONSTER_MOVE`            | `0x0DD` |
| `SMSG_DESTROY_OBJECT`          | `0x0AA` |
| `CMSG_PING` / `SMSG_PONG`      | `0x1DC` / `0x1DD` |

Flow:
1. Client sends `CMSG_CHAR_ENUM` (empty body).
2. Server replies `SMSG_CHAR_ENUM`: `u8 count`, then per character: `u64 guid`,
   `cstr name`, `u8 race, class, gender, skin, face, hairStyle, hairColor, facialHair,
   level`, `u32 zone, map`, `f32 x, y, z`, `u32 guildId`, `u32 charFlags`,
   `u8 firstLogin`, `u32 petDisplayId, petLevel, petFamily`, then 19 equipment slots of
   `{u32 displayId; u8 inventoryType; u32 enchantAuraId}` (vanilla has no bag/cloak extras
   the later versions add). Validate slot count/order against the live `SMSG_CHAR_ENUM`.
3. Client sends `CMSG_PLAYER_LOGIN`: body is `u64 guid` of the chosen character.
4. Server streams the world-entry burst, roughly in this order (CMaNGOS
   `Player::SendInitialPacketsBeforeAddToMap` / `…AfterAddToMap`):
   `SMSG_LOGIN_VERIFY_WORLD` (`u32 map; f32 x; f32 y; f32 z; f32 orientation`),
   `SMSG_ACCOUNT_DATA_TIMES`, `SMSG_SET_REST_START`, `SMSG_BINDPOINTUPDATE`,
   `SMSG_TUTORIAL_FLAGS` (8×u32), `SMSG_INITIAL_SPELLS`, action buttons, factions,
   then **`SMSG_UPDATE_OBJECT`** creating the player object, followed by further
   `SMSG_UPDATE_OBJECT` / `SMSG_COMPRESSED_UPDATE_OBJECT` for nearby entities and
   `SMSG_MONSTER_MOVE` for active splines. The player is "in the world" after
   `LOGIN_VERIFY_WORLD` + the self `UPDATE_OBJECT`.
5. The client must answer `CMSG_PING` ↔ `SMSG_PONG` (`u32 ping_id; u32 latency` /
   `u32 ping_id`) periodically or the server drops it. Vanilla 1.12 has **no**
   `SMSG_TIME_SYNC_REQ` (that is WotLK 3.x) — important: don't expect/require it.

### 2.6 SMSG_UPDATE_OBJECT body (vanilla)

Body (wowdev *SMSG_UPDATE_OBJECT*, GTKer, CMaNGOS `UpdateData::BuildPacket` /
`Object::BuildCreateUpdateBlockForPlayer`):
```
u32  count                 (number of object blocks)
u8   has_transport         (vanilla only; bool, usually 0)
repeat count times:
    u8   update_type        (0 VALUES, 1 MOVEMENT, 2 CREATE_OBJECT, 3 CREATE_OBJECT2,
                             4 OUT_OF_RANGE_OBJECTS, 5 NEAR_OBJECTS)
    ... per-type payload
```

- `OUT_OF_RANGE_OBJECTS (4)` / `NEAR_OBJECTS (5)`: `u32 guid_count` then that many
  **packed GUIDs**.
- `VALUES (0)`: packed GUID + values-update block.
- `MOVEMENT (1)`: packed GUID + MovementBlock.
- `CREATE_OBJECT (2)` / `CREATE_OBJECT2 (3)`: packed GUID + `u8 object_type`
  (0 OBJECT,1 ITEM,2 CONTAINER,3 UNIT,4 PLAYER,5 GAMEOBJECT,6 DYNAMICOBJECT,7 CORPSE) +
  **MovementBlock** + values-update block.

**Packed GUID** (wowdev *Packed_GUID*): `u8 mask` then, for each set bit i (LSB first),
the i-th byte of the 8-byte little-endian GUID. Zero bytes are omitted.

**MovementBlock** (vanilla):
```
u8   update_flags          (UPDATEFLAG_*: 0x01 SELF, 0x02 TRANSPORT, 0x08 HAS_TARGET(?),
                            0x10 LOWGUID, 0x20 LIVING, 0x40 HAS_POSITION ...)
if (update_flags & LIVING 0x20):
    u32  movement_flags     (MOVEFLAG_*; 0 = standing)
    u32  time               (ms timestamp)
    f32  x, y, z
    f32  orientation
    (if MOVEFLAG_ONTRANSPORT) transport guid+offset+time
    (if MOVEFLAG_SWIMMING)    f32 pitch
    f32  fall_time
    (if MOVEFLAG_JUMPING)     f32 jump z-speed, cos/sin angle, xy-speed
    (if MOVEFLAG_SPLINE_ELEVATION) f32 spline_elevation
    f32  walk_speed, run_speed, run_back_speed, swim_speed, swim_back_speed, turn_rate
    (if MOVEFLAG_SPLINE_ENABLED) full spline block
else if (update_flags & HAS_POSITION 0x40):   // non-living (gameobjects)
    f32  x, y, z, orientation
if (update_flags & LOWGUID 0x10): u32 low_guid (or the object's first value)
if (update_flags & HAS_TARGET 0x04): packed GUID victim
... (a couple of TransportTime / vehicle fields are TBC+, absent in vanilla)
```

**Values-update block** (the UpdateMask):
```
u8   blocks_count           (number of u32 mask words)
u32  mask[blocks_count]     (bit i set => field i present)
u32  values[...]            (one u32 per set bit, in ascending field order)
```
Field indices come from the vanilla `UpdateFields.h` (e.g. `OBJECT_FIELD_GUID = 0x00`,
`OBJECT_FIELD_ENTRY = 0x03`, `OBJECT_FIELD_SCALE_X = 0x04`, `UNIT_FIELD_HEALTH`,
`UNIT_FIELD_DISPLAYID`, `UNIT_FIELD_LEVEL`, `UNIT_FIELD_FACTIONTEMPLATE`, etc.).
For WorldForge the minimum useful fields are `OBJECT_FIELD_GUID`, `OBJECT_FIELD_ENTRY`,
`OBJECT_FIELD_TYPE`, `OBJECT_FIELD_SCALE_X`, and `UNIT_FIELD_DISPLAYID` (the model id to
render). Full field tables are in CMaNGOS `src/game/Object/UpdateFields.h` (vanilla).

**SMSG_COMPRESSED_UPDATE_OBJECT (0x1F6)**: `u32 uncompressed_size` then a raw **zlib**
(deflate) stream that decompresses to a normal `SMSG_UPDATE_OBJECT` body. WorldForge needs
a zlib inflate here (the only compression in the entry path).

### 2.7 SMSG_MONSTER_MOVE body (vanilla)

(GTKer `smsg_monster_move`, wowdev, CMaNGOS spline code):
```
PackedGUID guid
f32        spline_point_x, y, z      (start point)
u32        spline_id
u8         move_type                 (0 NORMAL, 1 STOP, 2 FACING_SPOT(f32 x,y,z),
                                       3 FACING_TARGET(u64 guid), 4 FACING_ANGLE(f32))
u32        spline_flags
u32        duration                  (ms for the whole path)
u32        number_of_splines
f32[3] × number_of_splines           (the waypoints; vanilla sends full Vector3 points,
                                       not the packed mid-point deltas that 3.3.5 uses)
```
For `move_type = STOP` the spline list is omitted. This is the opcode that animates a
creature smoothly between two `UPDATE_OBJECT` positions; WorldForge can lerp position over
`duration` along the points to drive `SimObject.pos`/`orientation`.

### 2.8 SMSG_DESTROY_OBJECT (0x0AA)

Vanilla body: `u64 guid` (CMaNGOS sends a plain non-packed `u64`; GTKer's "packed" note is
a discrepancy to verify on the wire — see Risks). Remove the object from the scene.

---

## 3. How other engines implement it

**CMaNGOS `mangos-classic` (the closest vanilla reference).**
- Logon: `src/realmd/AuthSocket.cpp` — `_HandleLogonChallenge`, `_HandleLogonProof`,
  `_HandleRealmList`; opcodes/results in `src/realmd/AuthCodes.h`. SRP6 math in
  `src/shared/Auth/Sha1.*` and the `Srp6`/`BigNumber` helpers.
- World link: `src/game/Server/WorldSocket.cpp` — `SendAuthChallenge` (sends `m_seed`),
  `HandleAuthSession` (reads `build, unk2(server_id), account, clientSeed, digest[20]`,
  recomputes `SHA1(account | t(0) | clientSeed | seed | K)`, then `m_crypt.Init(&K)`).
  The header cipher class is `AuthCrypt` (`src/shared/Auth/AuthCrypt.*`) — for vanilla it
  is the add/xor rolling cipher, not RC4.
- Object replication: `src/game/Object/Object.cpp`
  (`BuildCreateUpdateBlockForPlayer`, `BuildMovementUpdate`, `_BuildValuesUpdate`),
  `UpdateData.cpp` (`BuildPacket`, picks `SMSG_UPDATE_OBJECT` vs
  `SMSG_COMPRESSED_UPDATE_OBJECT` over a ~900-byte threshold and zlib-compresses),
  `UpdateMask.h`, `UpdateFields.h`. Movement/splines:
  `src/game/MotionGenerators/MoveSpline*` build `SMSG_MONSTER_MOVE`.
- World-entry burst: `Player::SendInitialPacketsBeforeAddToMap` /
  `SendInitialPacketsAfterAddToMap`, `HandlePlayerLoginOpcode`, `HandleCharEnumOpcode`.

**TrinityCore (3.3.5 analogue, use with care).** Same overall flow but several concrete
differences vs vanilla: RC4+HMAC header cipher (not add/xor); `SMSG_AUTH_CHALLENGE`
carries extra seeds; realm count is `u16`; `SMSG_MONSTER_MOVE` uses packed point deltas;
`SMSG_TIME_SYNC_REQ` exists. Good for understanding the *shape* of
`Object::BuildValuesUpdate`/`UpdateMask`, but do NOT copy 3.3.5 byte layouts.

**GTKer `wow_messages` / `wow_world_messages` (Rust).** Version-tagged, machine-checked
message definitions — the single best ground truth for vanilla wire layouts
(`docs/cmsg_auth_session.html`, `smsg_update_object.html`, `smsg_monster_move.html`, etc.)
and the SRP6 implementation guide. Cross-checks cleanly with CMaNGOS.

**Other clients to crib decoders from:** WoWPacketParser (TrinityCore tooling, but with a
vanilla profile), the WoWTools `SMSG_UPDATE_OBJECT.cs` parser (tomrus88), and the
chaodhib WoW Wireshark dissector (handles vanilla header decrypt + update-object parse —
very useful as an offline validation oracle).

---

## 4. Implementation plan for WorldForge (smallest-first)

Existing assets to build on: `src/srp6.*` (both roles done), `src/crypto.*` (SHA1 + BigUInt
done), `src/worldproto.hpp` (`WorldHeaderCrypt` + framing done, opcodes partly done),
`src/server/world_sim.*` + `src/server/stub_server.*` (an in-process world + TCP server to
test against), `src/byte_reader.hpp` / `src/byte_writer.hpp` (serialization),
`src/coords.hpp` (WoW↔engine coordinate transform for placing entities).

**Step 0 — TCP transport (new `src/net/tcp_socket.{hpp,cpp}`).** A thin blocking socket
(Winsock on Windows, BSD elsewhere) with `connect/send/recvN`. `StubBridgeServer` already
shows the platform socket usage to mirror. Validate: connect to `StubBridgeServer` and echo.

**Step 1 — Auth-protocol opcodes + structs (new `src/net/auth_protocol.hpp`).** Add
`eAuthCmd` and `AuthLogonResult` enums and the challenge/proof/realm-list struct
serializers (section 2.1). Pure header + `byte_writer`/`byte_reader`. Validate with a unit
test that round-trips the exact bytes in §2.1 against the wowdev examples.

**Step 2 — Logon client (new `src/net/logon_client.{hpp,cpp}`).** Drives:
`CMD_AUTH_LOGON_CHALLENGE → parse server challenge → feed B,N,g,s into the existing
`Srp6Client` → CMD_AUTH_LOGON_PROOF → verify M2 → CMD_REALM_LIST → parse realms`. Outputs
`{sessionKey K, vector<Realm>}`. Validate **offline** by pointing it at the existing SRP6
server role (`Srp6Server`) wrapped in a tiny test realmd loop (or extend `StubBridgeServer`
to answer auth opcodes); assert client and server derive identical `K` (the srp6 tests
already prove the math).

**Step 3 — World auth-session builder (extend `src/worldproto.hpp` /
new `src/net/world_auth.{hpp,cpp}`).** Implement `buildAuthSession(account, build=5875,
clientSeed, serverSeed, K)` computing `SHA1(account | u32(0) | clientSeed | serverSeed | K)`
with the existing `wf::sha1`, and the `SMSG_AUTH_CHALLENGE`/`SMSG_AUTH_RESPONSE` parsers.
Then key `WorldHeaderCrypt(K)` for both directions. Validate: in `StubBridgeServer`, add the
mirror `HandleAuthSession` and assert the digest matches and the first enciphered SMSG
header decrypts to a known opcode.

**Step 4 — World client + opcode pump (new `src/net/world_client.{hpp,cpp}`).** A
read-loop that: decrypts each 4-byte SMSG header via `WorldHeaderCrypt`, reads `size-2`
body bytes, dispatches by opcode. Send path enciphers the 6-byte CMSG header. Implement the
keep-alive `CMSG_PING`/`SMSG_PONG` and the entry handshake
(`CMSG_CHAR_ENUM` → parse `SMSG_CHAR_ENUM` → `CMSG_PLAYER_LOGIN` →
consume `SMSG_LOGIN_VERIFY_WORLD`). Validate against `StubBridgeServer` extended to emit
these.

**Step 5 — Update-object decoder (new `src/net/update_object.{hpp,cpp}`).** Parse
`SMSG_UPDATE_OBJECT` per §2.6: packed-GUID reader, the `update_type` switch, the
MovementBlock, and an UpdateMask reader that pulls only the fields WorldForge cares about
(`OBJECT_FIELD_ENTRY`, `OBJECT_FIELD_TYPE`, `UNIT_FIELD_DISPLAYID`, scale). Emit a
`SimObject`-shaped record (`guid, entry, kind, pos, orientation, displayId, scale`) — note
`WorldSim`/`SimObject` already model exactly this. Add a zlib inflate for
`SMSG_COMPRESSED_UPDATE_OBJECT` (reuse whatever inflate the MPQ/BLP path already links, or
add miniz). Validate by feeding it bytes produced by `Object::BuildCreateUpdateBlock`
captures (or the dissector's parsed output) and diffing the decoded `SimObject`.

**Step 6 — Movement decoder (extend update_object or new `monster_move.{hpp,cpp}`).** Parse
`SMSG_MONSTER_MOVE` (§2.7) into `SimObject.waypoints` + `duration`, and `SMSG_DESTROY_OBJECT`
to remove. Reuse `WorldSim`'s existing waypoint-walking `tick()` to interpolate so the same
animation path the editor uses also plays back live entities.

**Step 7 — Feed the renderer.** Bridge decoded `SimObject`s into `src/world_view.*` /
`src/scene.*`: map `displayId` → model (the M2/`CreatureDisplayInfo.dbc` path the engine
already has via `dbc_defs`), transform positions via `coords.hpp`, and let the existing
M2/anim renderer draw and the existing tick interpolate movement. This is the payoff: real
server entities standing and walking on the already-rendered terrain/WMOs.

**Offline validation strategy throughout.** Every step is testable without a real server by
using `StubBridgeServer` as the peer (extend it to speak real auth/world opcodes alongside
its editor protocol) and by replaying byte captures. For final end-to-end confidence, run a
local CMaNGOS `mangos-classic` realmd+mangosd with a 5875 client data set and compare
WorldForge's decoded state to the chaodhib Wireshark dissector's parse of the same session.

---

## 5. Risks / version pitfalls / open questions

- **Header cipher is version-specific.** Vanilla = add/xor rolling cipher (already
  correct). Do NOT pull TBC/WotLK `AuthCrypt` (RC4+HMAC-SHA1 with the
  `0x38A78315F8927629…`/`0xCC98AE04E897EACA…` HMAC seeds) — that breaks 1.12.
- **CMD_AUTH_LOGON_PROOF server reply length differs by build.** Vanilla 1.12 sends
  `cmd + error + M2[20]` (and sometimes a trailing `u16`/`u32 unk`), while TBC+ adds
  `account_flags`+`survey_id`. Read defensively; don't assume the 3.x layout.
- **Realm-list count width.** Vanilla = `u8` realm count; 2.4.3+ = `u16`. Getting this
  wrong desyncs the whole realm-list parse.
- **Reversed 4-char tags.** `gamename/platform/os/country` are little-endian fourCC, so on
  the wire they look reversed ("68x", "niW"). Easy to get backwards.
- **Account/password case.** SRP6 uses the UPPERCASED account and password; the world
  auth digest uses the UPPERCASED account name bytes. Mismatched casing => proof fails.
- **UpdateMask field indices are build-specific.** Vanilla `UpdateFields.h` differs from
  TBC/WotLK (fields were inserted/renumbered). Use the **vanilla** table only; a wrong
  index silently misreads every subsequent field. Start by decoding only a few well-known
  fields and skipping the rest by count.
- **MovementBlock conditionals.** The number of bytes in the living block depends on
  `movement_flags` (transport/swim/jump/spline) and `update_flags`. If any conditional is
  mishandled the parser desyncs for the rest of the packet. Build incrementally against the
  simplest "standing creature" case first.
- **`SMSG_MONSTER_MOVE` point encoding.** Vanilla sends full `Vector3` waypoints; 3.3.5
  sends a packed mid-point delta scheme. Don't import the WotLK spline reader.
- **No `SMSG_TIME_SYNC_REQ` in vanilla**; the keep-alive is `CMSG_PING`/`SMSG_PONG` only.
  Conversely the server WILL disconnect if pings stop.
- **Open question — `SMSG_DESTROY_OBJECT` GUID encoding.** CMaNGOS writes a plain `u64`;
  GTKer's vanilla doc labels it "packed". Confirm on the wire before committing a parser.
- **Open question — exact SMSG_CHAR_ENUM equipment-slot layout** (count and per-slot
  fields) for 5875; verify against a live capture, as it shifted across expansions.
- **zlib dependency for compressed updates.** Confirm an inflate is already linked
  (MPQ/BLP) or add a small one (miniz); large entry-bursts WILL arrive as
  `SMSG_COMPRESSED_UPDATE_OBJECT`.

### Primary sources
- wowdev.wiki: *Login*, *World Packet*, *CMD_AUTH_LOGON_CHALLENGE_Client/Server*,
  *CMD_AUTH_LOGON_PROOF*, *CMD_REALM_LIST_Server*, *SMSG_AUTH_CHALLENGE*,
  *CMSG_AUTH_SESSION*, *SMSG_UPDATE_OBJECT*, *Packed_GUID*, *Opcodes*.
- GTKer: *Implementation Guide for the WoW flavor of SRP6*; `wow_messages/docs`
  (`cmsg_auth_session`, `smsg_update_object`, `smsg_monster_move`, `smsg_destroy_object`).
- CMaNGOS `mangos-classic`: `src/realmd/AuthCodes.h`, `src/realmd/AuthSocket.cpp`,
  `src/game/Server/WorldSocket.cpp`, `WorldSession.cpp`, `Opcodes.cpp`,
  `src/game/Object/Object.cpp` / `UpdateData.cpp` / `UpdateFields.h`.
- TrinityCore (3.3.5) — cited only as a differing analogue.
