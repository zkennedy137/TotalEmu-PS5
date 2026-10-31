# Pending changes

All of these were built in v1.6.3 (see CHANGELOG.md).

## Built in v1.6.5 (disc read tested on the console; the rest not yet)

- Game screen: on PS1 and PS2, fill the space between the cover and the library panel and the space between the cover and the right side of the screen with the cover, dimmed. Built in v1.6.5.

- PS1 names: added GameDB-PSX's serial list (assets/gamedb/psx.tsv) as the title source for the TheGamesDB and RAWG lookups, and its release name as a libretro fallback. Order stays libretro, then TheGamesDB, then RAWG. Built in v1.6.5.

- PS4 and PS5 tabs: added as tabs in assets/launcher.ini. Built in v1.6.5.
- PS4 and PS5 discs: a disc with no SYSTEM.CNF and no PS3_GAME is listed as "Unidentified disc" on both tabs, and is not offered the PS1 install. It can't be started or installed yet. Built in v1.6.5. Tested: the PS5 disc was read and listed (main.log, 23:02 run).
- PS5 title launch: a title_id in [system.ps5] of launcher.ini is listed on the PS5 tab and started with Cross (helper LAUNCH). Built in v1.6.5. Needs a real installed title ID to test.
- Blu-ray reading: discs over 4,500,000 sectors (the DVD limit) were rejected, so a PS5 disc never reached the list. Limit raised to 60,000,000 sectors. Built in v1.6.5. Tested: the PS5 disc is now read.
  Still to do: identify PS4 and PS5 discs by their root layout (check main.log for the "disc check: root entry" lines from a real disc), and list installed PS4 and PS5 games (needs the console's installed-title data).

## Pending (not built yet)

- PS5 disc naming: a disc with a PS5 folder in its root and a PPSA folder under app/ is named by that title ID (the test disc is PPSA07411: app/PPSA07411, bd/param.json with masterDataId PPSA07411). It is listed only on the PS5 tab, not the PS4 tab. The PS5 folder alone is not enough: it holds only UPDATE/PS5UPDATE.PUP on the test disc. Discs without the PPSA folder stay "Unidentified disc" on the PS4 tab.
- PS4 disc detection: not started. No PS4 disc sample yet.

The items under "Pending (not built yet)" are not in the build. The "still to do" lines above need a disc sample or console data before they can be built.
