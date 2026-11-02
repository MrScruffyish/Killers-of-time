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

## The game
When the host starts the game, the server creates it (map from the lobby settings, one town and
one hero per player, neutral mines and dwellings) and sends, besides `lobby_state` and
`game_starting`, one `game_started` to every human player:

| Direction | Type | Fields |
|---|---|---|
| server -> client | `game_started` | `you` (`playerId`, `team`), `view` |
| server -> client | `game_state` | the player's `view`, sent after every change anybody makes |
| server -> client | `command_result` | `replyTo`, `info` (what happened, see below), sent to the player who sent the command |

Failed commands answer with `error` (`replyTo` names the request). Codes: `not_your_turn`,
`not_a_player`, `no_game`, `bad_request` (missing/invalid fields, `message` says which),
`failed` (the rules refused, `message` says why: "not enough gold", "requires Barracks", ...),
`unknown_message`.

### The view (`PlayerView`)
The server only sends what the player may know: `day`, `currentPlayer`, `you`, own `resources`
(private), `players` (name, team, colour, race, isAi), `map`, `heroes`, `armies`, `towns`,
`mines`, `mapDwellings`, `gameOver` (bool) and `winnerTeam` (when over; 0 = nobody). Each
player has `eliminated` (no towns and no heroes left: out of the game, turns skipped).
When the game is over every command is refused with code `game_over`.
- `map`: `width`, `height`, `terrainPalette`, `tiles` (palette index per tile, row-major, **-1 =
  never explored**), `visible` (base64, one byte per tile, 1 = in sight right now).
- Things of the player's own team are complete (hero traits, exact army HP, town buildings and
  recruits, dwelling stock). Things of others only appear on tiles in sight, without secrets (no
  traits, no HP, no garrison, no stock). Own mines and dwellings are always listed.

### Commands (all need your turn)
| Type | Fields | `info` on success |
|---|---|---|
| `end_turn` | | `day`, `newDay`. AI players then play their turns on the server (with these same commands); players who left are skipped. |
| `move_hero` | `hero`, `x`, `y` | `arrived`, `stepsTaken`, `movementSpent`, `capturedMine` / `capturedTown` / `capturedDwelling`; after an attack also `battles` (list, see below) and `heroKilled` |
| `build` | `town`, `building`, `useTrade`? | `trades` (the exchanges made) |
| `recruit` | `town`, `unit`, `count`, `army`? | |
| `recruit_at_dwelling` | `dwelling`, `count`, `army` | |
| `hire_hero` | `town`, `hero` (template id), `useTrade`? | `hero` (id) |
| `trade` | `give`, `amount`, `get` | |
| `move_units` | `from`, `to`, `stack`, `count` | |
| `pick_trait` | `hero`, `trait` | |
| `buy_all_units` | `town`?, `strategy`? (`best`/`most`) | `unitsBought`, `townsServed`, `spent` |
| `auto_build` | `town`?, `useTrade`? | `built`, `needsChoice` (towns and the buildings that need a decision), `cannotAfford` |
| `build_then_buy` | `town`?, `useTrade`?, `strategy`? | `build` and `buy` as above |
| `send_dwelling_units` | `town`? | `dwellingsEmptied`, `unitsSent` (no `town` = each to its closest town) |

`town` omitted means all of the player's towns for the one-click actions.

### Leaving
A player who disconnects during the game is played by the passive AI from then on (the game
continues). Reconnecting comes later.

## Coming next
Battles and attacking, spells, reconnecting, saving a running game on the server.

### Battles (auto-resolved, first version)
Moving onto a tile with hostile armies (enemy heroes, neutral monsters, a defended town)
attacks them when the hero can pay the whole path. Each entry of `battles`:
```json
{ "outcome": "attacker" | "defender" | "draw", "rounds": 4, "experience": 60, "levelsGained": 0,
  "loserHeroKilled": false,
  "attacker": { "owner": 3, "hero": 7, "losses": { "pikeman": 2 } },
  "defender": { "owner": 0, "hero": 0, "losses": { "militia": 5 } } }
```
Owner/hero 0 = neutral / none. Only the attacker gets the report for now; the defender sees the
result in the next `game_state`. Neutral monster armies appear in `armies` with `owner` 0 and `hero` 0.
