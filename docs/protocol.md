# Network protocol (version 1)

TCP. The server listens on port 45454 by default (`--port` to change it).

## Framing
Every message is one frame: a 4-byte big-endian length, then that many bytes of UTF-8 JSON
(one JSON object). Length 0 or above 16 MiB is a protocol error: the server answers
`protocol_error` and closes the connection. When the top bit of the length is set
(protocol 2), the payload is compressed: a 4-byte big-endian unpacked size (at most 16 MiB),
then a zlib stream of the JSON (Qt's `qCompress` format). Senders compress JSON of 1 KiB
or more; game views shrink from tens of KB to a few.

## Messages
Every message is a JSON object with a `"type"`. Requests carry an optional integer `"seq"`;
a reply to a request echoes it (`welcome`, `pong`) or names it in `"replyTo"` (`error`).
Unknown extra fields are ignored by receivers.

### Handshake
The first message of a client must be `hello`, otherwise the server sends `error`
(`expected_hello`) and closes.

| Direction | Type | Fields |
|---|---|---|
| client -> server | `hello` | `protocol` (must be 2), `name` (trimmed, max 24 chars, empty = "Player N"), `playerKey` (this client's secret for this server, see below), `content` (the client's content checksum: game messages carry content ids as numbers, so a server with game data refuses other data with `content_mismatch`) |
| server -> client | `welcome` | `protocol`, `serverVersion`, `clientId`, `races` (list), `aiProfiles` (list) |
| server -> client | `lobby_state` | the whole lobby, sent to everyone after every change (see below) |

A wrong `protocol` gives `error` with code `protocol_mismatch`, then the connection is closed.

### Lobby (before the game starts)
| Direction | Type | Fields |
|---|---|---|
| client -> server | `claim_slot` | `slot` (0-based; must be open) |
| client -> server | `release_slot` | |
| client -> server | `set_slot` | `slot` plus any of `race`, `team` (1-8), `color` (#RRGGBB). Host only: `kind` (`open`/`ai`/`closed`/`hotseat`: another human at the host's computer, always ready; the host's connection plays all his hot-seat players, the server acts for and shows the one whose turn it is), `name` (AI and hot-seat slots), `aiProfile`, `difficulty` (`easy`/`normal`/`hard`) |
| client -> server | `set_ready` | `ready` (bool) |
| client -> server | `set_settings` | host only: `mapWidth`, `mapHeight` (16-256), `seed` (number or text) |
| client -> server | `start_game` | host only |
| client -> server | `generate_map` | host only, no game running. A random scenario: `kind`=`scenario`, `id` (1-40 of `A-Za-z0-9_-`, the file name), `name`, `description`, `width`, `height` (24-192), `seed`, `races` (one per player, 2-8; `random` picks one; the first is meant for the human), `islands`, `seaLevel` (0.1-0.6), `mountains` (share), `rivers` (-1 = by size), `roads`, `monsterPercent`, `victory` (`conquest`/`capital`/`gold`/`days`), `victoryAmount` (gold or days; 0 = 50000 / 90). A random campaign: `kind`=`campaign`, `id`, `name`, `chapters` (2-6), `race`, `size` (first map; +16 per chapter), `seed`, `carryHeroes`, `carryArmy`, `islands`. Written to the server's saves folder (`scenarios/`, `campaigns/`); reply `id`, `kind`; then everyone gets `content_lists` |
| server -> client | `content_lists` | `scenarios`, `campaigns` (same as in `welcome`), after a map was made |
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
The server only sends what the player may know: `day`, `currentPlayer`, `you`, own `resources`, `income` (per day) and `tradePercent` (what trading gives, percent of the fair value)
(private), `players` (name, team, colour, race (id), isAi), `map`, `heroes`, `armies`, `towns`,
`mines`, `mapDwellings`, `mapObjects` (in sight: `id`, `kind` treasure/artifact/site, `x`, `y`, `resources` or `artifact`; sites: `site` and `giftFor` (your hero ids that would still get something there); a quest_hut also `quest`, its wish and reward in words), `intro` (the scenario's story, if any), `news` (today's story events, if any), `gameOver` (bool) and `winnerTeam` (when over; 0 = nobody) and `history` (when over: per week `week`, `players`: player id -> `armyValue`, `towns`, `mines`, `heroes`, `gold`; the last entry is now). Each
Own heroes have `movementPoints` and `movementMax` (a full day).
Towns in sight have `walls` (buildings with a defense effect) and `size` (buildings), so the map can draw them. Each
player has `eliminated` (out of the game, turns skipped: no towns and no heroes left, or out by the `defeat` rule; then the heroes leave the map), `towns`, `heroes`, `mines`, `armyValue` (what the units cost; rounded to two digits for other teams) and, while the player has no town, `townlessDaysLeft` (days left to take one).
When the game is over every command is refused with code `game_over`.
- `map`: `width`, `height`, `tiles` (the terrain id of each tile, row-major, **-1 =
  never explored**), `visible` (base64, one byte per tile, 1 = in sight right now).
- Things of the player's own team are complete (hero traits, exact army HP, town buildings and
  recruits, dwelling stock). Things of others only appear on tiles in sight, without secrets (no
  traits, no HP, no garrison, no stock). Own mines and dwellings are always listed.

### Commands (all need your turn)
| Type | Fields | `info` on success |
|---|---|---|
| `end_turn` | | `day`, `newDay`. AI players then play their turns on the server (with these same commands); the game waits at the turn of a human who is away. |
| `move_hero` | `hero`, `x`, `y` | `arrived`, `stepsTaken`, `movementSpent`, `capturedMine` / `capturedTown` / `capturedDwelling`; after an attack also `battles` (list, see below) and `heroKilled`; `visitedSite` (`site`, `result`: what the hero got), `foundResources`, `foundArtifacts` (of those, `stowedArtifacts` went to the backpack because their place was taken), `learnedSpells` when that happened; `ambushed: true` when a zone of control stopped the hero and the armies there attacked; `spotted: true` when he stopped because a hostile army came into sight that was not visible when he set out; `joinOffer` (`army`, `hero`, `units`: `unit`, `count`; `cost`: gold, balance `battle.joinCostPercent` of their recruit price) when much weaker neutral monsters (balance `battle.joinStrengthRatio`) would rather join him: the hero stopped next to them, answer with `answer_join` (computer players pay when they can (`joined`), else they fight). A move that runs out of movement keeps the target as the hero's `destination` (own heroes in the view: `destination: [x, y]`) |
| `build` | `town`, `building`, `useTrade`? | `trades` (the exchanges made) |
| `recruit` | `town`, `unit`, `count`, `army`? | |
| `recruit_at_dwelling` | `dwelling`, `count`, `army` | |
| `hire_hero` | `town`, `hero` (template id), `useTrade`? | `hero` (id); a tavern hires out one hero a week (own towns in the view: `hiredThisWeek`) |
| `trade` | `give`, `amount`, `get` | |
| `answer_join` | `answer`: `accept`, `fight` or `leave` | answers a `joinOffer` from the last `move_hero`: `joined`, `left`, or (fight) the attack like `move_hero` (`battleStarted`, ...). Any other command drops the offer |
| `move_units` | `from`, `to`, `stack`, `count`; or `all: true` instead of `stack`/`count` (every stack that fits). The armies are on one tile (hero and garrison) or are two heroes side by side | with `all`: `moved` (units), `left` (units that did not fit) |
| `pick_trait` | `hero`, `trait` | |
| `equip_artifact` | `hero`, `artifact`, `replace`? | wears one from the backpack; when its place is full, `replace` (worn there) or else the first worn there goes to the backpack |
| `unequip_artifact` | `hero`, `artifact` | takes a worn one off into the backpack |
| `give_artifact` | `from`, `to` (heroes), `artifact` | two of your heroes side by side: the artifact (worn or carried) goes to the other, worn when its place has room |
| `cast_spell` | `hero`, `spell` | an adventure-map spell (costs spell points) |
| `buy_boat` | `hero` | in a town with a shipyard: the hero boards a boat on the water next to it; `move_hero` then sails, and moving onto the shore lands (`landed`, ends the day's movement) |
| `buy_all_units` | `town`?, `strategy`? (`best`/`most`), `useTrade`? | `unitsBought`, `townsServed`, `spent` |
| `auto_build` | `town`?, `useTrade`? | `built`, `needsChoice` (towns and the buildings that need a decision), `cannotAfford` |
| `build_then_buy` | `town`?, `useTrade`?, `strategy`? | `build` and `buy` as above |
| `send_dwelling_units` | `town`? | `dwellingsEmptied`, `unitsSent` (no `town` = each to its closest town) |

`town` omitted means all of the player's towns for the one-click actions.

### Leaving, coming back, saved games
A player who disconnects keeps his seat: no computer plays for him, the game waits at his
turn (players may come at different times); a battle against him is fought by the battle AI
for his side. In views his entry in `players` has `away: true` while he is not connected.
Content ids are numbers everywhere in game messages (resources, terrain, units: `unit` in
stacks, recruit options, dwellings, battles, `recruit` commands; `losses`/`importedUnits` are
objects keyed by the unit id as text); they mean the same as in the server's data (the hello's
content checksum guarantees that). Seat keys: every client makes a random secret key per server (kept in its settings) and
sends it in `hello` (`playerKey`). The server keeps only its SHA-256 (with each seat the client
plays, also in saves). A running game, or one the host loads, gives the free seats bound to
that key back to the client at once (`game_started`, no choice offered); other clients are
only offered seats nobody has played yet (a seat of someone who is away cannot be taken). The server can resume
the newest save at start (`--resume`). A player who is out may join and watch; his turns are
skipped.

| Type | Fields | Reply (`command_result` info) / notes |
|---|---|---|
| `join_game` | `player` | `player`; then `game_started` to you and new views to all. Only a free human player (nobody plays him now). |
| `save_game` | `name` (1-40 of `A-Za-z0-9_-`) | `name`. Any player of the running game, any time; only the host may replace an existing save (else `save_exists`). Saved on the server (`--saves <dir>`, default the app data folder). |
| `list_saves` | | `saves` (names, newest first) and `games`: per save `name`, `gameVersion`, `savedAt`, `formatVersion`, `day`, `players`, `mapWidth`, `mapHeight`, `status` (`ok`, or `incompatible` = cannot be loaded: another save format, or other game data) and `problems` (why) |
| `load_game` | `name` | `name`. Host only, no game running. Refused with `incompatible_save` and the reason when the save cannot be loaded. Then everyone gets `game_loaded` {`freePlayers`}. |

`welcome` has `freePlayers` (`[{id, name}]`) when a game is running: a client joining late (or
coming back) picks one with `join_game`. After loading, all humans are free and the game waits on
a free human's turn until someone takes him over. No passwords yet: anyone can take a free player.

## Battles
Moving onto a tile with hostile armies (enemy heroes, neutral monsters, a defended town)
attacks them when the hero can pay the whole path; the hero fights from the tile before.
Each defender is fought in turn (a visiting hero first, then the garrison).

If a connected human owns one of the two sides, the battle is **interactive**: `move_hero` answers
with `battleStarted: true`, the participants' views get `battle`, everyone else's view gets
`battleInProgress: true`, and until it ends only `battle_action` is accepted (from the owner of the
active stack, whoever's turn it is on the map). Otherwise (AI vs AI or monsters) it is played out at
once by the battle AI and `move_hero` returns the reports.

`battle` in the view: `width`, `height`, `round`, `active` (unit id), `yourSide` (0 attacker,
1 defender), `sides` (`owner`, `hero`), `units` (`id`, `side`, `unit`, `count`, `frontHp`, `maxHp`,
`x`, `y` (hex, odd-r offset), `shots`, `retaliated`), `modifiers` (spell effects: `stat`, `amount`, `rounds`)), `hero` (yours only: `spellPoints`, `hasCast`, `spells` [`id`, `target`, `cost`, `castable`]), and for the active stack `reachable`
(`[[x, y], ...]`), `attackable` (enemy ids it can reach), `shootable` (enemy ids in its range), `canShoot`, `canWait`, `canRetreat`; `obstacles` (rock hexes); `terrain` (the ground, for the picture); in a siege `siege` (`level`, `walls`: [x, y, hp], `wallMaxHp`, `gate`, `towers`, `engineBuilt`, `canBuildEngine`); ranged units have `range` (hexes). Events may also be `wait`, `morale` (extra action), `falter` (bad morale), `retreat`; strikes may have `lucky: true`.

| Type | Fields | `info` on success |
|---|---|---|
| `battle_action` | `action`: `move` (`x`, `y`), `attack` (`target`, optional `x`, `y` = hex to strike from), `shoot` (`target`), `defend`, `wait` (act again at the end of the round, once), `retreat` (a side with a hero, not a town's defenders, that has an own town free of heroes to flee to: army lost, hero escapes to the nearest such town), `surrender` (same conditions, against another player: pay `surrenderCost` gold, keep hero and army, which go to that town; the view sends `surrenderCost` when possible), `unit_cast` (`spell`, `target`: the active stack's own spell, uses its turn), `cast` (`spell`, `target`: your hero, once per round, does not end the stack's turn), `auto` (the battle AI plays your side to the end), `ai` (the battle AI makes this one move for your stack: the client's auto-play, which can be stopped), `build_engine` (attacker in a siege: pay balance `siege.engineCost` for a catapult; does not use the turn), `shoot` with `x`, `y` (the catapult: a wall segment) | `events` (list; also `tower` (fromX, fromY, target, damage), `wall_hit` (x, y, damage, fell), `engine`; first your stack's, then what the computer's stacks did until your next turn: `type` move/attack/shoot/retaliate/defend/round, `unit`, `target`, `damage`, `killed`, `x`/`y`); when it ended `battleOver: true` and `move` (the `move_hero` info with `battles`) |

Battle reports (`battles` entries, and `lastBattle.report` in the view of both participants,
`lastBattle.id` grows with every battle):
```json
{ "outcome": "attacker" | "defender" | "draw", "rounds": 4, "experience": 60, "levelsGained": 0,
  "loserHeroKilled": false,
  "attacker": { "owner": 3, "hero": 7, "losses": { "pikeman": 2 } },
  "defender": { "owner": 0, "hero": 0, "losses": { "militia": 5 } } }
```
Owner/hero 0 = neutral / none. A winner whose race has the ability gets `raised` (`unit`, `count`: undead raise the fallen) or `lootGold` (orcs). After a retreat: `retreated: true`, and `retreatedTo` (town name) when
the hero fled to the nearest own town without a hero (he has no movement left that day). A surrender also sends `retreatedTo`: the hero goes there with his army.
Neutral monster armies appear in `armies` with `owner` 0 and `hero` 0.
Saving is refused while a battle runs. A human who disconnects during a battle is replaced by the
battle AI.

Lobby settings also have `difficulty` (`easy`/`normal`/`hard`/`expert`, host only); the game view has `difficulty`.
Lobby settings also have `defeat` (host only): when a player is out: `week` (default: 7 days without a town), `town` (at once with his last town), `all` (no towns and no heroes); the game view has `defeat` (a short text) next to `victory`.
Stacks in views and battle units may have `stars` (0..5, veterancy).

`welcome` lists `scenarios` (`id`, `name`, `description`, `width`, `height`, `races`). Lobby settings have `scenario` (an id, empty = random map; host only).
`welcome` also lists `campaigns` (`id`, `name`, `description`, `chapters`, `chapterIds`, `next`); lobby settings have `campaign`. The game view has `campaign` (`name`, `chapter`, `chapters`, `result` won/lost at the end).
The game view has `victory` (a short text: what wins this game).
The game view has `weekEvent` (`id`, `name`, `description`) when an event is on.
Lobby settings also have `mapType` (`continent` or `islands`: one island per player, boats needed; random maps only)
`hiddenMap` (default true; false: every team knows the whole map, towns, mines and dwellings from the start), `fogOfWar` (default true; false: everything a team has explored stays in sight, armies included) and `autosaves` (0-100, default 5): the server saves `autosave_<started>_dayNNNN` (`started`: when the game was started or loaded,
yyyyMMdd-HHmmss, so games never mix) at the start of every day and keeps that many most recently written (0 = no autosaves). A campaign chapter is also saved as `campaign_<id>_chapter<N>_start` when it starts
(never removed). Both show in `list_saves`.
