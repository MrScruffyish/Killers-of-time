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
60. Campaign carry-over: keep only a hero's name, class and level between chapters - no army,
    spells, artefacts, traits or experience (an army can be grown without risk otherwise).
    Add a configurable maximum level for carried heroes; 0 means no limit.
61. Re-run the intrinsic-1 all-AI benchmark after item 60: `./run-intrinsic-1-benchmark.sh`
    (200 runs, seed 1, simple profile). The report goes to stdout; nothing tracked needs updating.
62. An artifact that was a chapter's goal (FindArtifact) stays with the carried hero in later
    chapters. Other loot still does not carry over.
63. Generic campaign Legacy plumbing: a per-campaign storyline (intro, per-chapter text,
    epilogue) plus persistent threads — spells learned, monuments raised, best-day records.
    Carry the state in progress, apply/award it at chapter boundaries, and show the story in-game.
63.1 **MANUAL**: run `make test`; legacy parsing, progress round-trips, spell/monument awards
    and the carry-through-chapters flow are covered. Then play a couple of chapters with a story
    campaign and check the intro at the goal dialog and the story/epilogue at chapter end.
64. Ship the Codex campaign: a spell-research storyline where each won chapter teaches the
    carried hero a page/spell that lasts into later chapters.

## TBD

1. If the 512 px art masters stutter on first show: smaller copies or load them in the
background (needs playtesting).
2. Whether a QGraphicsView comparison is still needed for map drawing (only if it comes up).
3. Exact balance numbers for Sylvan and Tidefolk (wait for `kot_balance weeks`).
4. How players share a server address and token outside the game (copy/paste now; maybe a
file or link later).
6. In battles it is hard to se which side is which. Units should face towards the enemy side (so they attack backwards). i guess that needs a lot of redrawing units and then mirrror them?

## DONE

36. The "All town" is not needed when you only have one town, or the current city is the only one not having that building.
37. No upper limit on level on heros.
38. CTRL+s for save, +l for load.
39. After a battle the hero Level-up dialog appears before the battle result (losses), and the battle map is still active behind them. Show the result first, on the map, then the level-up.
40. In battle map, the hero spells should be short-cut S, the unit spells: X
41. The spells should be displayd on something that looks like a book, and there should be an illustration of the spell (saved as png in some art data directory so it can be replaced).
42. On first opening the spell book in a fresh battle, only the top few percent of the first spell's illustration is visible (the book window itself is normal sized).
43. Add a playable necromancer race (`necroman`): race data, hero class(es), hero templates, units with upgrades, dwellings and starting army.
44. Add a playable giants race: race data, hero class(es), hero templates, units with upgrades, dwellings and starting army.
45. Add a playable halflings race: race data, hero class(es), hero templates, units with upgrades, dwellings and starting army.
46. Add more buildings and upgrade chains, including the metals idea from TBD 5.
47. Make the graphics more advanced: richer generated art for the new content and improved detail/effects where practical.
48. Custom-art overlay: a `data/custom_art` folder with the same file names as `data/art`, checked first by the client, so `kot_artexport` can be rerun any time without destroying hand-made pictures.
49. Have plain `make` regenerate `data/art` when content or the drawings change (safe now that custom art lives in its own folder), without redoing it on every build.
50. Add the one-eyed, one-horned, flying purple people eater: a non-recruitable neutral monster with its own art, plus a "slay the monster" victory goal that generated scenarios/campaigns can use.
51. Allow campaigns of up to 20 scenarios, and add more generated goals: rotating bosses for slay goals, finding a particular artifact, and exploring the map.
52. Ship the fixed `intrinsic-1` five-chapter benchmark campaign and five built-in goal scenarios, expose campaign/goal generation in `kot_mapgen`, and add an all-AI `kot_aiplay --campaign` difficulty report.
53. Make campaigns a solo mode for one human: the setup lobby offers campaign (random or
    shipped) or map (random or pre-built), and the server fills every other seat with AI for
    the current chapter. Choosing a campaign hides all other setup options.
54. In-game menu: allow abandoning the current game with no save, resetting the lobby and
    returning to setup; add an option to show the current scenario goal again; and do not
    report taking an unowned/neutral mine ("You took a mine from somebody.").
55. For slay-goal scenarios and campaigns, slaying the purple monster is the only win
    condition; remove the "defeat everyone" conquest fallback.
56. Animate other players' (AI/enemy) moves with a configurable speed from slow to immediate
    (instant location change, no animation), so their direction is visible.
57. Sort the resource bar so gold is first, then the cheaper resources up to the rarer ones.
58. `make test` prints the current git commit and, when the working tree is dirty, a hash of
    its changes, so a test report says exactly what was rebuilt and run.
59. Fix the Solo Play chooser: it showed literal `<b>Play...` markup and the light-blue card
    colours clashed with the game's dark brown/gold look.
