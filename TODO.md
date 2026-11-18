# TODO.md

Numbered work list. Finished and committed items move to DONE. The agent adds tests but does
not compile or run them. Add a **MANUAL** line only when a change comes with new coded tests
for the user to build and run (simple changes need none); one test run resolves every manual
line waiting for it, and the agent then removes them.

## TODO

1. Playtest the real client and work off the user's notes.
1.1 Full single-player game: UI, rules, town and battle screens.
1.2 Two-client network game: connect, disconnect, reconnect and take seats back.
1.3 Write down each problem (what, where, expected) before fixing it.
2. Balance leftovers.
2.1 Buff Sylvan (weakest, ~20% wins).
2.2 Nerf Tidefolk (strong over 3 weeks).
2.3 Re-check with `kot_balance weeks` (budgets 4000/6000/9000 gold, 1-4 weeks).
3. Real art in the 2.5D style (replace the code-drawn pictures file by file).
3.1 Units, heroes and portraits.
3.2 Towns, buildings, mines and map objects.
3.3 Terrain, coasts, decorations, trees/hills/peaks.
3.4 UI buttons, cursors, spell and artifact icons.
4. Playing over the internet.
4.1 Document and test a remote server setup (port, TLS, allowed tokens).
4.2 Make connect and reconnect robust on slow or flaky links.
5. Better battle AI.
5.1 Measure the current battle AI with `kot_aiplay` and `kot_balance` duels.
5.2 Improve target choice, spell use and retreat/surrender decisions.
6. Object memory after a reconnect.
6.1 Keep remembered towns, mines, dwellings and treasures on the server.
6.2 Send them back in the view after a reconnect.
43. Add a playable necromancer race (`necroman`): race data, hero class(es), hero templates, units with upgrades, dwellings and starting army.
43.1 **MANUAL**: build, run `build/bin/kot_artexport` for the new content, then `make test`; necroman is playable with its roster and dwellings.
44. Add a playable giants race: race data, hero class(es), hero templates, units with upgrades, dwellings and starting army.
44.1 **MANUAL**: after the art export and `make test`, the giants race is playable with its full roster and dwellings.
45. Add a playable halflings race: race data, hero class(es), hero templates, units with upgrades, dwellings and starting army.
45.1 **MANUAL**: after the art export and `make test`, the halflings race is playable with its full roster and dwellings.
46. Add more buildings and upgrade chains, including the metals idea from TBD 5.
46.1 **MANUAL**: after the art export and `make test`, iron/bronze buildings work and every top-tier unit has a metal-traded elite upgrade.
47. Make the graphics more advanced: richer generated art for the new content and improved detail/effects where practical.
47.1 **MANUAL**: after the art export and `make test`, the new races, metals, elites, portraits and towns have distinct generated art.

## TBD

1. If the 512 px art masters stutter on first show: smaller copies or load them in the
background (needs playtesting).
2. Whether a QGraphicsView comparison is still needed for map drawing (only if it comes up).
3. Exact balance numbers for Sylvan and Tidefolk (wait for `kot_balance weeks`).
4. How players share a server address and token outside the game (copy/paste now; maybe a
file or link later).
5. Iron, bronze, and other metalls, that gives extra upgrades to units in town. So we can get many more things.
6. In battles it is hard to se which side is which. Units should face towards the enemy side (so they attack backwards). i guess that needs a lot of redrawing units and then mirrror them?

## DONE

36. The "All town" is not needed when you only have one town, or the current city is the only one not having that building.
37. No upper limit on level on heros.
38. CTRL+s for save, +l for load.
39. After a battle the hero Level-up dialog appears before the battle result (losses), and the battle map is still active behind them. Show the result first, on the map, then the level-up.
40. In battle map, the hero spells should be short-cut S, the unit spells: X
41. The spells should be displayd on something that looks like a book, and there should be an illustration of the spell (saved as png in some art data directory so it can be replaced).
42. On first opening the spell book in a fresh battle, only the top few percent of the first spell's illustration is visible (the book window itself is normal sized).
