# VOID for the M-VAVE FM-1

VOID is an ambient instrument that runs on the FM-1 as its own firmware: drones played as chords, melody, bass, drums,
shards, a shimmer cloud, LFOs and scenes. The manual is in [app/docs/MANUAL.md](app/docs/MANUAL.md).

VOID is built on [fm1-base](https://github.com/charlesvestal/fm1-base), which `build.sh` fetches at a fixed commit.

## Getting it onto your FM-1 (no installs on your computer)

GitHub builds the firmware for you, because the FM-1's compiler only downloads on Linux.

1. Make a new repository on GitHub, for example `void`, and upload everything in this folder to it, including the
   hidden `.github` folder. (On github.com: **Add file → Upload files**. Drag in the folder's contents.)
2. In the repository, open **Settings → Pages** and set **Source** to **GitHub Actions**.
3. Open **Actions → Build VOID → Run workflow**. It takes about 10 minutes.
4. When it's done, the run's page links your site, `https://YOUR-NAME.github.io/void/`. It has:
   - **Install**: in Chrome or Edge, connect the FM-1 by USB and press Install.
   - **Try it in the browser**: VOID running in the browser emulator.
   - **Manual**.
   The firmware file (`void-0.1.fwsc`) is also under **Artifacts** on the run's page.

To make a new version later, run the workflow again and type a new version number (0.2, 0.3, …).

**Going back to the stock firmware:** the install page's last section writes M-VAVE's own firmware back. If the FM-1
ever fails to start, it comes up in SAFE MODE after two tries, and USB still works there for a new install or the
stock firmware.

## Building on your own computer

```sh
./build.sh --host                        # the simulator: app/build/host/void_host SCRIPT OUTDIR
fm1-base/tools/get_toolchain.sh          # JieLi's toolchain, once (Linux x86-64; macOS through Docker)
git clone --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
./build.sh                               # app/build/void.fwsc
python3 fm1-base/tools/fm1_install.py app/build/void.fwsc   # (pip install mido python-rtmidi)
```

## What's where

| | |
|---|---|
| `app/app.c` | the FM-1 side: pages, keys, chords, step entry, scenes, the screen, MIDI |
| `app/dsp/engine.c` | the sound: drones, melody, bass, shards, cloud, echo, tape, LFOs, the sequencers |
| `app/dsp/drums.c` | the drum voices and kits |
| `app/dsp/tables.c` | scales, chords, voicings, presets, names |
| `app/fm1.toml`, `app/app.h` | fm1-base's build settings |

Licence: MPL-2.0 (LICENSE).
