# How it works

Target: Orange Pi 4 Pro — Allwinner A733 (2× Cortex-A76 + 6× Cortex-A55), PowerVR BXM-4-64,
3.8 GB RAM, vendor kernel 6.6, Ubuntu 26.04, X11. Game: Rain World 1.11 (Unity 2020.3, Mono,
Direct3D 11, BepInEx 5 bundled).

## 1. Getting it to start

- **box64 0.4.4** runs the x86_64 code (Wine + Unity + Mono JIT output) with its dynarec and
  wraps native ARM64 libraries (X11, Vulkan, GL, audio) instead of emulating them.
- **Wine 10.0 amd64-wow64** (Kron4ek). Wine 11.x aborts under box64 0.4.4 with
  `anon_mmap_alloc: Assertion '!(size & host_page_mask)'`.
- The vendor kernel has **no `binfmt_misc`**, so x86 binaries can't be exec'd directly. Wine itself
  is fine (box64 intercepts its execs), but the Steam client isn't: its 32-bit part crashes in
  box32, and the 64-bit SteamRT3 client spawns x86 helpers through native glib/sh. Hence the game
  is downloaded with **DepotDownloader** (native ARM64) from the user's own account.

## 2. Why the first try ran at ~1 FPS

The PowerVR driver offers GLES 3.2 and Vulkan 1.3, but no desktop OpenGL, so Wine's OpenGL
renderer ends up on **llvmpipe** (software). Per-thread CPU accounting (`/proc/PID/task/*/stat`)
showed all `llvmpipe-N` threads at ~100%; sampling them with gdb landed in JIT'ed fragment
shader code. So: fill-rate bound on the CPU.

**DXVK** would be the normal answer, but this driver lacks `VK_EXT_robustness2`
(`nullDescriptor`), BC texture compression and geometry shaders — DXVK refuses to start.
Zink fails for the same `nullDescriptor` reason.

## 3. OpiPerf mod (software path)

Rain World's Futile engine already renders its camera into a `RenderTexture` and shows it with a
UI `RawImage`. The mod swaps in a smaller RT (1/4 → 342×192) with point filtering:
4–6 → ~22 FPS in the menu.

Turning the camera off entirely still gave only ~28 FPS: Unity + wined3d do several full-window
passes per frame (UI blit, backbuffer, present + scale), all on llvmpipe. Fix: force a tiny game
window (half the screen) and let the **X server upscale it** with
`xrandr --scale-from` — Xorg's glamor runs on the GPU via GLES. → ~44 FPS.

The main menu then dropped to 7 FPS once its background loaded: 15 depth layers using the
`SceneBlur` shader. The game has a built-in switch, `StreamingAssets/flatmode.txt`, that uses one
flat illustration instead. → ~50 FPS in menus, 30–58 FPS in gameplay.

Debugging was done without touching the keyboard: the mod polls `opiperf.cmd` in the game folder
(`scale N`, `cam 0|1`, `res W H`, `kill SHADER`, `dump`, `story` …) and logs FPS and frame
hitches (with GC counts — hitches turned out not to be GC).

## 4. The GPU path: wined3d's Vulkan renderer

wined3d has its own Vulkan backend (`WINE_D3D_CONFIG=renderer=vulkan`), separate from DXVK and
less demanding. It failed with *"None of the requested D3D feature levels is supported"*: in
`adapter_vk.c`, feature level 10 requires `multiViewport`, `geometryShader` and
`pipelineStatisticsQuery`, and level 11 also `tessellationShader`. PowerVR has none of the four;
everything else needed is there. Rain World (2D) uses none of them.

`VK_LAYER_OPI_fakefeatures`:
1. `vkGetPhysicalDeviceFeatures[2]`: reports the four features as supported → wined3d picks FL 11.1.
2. `vkCreateDevice`: strips them from `pEnabledFeatures` / `VkPhysicalDeviceFeatures2`.

**v1 hung the whole board** (black screen, no SSH). Reason: wined3d sets
`viewportCount = multiple_viewports ? 16 : 1` in every pipeline (`context_vk.c`) — 16 viewports
on a device that supports one is undefined behaviour, and this driver took the kernel down with it.

v2 also sanitizes what reaches the driver:
- `vkCreateGraphicsPipelines`: viewport/scissor count → 1, GS/tess stages dropped;
- `vkCmdSetViewport/Scissor[WithCount]`: only viewport 0 is passed through;
- `vkCreateQueryPool(PIPELINE_STATISTICS)`: refused.

Result: full 1366×768, 59–60 FPS in gameplay, ~2 CPU cores busy instead of 6.

## 5. Other notes

- Pinning the main thread to the A76 cores with `taskset` made hitches worse (up to 5 s) — not used.
- `systemd-run --user --scope -p MemoryMax=…` caps the game so the OOM killer hits it, not the board.
- The hardware watchdog (`tools/enable-watchdog.sh`) is strongly recommended while experimenting.
