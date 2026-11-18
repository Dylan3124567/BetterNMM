# BetterNMM test checklist

Things to try by hand, most important first. Each item says what should happen. Everything not listed here (file linking,
deploying, the profile redirect, load order files, FOMOD logic) is covered by the automated tests (`ctest --test-dir build`).

If something goes wrong, the useful details are in the instance window's **Game Log** and **Other Logs** pages and in
`%APPDATA%\BetterNMM\logs\BetterNMM-0.log`.

## Before you start

- [ ] Copy `Documents\My Games\Skyrim Special Edition\Saves` somewhere safe once. BetterNMM never writes there, but a
      backup costs nothing.
- [ ] Double-click `start.bat`. The first run builds for a few minutes, later runs start in seconds.
      **Expected:** the setup wizard, then the main window.

## 1. Playing the game in an isolated instance

- [ ] Add Instance > Skyrim Special Edition > *Isolated instance* > OK.
      **Expected:** done in a few seconds; Edit > Game Files says about 36 files shared with Steam and about 41 MiB copied.
- [ ] Launch it, start a new game, change a setting (for example brightness), save, quit.
      **Expected:** the save shows on the instance's **Saves** page and *not* in your normal `My Games\...\Saves`;
      your normal Skyrim (from Steam) still has its old settings.
- [ ] Start Skyrim from Steam afterwards. **Expected:** your normal saves and settings, untouched.

## 2. Script extender on one instance only

- [ ] Add Instance with *Install the script extender (skse64_loader.exe) from Nexus Mods* ticked, or use **Install Script
      Extender** on an instance's Mods page. **Expected:** it names "SKSE64 Steam 2.3.1" as the build for your game
      (1.7.104). With Premium it installs straight away; otherwise BetterNMM's Nexus Mods window opens on that file's
      download page, and **Slow download** there installs it into this instance once the window closes.
- [ ] **Expected:** the **Launch** dropdown offers *Script extender (skse64_loader.exe)*; launching it starts the game with
      SKSE (the console command `getskseversion` answers).
- [ ] Check `steamapps\common\Skyrim Special Edition`. **Expected:** no `skse64_loader.exe` there.
- [ ] Create a second isolated instance. **Expected:** it has no SKSE.

## 3. Skyrim Together in its own instance

- [ ] Make a separate isolated instance, install Skyrim Together Reborn into it, and add its `SkyrimTogether.exe` under
      Edit > Settings > **Tools** (path relative to the game folder works).
- [ ] Launch that tool from the Launch dropdown. While the game runs, open Task Manager > SkyrimSE.exe > *Open file
      location*. **Expected:** the instance's `game` folder, **not** the Steam folder. (If it is the Steam folder, Skyrim
      Together finds the game on its own; please report it.)
- [ ] **Expected:** the other instances and the Steam folder have no Skyrim Together files.

## 4. Nexus Mods account and downloads (the API key is optional)

- [ ] With no account under Settings > Accounts: Browse Nexus Mods, Mods page > Check for Updates, and downloading all
      work; the download window asks you to log in to Nexus Mods once.
- [ ] Accounts > Add: paste your personal API key from <https://www.nexusmods.com/settings/api-keys>.
      **Expected:** your user name, and whether you are Premium. The dialog says the key is optional.
- [ ] Settings > General > *Handle Nexus Mods download links*. Then on a mod page click **Mod Manager Download**.
      **Expected:** BetterNMM asks which instance (if you have several); with a key it installs the file, with name,
      version and Nexus link filled in; without one the download window opens on that file (one click).
      (This takes the links over from Vortex; Vortex takes them back if you click its own setting.)
- [ ] Tick SkyUI (on an instance without SKSE) and **Review and confirm**. **Expected:** the list also has *Script
      extender (skse64_loader.exe)*, ticked, *Required by: SkyUI*. With SKSE installed it is not added. Tick a mod
      that needs other Nexus mods (for example a patch). **Expected:** those come along ticked, each with what needs it;
      mods you already have are not added; unticking one leaves it out.
- [ ] Browse Nexus Mods. **Expected:** a list like Prism's downloader (pictures, names, summaries), the full description
      with images on the right, more mods loading as you scroll, **Sort by** changing the order, and **Filter options**
      offering categories with counts, last update, author and adult content. Enter in the search box searches and
      keeps the filters.
- [ ] Tick two mods (checkbox or *Select mod for download*, picking a file under *File selected*), then **Review and
      confirm**. **Expected:** both listed; Premium installs them, otherwise the button says **Download** and the
      *BetterNMM — Nexus Mods downloads* window opens (no browser of yours, no Stardrop).
- [ ] Not logged in: **Expected:** the bar at the top says to log in; clicking **Slow download** does not move on. Log in
      on the page. **Expected:** Cloudflare's check on the login page passes; back on the download page the bar says *File 1 of 2 … click "Slow download"*. Next time
      the window opens you are still logged in.
- [ ] Click **Slow download** on each page as fast as you can. **Expected:** the next file's page is there immediately,
      no white or half-loaded page in between; the window closes by itself a few seconds after the last click (Nexus
      Mods' countdown), then both mods install (their installers, if any, appear only now). The archives are in the
      instance's downloads folder.
- [ ] Under "Slow download. Wait more." the page says *Throttled downloads (3MB/s)* (the first page may load once more
      for that, with *loading again for Nexus Mods' 3 MB/s free speed* in the bar). **Expected:** a single download runs
      at about 2.8 MiB/s.
- [ ] A file over 500 MB (for example SMIM). **Expected:** after **Slow download** the page stays, Nexus Mods asks
      *Standard download* or *Resumable download*, and the bar says to click **Standard download**; that click moves on.
- [ ] Ctrl with the mouse wheel, Ctrl+−, Ctrl+0. **Expected:** the page zooms out and back; the bar shows the zoom
      while it is not 100 %; the next time the window opens it has the same zoom.
- [ ] **Skip this file**, and closing the window halfway. **Expected:** skipped and unclicked files are not installed and
      nothing complains; files already clicked still download and install.
- [ ] Task Manager while the window is open, then after it closed. **Expected:** `betternmm-browser.exe` processes only
      while the window is open.
- [ ] Mods page > Check for Updates. **Expected:** the Latest column fills in; outdated mods offer Update.

## 5. Real mods

- [ ] A mod with an installer wizard (FOMOD), for example SMIM. **Expected:** the wizard with pictures; only the
      chosen options end up in the mod (Mods page > View Folder).
- [ ] Two mods that change the same file. **Expected:** the Conflicts column shows them; the lower one wins; dragging
      to reorder and Redeploy swaps which one the game sees.
- [ ] Plugins page: reorder and disable a plugin, launch. **Expected:** the game uses that order (the in-game mod list
      or LOOT started as a Tool shows it).
- [ ] Activate a patch whose master mod is not installed (or deactivate the master). **Expected:** the patch's row turns
      red and Flags says "Missing master: …" or "Inactive master: …"; installing or activating the master clears it.
- [ ] Install an archive with one folder and a Readme.txt beside it. **Expected:** the folder's contents land in Data,
      not Data/<folder>. Install a damaged archive. **Expected:** "… is damaged or incomplete", nothing installed.
- [ ] Remove a mod. **Expected:** the question names the mod; its files are gone from the instance's game folder, and
      files it had replaced are back.
- [ ] Install an archive from the Mods page (Install File) several times in a row. **Expected:** no "Deploying mods
      failed … The file exists"; the Plugins page shows each new plugin as provided by its mod. Installing the same
      file again asks "… is installed already. Replace it with this one?"; Yes keeps one copy, No adds a second.
- [ ] Clean a plugin of an enabled mod with xEdit (or save any change to it in xEdit), then disable that mod.
      **Expected:** "Changed files were kept" names the plugin; the cleaned plugin is still in the game's Data folder.
      Enabling the mod again puts the mod's own plugin back in front.
- [ ] After the xEdit step above (with the mod enabled): Mods page > **Changed Files**. **Expected:** the cleaned plugin
      is listed with its mod; **Save into Mod** closes the list, and disabling and enabling the mod keeps the cleaned
      plugin. **Restore Mod's File** instead puts the mod's plugin back and the cleaned one is in the Recycle Bin.
- [ ] Mods page > **Export List**, save as `.md`, then as `.csv`. **Expected:** the Markdown file shows the mods in order
      with versions and Nexus Mods links, then the load order; the CSV opens in a spreadsheet with one row per mod.
- [ ] Plugins page with a few hundred plugins (or Creation Club content). **Expected:** the top line says e.g.
      "slots: 230/254 full, 40/4096 light"; over 254 full plugins the count turns red, launching the game lists it
      first ("Launch Anyway" / Cancel), and a crash report names it under *Too many plugins*.
- [ ] Activate a plugin whose master is not installed and press Launch. **Expected:** "Load order problems" names the
      plugin and its master; Cancel starts nothing; launching xEdit from the Launch menu does not ask.
- [ ] Start the game, and while it runs let a Nexus Mods download finish (or Install Mod from the main window).
      **Expected:** nothing pops up over the game; the mod shows on the Mods page; once the game is closed it is
      deployed and its plugin is active.
- [ ] Mods page > **Auto Sort** with a few dozen mods installed. **Expected:** a short progress window, then "Plugins: n
      of m moved ... Mods: n of m moved". The Plugins page shows the new order (the same LOOT would give: LOOT started
      as a Tool and sorted shows no changes). **Undo** puts both orders back. Unplugged from the internet, Auto Sort
      still works and says it used the masterlist from the last time.

## 6. Steam installation instance (changes your real game folder, by design)

- [ ] Add Instance > Skyrim Special Edition > *Steam installation*. Install one small mod.
      **Expected:** its files appear in `steamapps\common\Skyrim Special Edition`.
- [ ] Game Files > *Remove All Mod Files*, or delete the instance. **Expected:** the Steam folder is back as it was.
      Do not let Vortex manage the same folder at the same time.
- [ ] Let Steam update the game (or verify its files) while a mod replaces one of the game's files, then disable the
      mod. **Expected:** "Changed files were kept" names that file; Steam's new version stays, not the old original.
- [ ] With a Steam instance of a game present, open Add Instance for that game. **Expected:** *Steam installation* is
      greyed out and names the instance that already manages it.
- [ ] Select the Steam instance. **Expected:** bottom right says *Playtime on Steam* with the time Steam shows for the
      game. Isolated instances show their own *Playtime*; with nothing selected it is the total.

## 6b. INI editor (Edit > Settings > INI Files)

- [ ] Open Skyrim.ini of an isolated instance. **Expected:** coloured sections, keys and values with line numbers; the
      list shows the My Games INIs (missing ones as "not created yet") and the game folder's INIs, mod ones "(from …)".
- [ ] *Settings* view, type `fov` in the filter, select a value, press F2 and enter `abc`. **Expected:** "takes a number"
      and the old value back. Enter `100` instead, Save (Ctrl+S). **Expected:** "Saved at …"; the file on disk changed
      only on that line.
- [ ] Change a value and close the window. **Expected:** Save / Discard / Cancel.
- [ ] Edit an INI a mod installed (for example an SKSE plugin's). **Expected:** it says it is saved into the mod;
      Redeploy keeps the change.

## 6c. INI Settings (Edit > Settings > INI Settings)

- [ ] Open it on a Skyrim Special Edition instance. **Expected:** categories on the left (Display, Quality, Shadows,
      Grass and trees, Snow and water, Blood and marks, View distance, Gameplay, Saves, Interface, Controls, Memory,
      Troubleshooting); each setting has a control and a grey explanation; the values match the instance's INI files
      (hover a name to see which INI line it is). On a fresh instance no choice reads "Custom".
- [ ] Grass and trees: lower *Grass spacing (density)* to 10, Save, play. **Expected:** Skyrim.ini `[Grass]
      iMinGrassSize=10`, visibly denser grass.
- [ ] Interface: click *Console input colour*, pick a colour, Save. **Expected:** Skyrim.ini `[Menu] rConsoleTextColor=r,g,b`
      and the console text in that colour. Type a folder in *Screenshot files*, press Print Screen in game.
      **Expected:** the screenshot lands there. *Free world map camera* writes three `[MapMenu]` lines.
- [ ] Fallout 4 instance: Interface > *Pip-Boy screen colour* and *HUD colour*. **Expected:** Fallout4Prefs.ini
      `fPipboyEffectColorR/G/B` as 0-1 numbers and `iHUDColorR/G/B` as 0-255; the game shows the colours.
- [ ] Memory: raise *Script memory* and *Exterior cell buffer*, Save. **Expected:** INI Files > Skyrim.ini shows the new
      values under `[Papyrus]` and `[General]`, nothing else changed. Without ENB the ENB settings are greyed out with a
      note; after installing ENB (enblocal.ini in the instance's game folder) they are editable.
- [ ] Click **High** next to *Quality preset*. **Expected:** "Applied the High preset (…values)", the Quality and View
      distance pages show the new values, **Revert** puts them back.
- [ ] Change something and switch to the INI Files tab. **Expected:** Save / Discard / Cancel.
- [ ] Choose *Borderless window* and play. **Expected:** the game runs borderless.

## 6d. Crash report

- [ ] Make the game crash (for example a mod known to crash, or a test instance with a broken mod). **Expected:** a
      window "<instance> crashed" naming the crash (like "access violation"), then: mods the crash log names (with
      Crash Logger SSE installed), mods made for another version of the game, plugins missing their masters, and mods
      that change the same files. Without a crash logger it links Crash Logger SSE AE VR on Nexus Mods.
- [ ] Wrong version: in a Skyrim SE test instance with SKSE, install the **SE** file of a mod that ships SE and AE
      builds (for example powerofthree's Papyrus Extender) on an AE game (1.6.x or 1.7.x) and make the game crash as
      above. **Expected:** under "Mods made for another version of the game", `po3_PapyrusExtender.dll` from that mod,
      saying it is made for an older version and to get the file made for your game version. With the right file
      installed the section says none were found.
- [ ] **Open Crash Log**, **Show Mods** and **Copy Report** do what they say.

## 7. Saved Games and LocalLow (only matters for games that save there)

- [ ] Settings > General > Saves > *Give each instance its own Saved Games and LocalLow* > Yes.
      **Expected:** the text changes to "Each isolated instance has its own ... Saved Games and AppData\LocalLow"; File
      Explorer still shows your normal Saved Games folder in its old place.
- [ ] Run `ctest --test-dir build -R Isolation` from the repo folder (MSYS2 MinGW64 shell). **Expected:** passes;
      this test now proves an instance gets its own Saved Games.
- [ ] Optional, with Cyberpunk 2077 or a Unity game: save in an instance. **Expected:** the save lives in the instance.
- [ ] The same button (*Share Saved Games and LocalLow again*) undoes it.

## 8. Instance housekeeping

- [ ] Settings > Appearance: Dark, then Bright, then System. **Expected:** Dark and Bright show the slate/amber look (amber
      selection, amber Launch button in the instance window, round radio buttons, ticked amber checkboxes); System looks like
      Windows. The taskbar, title bars, About and BetterNMM's Nexus Mods window show the "B" logo.
- [ ] An instance made by an older BetterNMM (Game Files > *Local copies* empty): launch it. **Expected:** the Game Log says
      some files were still shared with the Steam installation and are the instance's own copies now; the field shows the
      default patterns greyed out.

- [ ] Copy an instance, with *Hard link mod files* ticked. **Expected:** done in seconds; the copy plays with its own
      saves and settings.
- [ ] Export an instance and import it again (Add Instance > Import). **Expected:** same mods and settings; the game
      folder is rebuilt from your Steam install.
- [ ] Create Shortcut. **Expected:** the desktop shortcut starts that instance directly, also when BetterNMM was started
      with its own data folder (`-d`).
- [ ] Delete an instance. **Expected:** it goes to the Recycle Bin; the Steam folder is unaffected.
- [ ] Open a file of an instance in another program (for example its log in a text editor that locks it), then delete
      the instance. **Expected:** "could not be moved to the Recycle Bin … Delete it permanently instead?"; Yes says it
      is in use and nothing was deleted. Close the program and delete again.
- [ ] Add Instance > Another game or folder: type the folder, game, name and executable without pressing Tab.
      **Expected:** OK becomes clickable as soon as the last field is typed.
- [ ] After a Steam update of the game: Game Files > *Update from Steam Installation*. **Expected:** a short report;
      your INI edits and mods stay. With the game's drive disconnected: an error saying the game folder is not there,
      and the instance's game folder unchanged.

## 9. Other games you have installed

Elden Ring, Oblivion, Fallout: New Vegas, Morrowind and Stardew Valley showed up on this PC.

- [ ] One isolated instance of each, launch it once. **Expected:** it starts and saves into the instance.
- [ ] Fallout: New Vegas or Oblivion with a plugin mod. **Expected:** the Plugins page order is applied (these games
      order plugins by file date).
- [ ] Stardew Valley with SMAPI. **Expected:** the Launch dropdown offers *Script extender (StardewModdingAPI.exe)*.

## 10. Setup and coming from Vortex

Automated: `ctest --test-dir build -R VortexImport` (the Vortex database reader and the whole import of a profile into an
instance). With `BETTERNMM_TEST_REAL_VORTEX=1` set, `LauncherTests VortexImport_test test_real_vortex_read_only` also plans
an import of every profile of this PC's Vortex, reading only, and prints where each mod's files would go.

- [ ] `make-installer.bat`. **Expected:** `dist\BetterNMM-Setup-1.0.0.exe`, about 160 MB (most of it the Nexus Mods window).
- [ ] Run it with BetterNMM not installed. **Expected:** dark or light like Windows; *Just for me* proposes
      `%LOCALAPPDATA%\Programs\BetterNMM` and shows the space needed; the options page has desktop, Start menu, taskbar
      and download link options; with Vortex set up, the next page lists its profiles, the most recently used one of each
      game ticked.
- [ ] Install with *Pin to the taskbar*. **Expected:** Windows asks whether to pin BetterNMM; after Yes the taskbar
      button starts BetterNMM, and BetterNMM's window groups with it.
- [ ] With a Vortex profile ticked and *Start BetterNMM now*: **Expected:** after the first-start setup, *Import from
      Vortex* opens with that profile ticked. Import it. **Expected:** an instance named "<game> (<profile>)" with the
      profile's mods enabled as in Vortex, its load order on the Plugins page, Vortex's tools (for Elden Ring:
      *ModEngine 2*) in the Launch menu; Vortex still starts and shows everything as before.
- [ ] Settings > General > Vortex > *Import from Vortex*. **Expected:** the same window; on a PC without Vortex the
      button is greyed out and says there is nothing to bring over.
- [ ] Run the same setup again. **Expected:** "Reinstall BetterNMM", the folder fixed, the options as chosen before, no
      Vortex page. With BetterNMM running, Update says BetterNMM is running from the folder.
- [ ] *Everyone on this PC*. **Expected:** Windows asks for administrator rights; BetterNMM goes into
      `C:\Program Files\BetterNMM` and shows for every user in the Start menu.
- [ ] Settings > Apps > Installed apps > BetterNMM > Uninstall. **Expected:** the program folder, shortcuts and the entry
      go; `%APPDATA%\BetterNMM` with your instances stays.
