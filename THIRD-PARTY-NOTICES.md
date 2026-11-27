# Third-party notices

Fugue's own code is released under the Unlicense (see `LICENSE`). That licence does not cover the third-party
material listed here, which keeps its own terms. This file lists everything in the source tree that comes from, or is
derived from, someone else's work, and what is linked at build time. If you find something missing, please open an
issue.

## Included in the source tree

### stb_vorbis

* Files: `audio/third_party/stb_vorbis.c` (unmodified), `audio/stb_vorbis_impl.cpp` (the one translation unit that
  compiles it).
* What: Ogg Vorbis decoder, v1.22 (Sean Barrett) (http://nothings.org/stb_vorbis/).
* Author: Sean Barrett. Licence: dual-licensed, your choice of MIT or public domain (Unlicense). The full text of both is at the end of
  `stb_vorbis.c`. Copyright (c) 2017 Sean Barrett.

### Go standard-library code, ported

* Owner: The Go Authors. Licence: BSD-3-Clause (text below).
* Files: `model/gomath.h`; the entity-unescaping routine in `model/htmltext.cpp` (structure ported
  from the `html` package's `unescapeEntity`).
* What: C++ ports of the `math` functions the model uses. Bit-exact results keep model calculations deterministic
  across platforms, whatever the platform's libm.
* Licence: BSD 3-Clause, Copyright 2009 The Go Authors. Some of the algorithms Go itself took from the Cephes
  library (S. L. Moshier) and FreeBSD's msun; their notices are in Go's source under `src/math`.

```
Copyright 2009 The Go Authors.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:

   * Redistributions of source code must retain the above copyright notice, this list of conditions and the
     following disclaimer.
   * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
     following disclaimer in the documentation and/or other materials provided with the distribution.
   * Neither the name of Google LLC nor the names of its contributors may be used to endorse or promote products
     derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### FreeSerif glyph outlines (the application icon)

* Owner: the GNU FreeFont authors. Licence: GPLv3-or-later with the font exception.
* Files: `packaging/flatpak/io.github.toomykins.Fugue.svg`, `packaging/flatpak/icons/*.png`; sources in
  `packaging/flatpak/icons-src/`.
* What: the icon is the letter "F" from FreeSerif Bold (GNU FreeFont), whose outlines are embedded in the SVG as
  paths, with a small "u" set into it. The PNGs are renderings of the same glyph.
* Licence: GNU FreeFont is GPLv3 or later with the font exception: the exception allows documents and other works
  that embed the font to be distributed under terms of your choice. See https://www.gnu.org/software/freefont/.
  The icon is a work that embeds the font in that sense.

### BYOND's default skin

* Owner: BYOND (Dantom Co.). Licence: BYOND's own terms; **not** covered by the Unlicense.
* File: `app/defaultskin.h`.
* What: the skin a world without its own `.dmf` runs under is BYOND's built-in default, `byond.dmf` from BYOND
  516.1684, included verbatim (line endings normalised). It is BYOND's file and is reproduced here only so that
  skinless worlds look as they do in DreamSeeker. Copyright belongs to its owners.

## Linked at build time (not included in the tree)

* Qt 6 (Core, Gui, Widgets, Network, WebEngine, WebChannel, Test), The Qt Company and contributors: LGPLv3 (also
  available under GPL and commercial terms); Qt WebEngine embeds Chromium and its own third-party components, whose
  notices ship with Qt. Fugue links Qt dynamically.
* zlib (Jean-loup Gailly and Mark Adler): zlib licence. Linked dynamically.
* libjpeg / libjpeg-turbo: JPEG icon decoding. libjpeg-turbo is covered by the IJG,
  modified BSD and zlib licences (see its distributed `LICENSE.md`). Linked dynamically
  in the Windows vcpkg build; platform builds use the CMake-discovered JPEG library.
* libopenmpt (optional, `FUGUE_OPENMPT`), OpenMPT developers: BSD-3-Clause, Copyright (c) 2004-2024 OpenMPT Project
  Developers and Contributors, and 1997-2003 Olivier Lapicque. Linked when found; built from the pinned release tarball and bundled
  in the Flatpak (`packaging/flatpak/io.github.toomykins.Fugue.yml`).

## Other files worth knowing about

* `pager/dsargv.h` reproduces the observable behaviour of DreamSeeker's command-line parsing; it contains no BYOND
  code.
* `net/md5.cpp` is a straightforward implementation of RFC 1321 written for this project, not copied code.
* Test data under `tests/data/*-world` was recorded from small worlds written for this project; the BYOND tools
  that compiled and ran them are not included.
