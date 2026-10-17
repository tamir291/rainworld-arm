# Rain World на ARM64 Linux — 60 FPS на Orange Pi 4 Pro (PowerVR BXM)

Запуск **Windows-версии Rain World** на **ARM64-одноплатнике** через **box64 + Wine** с рендером
на **GPU PowerVR через Vulkan**. На Orange Pi 4 Pro (Allwinner A733, PowerVR BXM-4-64, 4 ГБ RAM)
игра идёт в полном 1366×768 при ~60 FPS.

> Неофициальный фанатский проект, не связан с Videocult и Akupara Games.
> **Игра должна быть куплена в Steam.** В репозитории нет файлов игры: установщик скачивает её
> с *вашего* Steam-аккаунта.

[English](README.md) · [Как это устроено (англ.)](docs/HOW-IT-WORKS.md) · [Подробный отчёт](docs/report.ru.md)

## Результаты (Orange Pi 4 Pro, Ubuntu 26.04)

| Вариант | Меню | Игра |
|---|---|---|
| box64 + Wine, программный OpenGL (llvmpipe) | 4–6 FPS | ~1 FPS |
| + мод OpiPerf, режим `--cpu` (низкое внутреннее разрешение) | ~50 FPS | 30–58 FPS |
| **GPU: Vulkan-рендер wined3d + слой-обманка** | **53–60 FPS** | **59–60 FPS** |

При загрузке новой комнаты бывают фризы на 0.1–2.5 с.

## Установка

```bash
git clone https://github.com/tamir291/rainworld-arm.git
cd rainworld-arm
bash install.sh                 # спросит логин Steam (пароль и Steam Guard уходят только в Steam)
# или из готовой папки с Windows-версией игры:
bash install.sh --from-dir /путь/к/RainWorld
```

Root не нужен. Запуск: `rainworld` (GPU) или `rainworld --cpu` (программный запасной режим),
либо ярлыки в меню. В игре: F7/F8 — масштаб рендера, F9 — счётчик FPS.

## Требования

- ARM64 (aarch64) Linux со **страницами 4K** (Raspberry Pi 5: `kernel8.img`), X11, systemd.
- Для сборки box64 (если его нет в системе): `git cmake gcc make`.
- Vulkan-драйвер (`libvulkan1`), Mesa (для `--cpu`), `curl unzip xrandr`.
- ~6 ГБ места, 4 ГБ RAM (желательно со swap).
- GPU-режим сделан и проверен **только на PowerVR BXM-4-64**. Если GPU умеет DXVK
  (Mali + panvk, Adreno + turnip), лучше DXVK. `--cpu` работает везде, где есть Mesa.

## Осторожно: GPU может повесить всю плату

Первая версия слоя вешала систему целиком: wined3d ставил 16 вьюпортов, а драйвер умеет один.
Это исправлено, но слой всё равно обходит ограничения драйвера. Перед экспериментами включите
аппаратный watchdog, тогда зависшее ядро само перезагрузит плату:

```bash
sudo bash tools/enable-watchdog.sh        # откат: sudo bash tools/enable-watchdog.sh undo
```

## Ограничения

- DLC (Downpour, The Watcher) без Steam-клиента не активируются, а сам клиент здесь не запускается:
  64-битному клиенту нужен `binfmt_misc`, которого нет в некоторых вендорских ядрах.
- Wine 11.x под box64 0.4.4 падает, поэтому установщик закрепляет Wine 10.0.
- В полноэкранном режиме игра прячет курсор, меню управляются клавиатурой или геймпадом.

## Лицензии

Свой код (слой, мод, лаунчер, установщик) — [MIT](LICENSE). Сторонние компоненты установщик
скачивает сам, в репозиторий они не входят: [THIRD_PARTY.md](THIRD_PARTY.md).
