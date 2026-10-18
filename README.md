<!--
  kdenlive-morphcut - README.md
  Description, installation and usage.

    Copyright (C) 2026 Thorsten Schnebeck <thorsten.schnebeck@gmx.net>
    Produced by Thorsten Schnebeck - the idea, the decisions, the testing.
    Written by Anthropic Claude Opus 5.5 - AI generated content.

    Free software under the GNU General Public License, version 3 or later.
    There is no warranty, to the extent permitted by law. The full text is in
    LICENSES/GPL-3.0-or-later.txt.

  SPDX-FileCopyrightText: (C) 2026 Thorsten Schnebeck <thorsten.schnebeck@gmx.net>
  SPDX-FileContributor: Anthropic Claude Opus 5.5 (AI generated content)
  SPDX-License-Identifier: GPL-3.0-or-later
-->

# Morph Cut for Kdenlive

A composition (transition) for Kdenlive 26.08 / MLT 7 that hides jump cuts,
similar to *Morph Cut* in Adobe Premiere. Typical case: an "uhm" was cut out
of an interview, and the speaker's head jumps at the cut.

Morph Cut does not crossfade. It **generates the intermediate frames**, using
the AI frame interpolation [RIFE](https://github.com/hzwer/Practical-RIFE)
(ncnn/Vulkan; runs on AMD, NVIDIA and Intel GPUs, or on the CPU as a fallback).

## How it works

```
Timeline:   … A A A A [ M M M M M M ] B B B B …
                      ^             ^
                 key frame A    key frame B
```

* Key frame A is the outgoing clip at the **first** frame of the composition.
* Key frame B is the incoming clip at the **last** frame of the composition.
* RIFE computes every frame in between (M). What the clips would show
  *during* the composition is ignored, because that is exactly the material
  that was cut away.

Key frames are rendered with the clip's own effects (e.g. color correction),
and the generated frames are cached.

## Installation

Requirements: `cmake` (≥ 3.28), `g++`, `git`, a Vulkan driver (on AMD,
Mesa/RADV is usually already installed) and Kdenlive with MLT 7.

```bash
./install.sh             # current user only, no root needed
./install.sh --system    # system-wide into the MLT directories (sudo)
./install.sh --uninstall # remove everything again
```

The script downloads ncnn, builds everything in `build/` and installs:

| What                         | Where (user installation)                         |
|------------------------------|---------------------------------------------------|
| MLT module                   | `~/.local/lib/morphcut/`                          |
| AI models, metadata          | `~/.local/share/morphcut/`                        |
| Kdenlive UI description      | `~/.local/share/kdenlive/transitions/morphcut.xml`|
| Kdenlive menu entry          | `~/.local/share/applications/org.kde.kdenlive.desktop` |
| Session environment          | `~/.config/plasma-workspace/env/morphcut.sh`      |

Why the detour via the menu entry? MLT loads modules from *one* directory
only. Without root, the installer therefore creates an overlay directory
that links all system modules plus Morph Cut, and Kdenlive is started with
`MLT_REPOSITORY` pointing to it. The overlay is refreshed on every start, so
MLT updates do no harm. With `--system` none of this is needed.

**Restart Kdenlive afterwards** (from the application menu, or normally after
the next login).

## Usage in Kdenlive

The composition is called **"Morph Cut"** (composition list, category
Video). Three layouts work:

1. **Both clips on the same track (simplest).**
   Split the clip before and after the slip of the tongue (`X` or
   `Shift+R`), delete the middle piece and close the gap with right-click →
   "Remove Space". Drag "Morph Cut" onto the *empty track above*, over the
   cut point, roughly half before and half after it:
   ```
   V2:              [Morph]
   V1:  [ We started, ][ the project in 2024 ]
   ```
2. **Adjoining clips on two tracks:** A on V1, B directly after it on V2,
   Morph Cut on V2 over the cut point.
3. **Overlapping clips on two tracks** (classic): Morph Cut exactly on the
   overlap.

The audio stays a hard cut. That is intended: the ear barely notices it, the
eye notices the jump in the picture.

**Recommended length: 6–12 frames.** The shorter, the more natural. Long
morphs or large motion (a swinging arm, a person turning away) lead to
ghosting. The same applies to Premiere; the technique is meant for small
head and body movements.

### Parameters

| Parameter | Meaning |
|---|---|
| Method | *AI morph* (RIFE) or *Freeze-frame dissolve* (no AI) |
| AI model | RIFE 4.22 (recommended, best in measurements), 4.26, 4.6 (fast) |
| Easing | Linear / Smooth / Smoother (motion accelerates and decelerates) |
| Direction | Automatic detects the outgoing clip by itself |
| Best quality (TTA) | computes every frame several times mirrored, about 4–8× slower |
| UHD mode | for 4K footage or large motion |
| Device | GPU 1, GPU 2 or CPU |
| Apply clip effects | render the key frames with the clips' effects |

### Translations

The UI is available in English and German (`po/de.po`). Kdenlive translates
compositions only through its own `kdenlive` catalog, so the user
installation puts a copy of Kdenlive's catalog with the Morph Cut strings
added into `~/.local/share/locale/<lang>/LC_MESSAGES/kdenlive.mo`. It is
rebuilt automatically on every start when Kdenlive or the translation
changes (needs the gettext tools `msgfmt`, `msgunfmt`, `msgcat`), and removed
by `--uninstall`. With `--system` the UI stays English.

New language: run `po/extract.py`, then `msginit -i po/morphcut.pot -o po/<lang>.po`
and translate. Keep the commas in list entries, they separate the choices.

## Measurements

Tested with *Tears of Steel* (CC-BY, Blender Foundation), 1280×534, AMD
Radeon AI PRO R9700: about **10–20 ms per generated frame**.
Quality (PSNR of the generated middle frame against the real one, in dB,
higher is better):

| Gap | Crossfade | RIFE 4.6 | RIFE 4.22 | RIFE 4.26 |
|---|---|---|---|---|
| 4 frames  | 30.7 | 35.2 | **35.9** | 35.5 |
| 12 frames | 24.7 | 28.8 | **29.0** | 28.9 |
| 24 frames | 22.4 | 24.2 | 24.1 | **24.2** |

## Limitations

* Clips with changed speed or time remapping: key frames are computed at
  speed 1.
* Title and generator clips without a file are not morphed (they are shown
  as usual).
* The composition must contain a clip boundary. If there is no clip change,
  it shows the track unchanged and writes a warning to the log.

## Source layout

```
src/transition_morphcut.cpp   MLT transition: find clips, key frames, cache
src/interpolator.cpp          RIFE engine (ncnn/Vulkan), shared
src/factory.cpp               MLT module registration
kdenlive/morphcut.xml         Kdenlive UI description
po/                           UI translations (extract.py, morphcut.pot, de.po)
data/transition_morphcut.yml  MLT metadata
third_party/rife/             rife-ncnn-vulkan (MIT), modified, see UPSTREAM
third_party/mlt/              MLT 7.38 headers (LGPL), used if libmlt-dev is missing
third_party/stb/              stb_image (public domain / MIT), test tool only
models/                       RIFE models (MIT)
tools/morphcut_interp.cpp     test tool: two PNGs → intermediate frames
```

Build the test tool: `cmake -B build -DMORPHCUT_BUILD_TOOLS=ON && cmake --build build`,
then `build/morphcut-interp models/rife-v4.22 a.png b.png 9 out/f`.

## License

Copyright (C) 2026 Thorsten Schnebeck <thorsten.schnebeck@gmx.net>

The code of this project (`src/`, `tools/`, `kdenlive/`, `data/`, `po/`,
`install.sh`, `CMakeLists.txt`) is free software under the
**GNU General Public License, version 3 or later**, see [COPYING](COPYING).
There is no warranty, to the extent permitted by law.

Third-party components keep their own licenses:

| Component | License | Location |
|---|---|---|
| rife-ncnn-vulkan (nihui), with extensions by TNTwise, modified | MIT | `third_party/rife/` |
| RIFE models (hzwer, ncnn conversions by nihui and TNTwise) | MIT | `models/` |
| MLT framework headers (Meltytech) | LGPL-2.1-or-later | `third_party/mlt/` |
| stb_image, stb_image_write (Sean Barrett) | MIT or Unlicense | `third_party/stb/` |
| ncnn (Tencent), downloaded at build time | BSD-3-Clause | linked statically |

All of these licenses are compatible with GPL-3.0; the combined module is
distributed under GPL-3.0-or-later. Every file carries its license as an SPDX
tag or in [REUSE.toml](REUSE.toml), the license texts are in
[LICENSES/](LICENSES/); the project is [REUSE](https://reuse.software) compliant.

## Credits

Idea, decisions and testing: Thorsten Schnebeck. Written by Anthropic
Claude Opus 5.5 (AI generated content).
