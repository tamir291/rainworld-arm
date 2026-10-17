# Third-party components

Nothing below is stored in this repository; `install.sh` downloads it from the original sources.

| Component | Source | License |
|---|---|---|
| box64 v0.4.4 (built from source if not installed) | https://github.com/ptitSeb/box64 | MIT |
| Wine 10.0 amd64-wow64 build | https://github.com/Kron4ek/Wine-Builds (Wine: https://www.winehq.org) | LGPL-2.1+ |
| DepotDownloader 3.4.0 | https://github.com/SteamRE/DepotDownloader | GPL-2.0 |
| BepInEx 5 | ships with Rain World itself | LGPL-2.1 |
| Rain World | Steam app 312520 — you must own it | proprietary, © Videocult / Akupara Games |

`prebuilt/OpiPerf.dll` and `prebuilt/libVkLayer_opi_fakefeatures.so` are builds of this
repository's own code (MIT). The mod is compiled against the game's managed assemblies, which are
not included — to rebuild it you need your own copy of the game (see `mod/OpiPerf/OpiPerf.csproj`).
