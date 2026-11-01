# Network protocol (version 1)

TCP. The server listens on port 45454 by default (`--port` to change it).

## Framing
Every message is one frame: a 4-byte big-endian length, then that many bytes of UTF-8 JSON
(one JSON object). Length 0 or above 16 MiB is a protocol error: the server answers
`protocol_error` and closes the connection.

## Messages
Every message is a JSON object with a `"type"`. Requests carry an optional integer `"seq"`;
a reply to a request echoes it (`welcome`, `pong`) or names it in `"replyTo"` (`error`).
Unknown extra fields are ignored by receivers.

### Handshake
The first message of a client must be `hello`, otherwise the server sends `error`
(`expected_hello`) and closes.

| Direction | Type | Fields |
|---|---|---|
| client -> server | `hello` | `protocol` (must be 1), `name` (trimmed, max 24 chars, empty = "Player N") |
| server -> client | `welcome` | `protocol`, `serverVersion`, `clientId`, `races` (list), `aiProfiles` (list) |
| server -> client | `lobby_state` | the whole lobby, sent to everyone after every change (see below) |

A wrong `protocol` gives `error` with code `protocol_mismatch`, then the connection is closed.

### Lobby (before the game starts)
| Direction | Type | Fields |
|---|---|---|
| client -> server | `claim_slot` | `slot` (0-based; must be open) |
| client -> server | `release_slot` | |
| client -> server | `set_slot` | `slot` plus any of `race`, `team` (1-8), `color` (#RRGGBB). Host only: `kind` (`open`/`ai`/`closed`), `name`, `aiProfile`, `difficulty` (`easy`/`normal`/`hard`) |
| client -> server | `set_ready` | `ready` (bool) |
| client -> server | `set_settings` | host only: `mapWidth`, `mapHeight` (16-256), `seed` (number or text) |
| client -> server | `start_game` | host only |
| client -> server | `ping` | answered with `pong` |
| server -> client | `game_starting` | the final lobby (same fields as `lobby_state`) |
| server -> client | `error` | `code`, `message`, `replyTo` |

Error codes: `protocol_error`, `protocol_mismatch`, `expected_hello`, `unknown_message`,
`unknown_client`, `already_joined`, `already_started`, `no_such_slot`, `slot_taken`,
`slot_closed`, `no_slot`, `not_your_slot`, `not_host`, `invalid_value`, `color_taken`,
`not_ready`, `cannot_start`.

### lobby_state
```json
{ "type": "lobby_state",
  "members": [ { "clientId": 1, "name": "Ann", "isHost": true } ],
  "slots": [ { "index": 0, "kind": "human", "clientId": 1, "name": "Ann", "race": "humans",
               "team": 1, "color": "#d62728", "ready": false, "aiProfile": "", "difficulty": "" } ],
  "settings": { "mapWidth": 64, "mapHeight": 64, "seed": "1" },
  "started": false }
```
`seed` is text because 64-bit numbers do not fit a JSON number.

Lobby rules (see `server/Lobby.h`): the first member is the host; a new member takes the first
open slot; colours are unique among occupied slots; any change to a human's slot (or the map
settings) clears the ready flag; the host can start when there is at least one human, two
occupied slots on two different teams, and all humans are ready. Leaving frees the slot, and
the next member becomes host if the host leaves.

## Coming next
The game itself: `game_started` with the player's view, commands (`move_hero`, `build`, `recruit`,
`end_turn`, ...), fog-filtered state updates, battle messages, reconnecting.
