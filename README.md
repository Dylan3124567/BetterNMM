<p align="center">
  <img alt="BetterNMM" src="/program_info/io.github.betternmm.BetterNMM.svg" width="20%">
</p>

<h1 align="center">BetterNMM</h1>

<p align="center">A mod manager for Nexus Mods games where every setup gets its own copy of the game.</p>

Ever installed SKSE, an ENB or Skyrim Together for one playthrough, and then found it sitting in every other playthrough
too? Or loaded up an old save and realized your INI tweaks from a different modlist came along for the ride? That's
because most mod managers dump everything into the one game folder Steam gave you, and all your setups share it.

BetterNMM does it the way Prism Launcher does it for Minecraft. Every setup is its own **instance**: its own game folder,
its own saves, its own INI files and load order, its own mods. The game folder is built out of hard links to your Steam
install, so a full copy of Skyrim costs next to nothing in disk space. Want a survival run, a vanilla-plus run and a
multiplayer run side by side? Make three instances. Your normal Steam install stays clean unless you tell BetterNMM to
manage it.

Coming from Vortex? The installer can bring your Vortex profiles over: mods, load order, settings, tools and copies of
your saves. Vortex itself is only read, so it keeps working exactly like before if you want to go back.

> **AI-coded notice:** BetterNMM was written with heavy help from AI coding tools. It has been tested (there's an
> automated test suite and a lot of hands-on checking), but it's a hobby project, not a polished commercial product. Back
> up your saves before trying anything big, and please open an issue if something breaks.

BetterNMM is a fork of [Prism Launcher](https://prismlauncher.org). It isn't endorsed by or affiliated with the Prism
Launcher project or Nexus Mods.

## Download

Grab `BetterNMM-Setup-<version>.exe` from the [Releases](../../releases) page and run it. The installer isn't code-signed,
so Windows will probably show "Windows protected your PC" the first time: click **More info**, then **Run anyway**.

Windows 10 or 11, 64-bit. Games from Steam are found on their own; other installs can be added as a folder.

## What it does

- **Isolated instance**: its own game folder, its own Documents and AppData (saves, INI files, `plugins.txt`), its own mods.
  A script extender, ENB or multiplayer client installed into one isolated instance exists only there.
- **Steam installation**: manages the game folder Steam installed and your normal profile, for mods you want everywhere
  (including when the game is started from Steam).

## How instances stay small

An isolated instance's game folder is built from the Steam installation:

- Game files are **hard links** to the Steam files, so they take no extra disk space. The instance is placed in
  `<Steam library>\BetterNMM Instances` so it is on the same drive as the game (hard links can not cross drives; on another
  drive, or one without hard links such as exFAT, the files are copied, which **Add Instance** says beforehand). Copying
  stops before it starts when it would leave the drive with less than 512 MiB free, and nothing is changed.
- Files matching the **local copy** patterns (executables, DLLs, configs: `*.exe;*.dll;*.asi;*.ini;*.cfg;*.toml;*.json;*.xml;*.txt`,
  plus plugins for games that order plugins by date) are real copies, so patching them never changes the Steam installation
  or another instance. An empty field means these defaults. Before each launch, files matching the patterns that are still
  links (an instance made by an older version, or a pattern added later) become the instance's own copies.
- Only the game's own files are taken over. BetterNMM reads Steam's file list for the installed depots, so mods, crash dumps
  and leftovers someone put into the Steam folder are not copied in. Files matching the **exclude** patterns
  (`*.dmp;*.log;steam_appid.txt` by default) and files other mod managers deployed are left out as well.
- **Update from Steam Installation** (Game Files page) brings the instance up to date after Steam updates the game, keeping
  files that were changed inside the instance. When the game folder is not there or has no game files left (a drive that is
  not connected, a moved or uninstalled game) it stops and changes nothing, and files Steam lists but the folder lacks stay
  in the instance.

## Profiles, saves and INI files

When an isolated instance starts, BetterNMM points the game's user folders at `<instance>\profile`: Documents (and with it
`My Games`), `AppData\Local` and `AppData\Roaming`. Saves, INI files and the load order written there belong to that instance
alone. A new instance starts with copies of your current INI files and load order; your saves are never read, copied or
changed (bringing a profile over from Vortex copies them into the instance when you ask it to, and leaves the originals as
they are).

Windows only gives a program its own **Saved Games** and **AppData\LocalLow** when your Windows user folder settings name
them relative to your profile, which they do not by default. Until then, games that save there (Cyberpunk 2077, many Unity
games) share those saves between instances, and the launch log says so. **Settings > General > Saves** records the current
places of both folders in that form (`%USERPROFILE%\Saved Games`, the way Windows names Documents); nothing moves and other
programs keep using the same folders. The same button undoes it. Folders you moved elsewhere are left alone.

When OneDrive backs up Documents (`%USERPROFILE%\OneDrive\Documents`), the instance's Documents follows the same layout.

## Installing

`BetterNMM-Setup-<version>.exe` (built with `make-installer.bat`, below) is a small wizard with no runtime of its own:

- **Just for me** installs into `%LOCALAPPDATA%\Programs\BetterNMM` without administrator rights; **Everyone on this PC**
  into `%ProgramFiles%\BetterNMM`, after Windows asks for them. The folder can be changed; the page shows how much space
  it takes and how much the drive has. Only the program goes there: instances, mods, downloads and settings live in
  `%APPDATA%\BetterNMM`, so updating, reinstalling or uninstalling never touches them.
- Options: a **desktop shortcut**, a **Start menu shortcut**, **pin to the taskbar** (Windows asks you to confirm; it needs
  the Start menu shortcut), BetterNMM as the handler of Nexus Mods **download links**, and **Start BetterNMM now**.
- Running setup again (any version) **updates** the installed copy in its folder and keeps the options chosen
  before. The new files are unpacked next to the old ones and swapped in; if anything fails the old version is put back.
  Setup will not update while BetterNMM runs from that folder and says so.
- BetterNMM appears in Windows' **Installed apps** (Settings > Apps); uninstalling removes only the files setup put there,
  its shortcuts and registry entries, and the download link handler when it is BetterNMM's.
- Unattended: `BetterNMM-Setup-<version>.exe --silent [--dir <folder>] [--scope user|machine] [--desktop 0|1]
  [--startmenu 0|1] [--nxm 0|1] [--launch 0|1]` and `uninstall.exe --silent`. Exit codes: 0 done, 1 failed, 3 BetterNMM is
  running from the folder, 4 BetterNMM is installed elsewhere (uninstall it first to move it). The log is
  `%TEMP%\BetterNMM-Setup.log`.

## Coming from Vortex

Setup lists the Vortex profiles of the games BetterNMM supports and brings the ones you tick over the first time BetterNMM
starts. **Settings > General > Vortex > Import from Vortex** does the same at any time.
Vortex, its mods folder and your saves are only read: Vortex keeps working exactly as before. Each profile becomes an
isolated instance of the game installation Vortex manages, with:

- **Mods**: every mod Vortex has installed for the game, enabled as in the profile, with its Nexus Mods page, version and
  archive name, so update checks keep working. Files are placed where Vortex deploys that kind of mod (read from the
  deployment manifests Vortex keeps; Bethesda games' `Data`, The Witcher 3's `Mods`, Elden Ring's `Game`...). They are
  hard links to Vortex's copies when both are on one drive (configs and programs are real copies, as with game files),
  otherwise copies; the import checks there is room first.
- **Order**: Vortex's *load before / load after* rules, and for the profile Vortex deployed last, which mod's file actually
  won each conflict, so the game sees the same files.
- **Load order and settings**: the profile's `plugins.txt` and `loadorder.txt`, and its own INI files when the profile has
  them.
- **Saves**, when ticked: copies of the profile's own saves (Vortex's per-profile saves) or of the game's saves.
- **Tools** Vortex starts for the game (ModEngine 2, script extender launchers, LOOT...) become the instance's programs,
  and the archives Vortex downloaded for the game go into BetterNMM's downloads folder.

## Using it

1. **Add Instance** lists the games in your Steam libraries. Pick one, choose *Isolated instance* or *Steam installation*,
   and confirm. Each Steam installation can have one Steam instance. Tick *Install the script extender* to get the build
   made for your game's version from Nexus Mods (also on the Mods page: **Install Script Extender**).
2. **Accounts** (optional): browsing, update checks and downloads work without anything here; the download window has
   you log in to Nexus Mods the first time. A personal API key (from <https://www.nexusmods.com/settings/api-keys>)
   lets Premium members download without clicking, and turns website "Mod Manager Download" links straight into files.
3. Install mods:
   - **Install Mod** / **Install File** or drag archives (zip, 7z, rar) onto the Mods page. Installing a mod that is
     there already (the same Nexus mod, or for a file from disk the same name) asks whether to replace it in place or
     install it next to it. A damaged or incomplete archive is reported and nothing of it is installed.
   - **Browse Nexus Mods** works like Prism's mod downloader: search, sort, filter by category, update date, author and
     adult content, scroll through all results, read the full description, pick a file and select several mods, then
     **Review and confirm**. Like Prism, the review adds what the chosen mods require on Nexus Mods (and what those
     require), ticked and marked *Required by*, unless the instance has it already; the script extender comes as the
     build for your game version. Requirements BetterNMM can not download (hosted elsewhere, removed from Nexus Mods,
     for another game) are listed with a link. Premium members download directly. Nexus Mods lets other accounts download only with the
     **Slow download** button on its website, clicked by the person (Vortex and Mod Organizer work the same way), so
     BetterNMM opens its own Nexus Mods window (Chromium in a cut-down Electron, `browser\betternmm-browser.exe`) with
     each file's download page in turn. It ships no patented video or audio decoders: H.264 and HEVC video play through
     the graphics card, AAC audio (in some ads) does not play. Ctrl with the mouse wheel, or Ctrl and + − 0, zooms the
     pages, and the window keeps the zoom for next time. Nothing in the window blocks ads, so it gets Nexus Mods' free
     speed for that (3 MB/s rather than 1.5): it runs Nexus Mods' own ad check again once the site's consent manager has
     answered, which the page itself checks too early. Log in there once; after that it is one click per file: the next page is already
     loaded and appears the moment you click, while the clicked one starts its download out of sight. Files download
     into the instance's downloads folder, the window closes by itself when the last one has started, and then everything
     installs. Updates from the Mods page work the same way.
   - **Mod Manager Download** buttons on the website work once BetterNMM handles `nxm://` links
     (Settings > General > *Handle Nexus Mods download links*; BetterNMM also offers this the first time it is needed).
   - Downloads run several at a time (Settings > General > *Concurrent Download Limit*, 6 by default) and install one
     after another as they finish. **Downloads** shows each one's progress and speed, with pause, resume and cancel. A
     dropped or stalled connection picks up where it stopped, so does a download after BetterNMM restarts, and an
     expired Nexus Mods address is renewed.
   - FOMOD installers (`fomod/ModuleConfig.xml`) open as a wizard. C# script installers can not run; BetterNMM asks before
     installing such an archive's files as they are.
   - Nemesis is set up when it is installed: after a deploy it is pointed at the instance's Data folder, gets its cache
     folder and language, has the patches other mods brought ticked, gets its engine updated when those change, and
     gets a Launch entry whose output goes into the mod *Nemesis Output*.
   - While the game runs, mods still install (a download finishing mid-game): nothing pops up over the game, and the game
     folder is left alone until the game stops; then the new mods are deployed and their plugins switched on.
4. **Mods page**: enable, disable, reorder (lower wins when two mods have the same file), check for updates, open on Nexus.
   Mods are hard-linked into the game folder; files they replace are kept aside and put back when the mod is removed.
   A deployed file that something else replaced since (xEdit saving a cleaned plugin, a Steam update, you) is never
   deleted: disabling, removing or updating its mod leaves it in the game folder and says so, with where the game's own
   file it had replaced is kept. A file a program has open stops deployment with its name and nothing half done.
   **Changed Files** lists such files: **Save into Mod** makes the change part of its mod (a plugin you cleaned with xEdit
   then stays cleaned when the mod is disabled and enabled again), **Restore Mod's File** moves the changed file to the
   Recycle Bin and puts the mod's file back. A mod file deleted from the game folder comes back with the next deploy.
   **Export List** saves the mods (order, on/off, version, Nexus Mods page) and the load order as Markdown, CSV or text,
   for a support thread or a guide.
   **Add Separator** puts a heading above the selected mod that groups the mods below it; double-click it or press F2 to rename it,
   click its arrow to fold its mods away. A folded group moves as a whole, Auto Sort sorts within each group, and
   exports show the headings.
   **Ignore Updates** stops pointing out newer versions of the selected mods (their *Latest* column says *ignored*) and
   leaves them out of **Update**; choosing it again undoes it.
   **Conflicts** (or a double-click on the *Conflicts* column) lists the files of the selected mods, or of every enabled
   mod, with the mod whose copy the game gets and the other mods that have the file. **Hide** takes a file out of the
   game folder while leaving it in its mod (renamed to *name*.mohidden), so the next mod with it wins; **Show** brings it
   back. Hidden files stay hidden when their mod is reinstalled or updated.
   **Change Installer Options** runs the selected mod's FOMOD installer again with the options it has now already
   picked (those you chose, or for an older install the ones its files show) and installs it anew with your changes, in
   the same place in the list. **Check Patches** finds compatibility patches the installers of your mods offer for the
   other mods you have, and adds the ones you tick. Both read the mod's installer again, so BetterNMM keeps its own copy
   of every archive with a FOMOD installer in the instance's `mods\.archives` folder: a hard link when the archive is on
   the same drive (no extra space), a copy otherwise. Moving or deleting your downloads changes nothing, and removing a
   mod removes its copy. Mods installed before 1.2.1 get theirs the first time Check Patches finds their archive (where
   they were installed from, in BetterNMM's downloads folder or in your Downloads folder; **Look in Folder** finds it
   anywhere else). An archive deleted before that can be fetched with **Download Again**: it finds the very file on Nexus
   Mods (by the ids BetterNMM recorded or by the archive's name) and only downloads it, for the next check to keep.
   **Auto Sort** orders everything in one click:
   - Plugins (Skyrim Special Edition, Skyrim, Fallout 4, Fallout: New Vegas, Fallout 3, Oblivion, Starfield) are sorted
     the way LOOT 0.29 sorts them: BetterNMM carries its own port of LOOT's sorter (masters, LOOT's masterlist groups,
     load-after and requirement rules with their conditions, overlapping records and archive files, the game's fixed
     plugins and Creation Club list), so LOOT itself is not needed. LOOT's masterlist (CC0) is downloaded each time and
     kept in `<BetterNMM data>\loot\`, so sorting still works offline with the last copy.
   - Mods that have the same files are put in an order that agrees with that: a mod wins over the mods it needs (its
     Nexus Mods requirements), then the mod whose plugin loads later wins, otherwise the current winner stays. Mods that
     share no files keep their place.
   - A summary says how many plugins and mods moved, with **Undo**. A contradiction LOOT would refuse (for example two
     plugins that each must load after the other) is reported with the plugins and rules involved, and nothing changes.
5. **Plugins page** (Bethesda games): activate and order plugins. The load order is written into the instance's profile
   before every launch. An active plugin whose masters are missing or inactive is shown in red with the masters it
   needs, since the game crashes while loading it. The top line counts the plugin slots in use: Skyrim Special Edition
   and Fallout 4 load at most 254 full plugins plus 4096 light (ESL) ones; Skyrim, Oblivion, Fallout 3 and New Vegas 255
   plugins in all. Too many shows in red.
   **Health page**: what keeps the mods from working, each with a fix. Active plugins whose masters are missing (or
   only in a disabled mod), inactive, or load after them; and, after **Check** asks Nexus Mods, enabled mods whose
   requirements are not installed, are disabled, or (the script extender) are not in the game folder. **Fix** does what
   the *Fix* column says: activate, deactivate or move plugins, enable mods, or install the requirement from Nexus
   Mods. **Find on Nexus** opens a requirement's page or searches for a missing master.
6. **Launch** starts the game, the script extender when one is installed, or a tool (Settings > Tools: xEdit, LOOT,
   BodySlide, ...). Tools see the instance's game folder, load order and profile exactly like the game; a tool inside
   the game folder is kept as a path relative to it, so copies and exports of the instance use their own copy.
   When Steam is closed, launching an isolated instance of a Steam game starts Steam first and waits until you're logged
   in: a game that finds Steam closed starts it itself, and Steam then has the instance's profile and runs the game again
   from the Steam installation (with whatever is installed there). A launch also warns when Steam is still running with an
   instance's profile from such a start; exit Steam and start it again.
   A tool with an *Output mod* (Settings > Tools; a tool you add gets *Name Output*) has the files it writes into the
   game folder moved into that mod once it exits, also when it crashed or was killed: generated meshes, patches and
   plugins become a mod you can disable, reorder or remove. The mod is made at the end of the list the first time and
   switched on when needed. Logs, crash dumps and the tool's own folder are left alone; a mod's file the tool wrote
   into is copied into the output mod (the log names it), and game files it changed stay where they are.
   **Kill** ends the game without marking the instance as crashed. Starting the game (not a tool: xEdit or LOOT are how
   such a load order gets fixed) with an active plugin whose masters do not load, or with more plugins than the game
   has slots for, first lists the problems; **Launch Anyway** goes on, Cancel (the default) does not.
7. **Settings > INI Settings** (Skyrim Special Edition, Skyrim, Fallout 4) shows the game's settings like BethINI: named
   controls with an explanation each, plus the game's own Low/Medium/High/Ultra presets. Skyrim Special Edition and
   Fallout 4 have every setting BethINI Pie offers (about 190 and 160), in Display, Quality, Shadows, Grass (and trees),
   Snow and water, Blood and marks, View distance, Gameplay, Saves, Interface, Controls, **Memory** and Troubleshooting:
   grass density and wind, snow sparkles, water reflections, shadow and ambient occlusion tuning, field of view, physics
   rate, colours (subtitles, console, HUD, Pip-Boy), the console, the world map, screenshots and more. Skyrim keeps its
   own smaller set. Memory covers the cell buffers, the Papyrus script
   memory, ENB's `enblocal.ini` memory settings (once ENB is installed) and, for Skyrim, SKSE's heap settings. Values are
   read the way the game reads them (`SkyrimCustom.ini` wins) and saved where the game takes them from; every other line
   of the files stays as it was. Settings are described in `launcher/resources/games/settings.json`.
8. **Settings > INI Files** edits the instance's INI files: the game's settings in its My Games folder and every INI in its
   game folder, as text or as a filterable list of settings whose values are checked against their type. Saving never
   writes through a hard link, and INI files a mod installed are saved into that mod.
9. When the game crashes, a window says how it crashed and which mods are likely involved: mods whose files the crash log
   names (Crash Logger SSE, Buffout 4, Trainwreck, .NET Script Framework and others; without one installed it links the
   right crash logger), mods made for another version of the game, plugins whose masters are missing or inactive, and
   mods that overwrite each other's files. For the version check it reads the game's version from its executable and
   applies the plugin loading rules of SKSE64, F4SE or SFSE for that version: a script extender plugin (DLL) built for
   another game version or needing an Address Library that is not installed, a script extender built for another game
   version, and plugins (.esp/.esm/.esl) whose header is older than the game's. Each one names its mod and says which
   file to get instead. More active plugins than the game has slots for are named too.
10. The status bar shows the selected instance's playtime; for a Steam installation it is the time Steam counted.
11. **Delete** moves the instance to the Recycle Bin (Edit > Undo Last Instance Deletion brings it back). When that is not possible (a drive
    without one, or a program has one of its files open) BetterNMM asks before deleting it for good, and a file in use
    stops the delete before anything is deleted.
12. **Settings > Appearance**: *Dark* and *Bright* are BetterNMM's own look (slate with an amber accent, flat toolbars, the
    instance window's Launch button in the accent colour); *System* uses Windows' own. The logo is a "B" made of two mod
    layers: the upper one lies over the lower, and where both have a file (the middle bar) is the one that wins.

### Command line

```
betternmm [--dir <data folder>] [--launch <instance id> [--executable <name>]] [--show <instance id>] [--import <file or nxm link>] [nxm link]
```

## Supported games

Skyrim Special Edition, Skyrim, Fallout 4, Fallout: New Vegas, Fallout 3, Oblivion, Morrowind, Starfield, Cyberpunk 2077,
The Witcher 3, Stardew Valley, Mount & Blade II: Bannerlord, Dark Souls III and Elden Ring, plus any other game folder with
its Nexus Mods game id set by hand. Game definitions live in
[launcher/resources/games/games.json](launcher/resources/games/games.json).

## Limitations

- Nexus Mods collections.
- C# script FOMOD installers.
- Platforms other than Windows, and game stores other than Steam (other installs can be added as a plain folder).

## Building

Double-click `start.bat`: it builds what changed, bundles BetterNMM with its Qt runtime in `install\` (that copy runs on
its own, which `nxm://` links need) and starts it. `make-installer.bat` builds the release and packs it into
`dist\BetterNMM-Setup-<version>.exe` (CMake target `setup`). The first run takes a few minutes and downloads the Chromium runtime
for the Nexus Mods window (Electron 45.3.3 and its FFmpeg without patented codecs, about 160 MB, checked against their
SHA-256) into `.cache\electron`, once for all build folders; a computer that cannot download them can put the two zip
files from Electron's GitHub release there by hand. By hand:

Windows with [MSYS2](https://www.msys2.org), packages `mingw-w64-x86_64-` + `qt6-base`, `qt6-svg`, `libarchive`,
`extra-cmake-modules`, `cmake`, `ninja`, `gcc`. From an MSYS2 MinGW64 shell:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/c/msys64/mingw64
cmake --build build
ctest --test-dir build
```

## License

All code is available under the GPL-3.0-only license; see [LICENSE](LICENSE) and [COPYING.md](COPYING.md) for the notices
of the work it is based on. The BetterNMM logo is available under the CC BY-SA 4.0 license
([program_info/LICENSE](program_info/LICENSE)).
