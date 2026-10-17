# Rain World on ARM64 Linux — 60 FPS on Orange Pi 4 Pro (PowerVR BXM)

Run the **Windows version of Rain World** on an **ARM64 single-board computer** with
**box64 + Wine**, using the **PowerVR GPU through Vulkan** — full 1366×768 at ~60 FPS on an
Orange Pi 4 Pro (Allwinner A733, PowerVR BXM-4-64, 4 GB RAM).

> Unofficial fan project, not affiliated with Videocult or Akupara Games.
> **You need to own Rain World on Steam.** This repository contains no game files —
> the installer downloads the game from *your* Steam account.

[Русская версия](README.ru.md) · [How it works](docs/HOW-IT-WORKS.md)

## Results (Orange Pi 4 Pro, Ubuntu 26.04)

| Setup | Menu | Gameplay |
|---|---|---|
| box64 + Wine, software OpenGL (llvmpipe) | 4–6 FPS | ~1 FPS |
| + OpiPerf mod, `--cpu` mode (low internal res) | ~50 FPS | 30–58 FPS |
| **GPU mode: wined3d Vulkan + fake-features layer** | **53–60 FPS** | **59–60 FPS** |

Short hitches (0.1–2.5 s) happen while a new room loads.

## What's inside

- **`vk-layer/`** — `VK_LAYER_OPI_fakefeatures`, a tiny Vulkan layer. PowerVR's Vulkan driver lacks
  `geometryShader`, `multiViewport`, `pipelineStatisticsQuery` and `tessellationShader`, so Wine's
  wined3d refuses to expose Direct3D feature level 10/11 and Unity games won't start. The layer
  reports those features, strips them from `vkCreateDevice`, and sanitizes everything that would
  otherwise reach the driver (viewport/scissor count → 1, GS/tess stages dropped,
  pipeline-statistics query pools refused). DXVK still can't run on this GPU (no `robustness2`,
  no BC textures), but wined3d's own Vulkan renderer can.
- **`mod/`** — **OpiPerf**, a BepInEx plugin: internal render scale (F7/F8), FPS counter (F9),
  forced window size, hitch logging, shader swapping and debug commands via a file.
- **`launcher/rainworld`** — starts the game in GPU mode, or `--cpu` for the software fallback.
- **`install.sh`** — no-root installer: box64 (system one or built from source), Wine 10.0,
  DepotDownloader, the game from your Steam account, the mod, the layer, launcher and shortcuts.

## Requirements

- ARM64 (aarch64) Linux with **4K pages** (Raspberry Pi 5: use `kernel8.img`), X11, systemd.
- Recent glibc if you use a prebuilt box64; otherwise `git cmake gcc make` (the installer builds it).
- Vulkan driver (`libvulkan1`), Mesa (for `--cpu` mode), `curl unzip xrandr`.
- ~6 GB of disk, 4 GB RAM (+ swap recommended).
- GPU mode was made and tested **only on PowerVR BXM-4-64**. On other GPUs:
  if your driver already has these features the layer changes nothing; if your GPU supports DXVK
  (e.g. Mali with panvk, Adreno with turnip) DXVK is the better route. `--cpu` works anywhere Mesa does.

## Install

```bash
git clone https://github.com/tamir291/rainworld-arm.git
cd rainworld-arm
bash install.sh                 # asks for your Steam login (password and Steam Guard go to Steam only)
# or, if you already have the Windows game folder:
bash install.sh --from-dir /path/to/RainWorld
```

Run with `rainworld` (GPU) or `rainworld --cpu` (software fallback), or use the menu shortcuts.

## Safety: the GPU can hang the whole board

An early version of the layer froze the entire system (wined3d used 16 viewports on a driver
that supports one). That's fixed, but this is still a workaround around missing driver features.
Before experimenting, enable the hardware watchdog so a kernel hang reboots the board by itself:

```bash
sudo bash tools/enable-watchdog.sh        # undo: sudo bash tools/enable-watchdog.sh undo
```

## Known limitations

- DLC (Downpour, The Watcher) are not unlocked without the Steam client, which doesn't run here
  (its 64-bit client needs `binfmt_misc`, which some vendor kernels don't have).
- Wine 11.x crashes under box64 0.4.4 — the installer pins Wine 10.0.
- The game hides the mouse cursor in fullscreen; menus work with keyboard/gamepad.

## Credits & licenses

Own code (layer, mod, launcher, installer) — [MIT](LICENSE). Third-party components are downloaded
by the installer, not bundled — see [THIRD_PARTY.md](THIRD_PARTY.md).
Thanks to [box64](https://github.com/ptitSeb/box64), [Wine](https://www.winehq.org/),
[Kron4ek's Wine builds](https://github.com/Kron4ek/Wine-Builds),
[DepotDownloader](https://github.com/SteamRE/DepotDownloader) and [BepInEx](https://github.com/BepInEx/BepInEx).

*Keywords: Rain World ARM, Rain World Linux ARM64, box64 Wine Unity, PowerVR Vulkan wined3d,
Orange Pi 4 Pro gaming, Allwinner A733, BXM-4-64, SBC gaming, Wine D3D11 without geometry shader.*
