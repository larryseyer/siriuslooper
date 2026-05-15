# Third-Party Licenses

This document lists the third-party dependencies used by Sirius Looper and their
respective licenses. Sirius Looper follows the same licensing model as its
sister application, OTTO; this file is maintained alongside the project's
evolving dependency set.

**Last Updated:** May 2026

---

## Commercial License Required

The following dependencies are dual-licensed under GPL (or AGPL) and commercial
licenses. The required commercial licenses have been obtained for the
distribution of Sirius Looper.

### JUCE Framework

- **License:** AGPLv3 or Commercial (JUCE 8 End User Licence Agreement)
- **Website:** https://juce.com
- **Commercial License:** https://juce.com/legal/juce-8-licence/

The JUCE Framework is dual-licensed. A commercial license from JUCE is held for
the distribution of Sirius Looper.

### Ableton Link

- **License:** GPLv2+ or Proprietary
- **Repository:** https://github.com/Ableton/link
- **Website:** https://ableton.github.io/link/

Ableton Link is used as one of the Logical Master Clock's discipline sources for
tempo/phase synchronization across an ensemble. A proprietary license from
Ableton is held for the distribution of Sirius Looper.

---

## Permissive Licenses (Attribution Required)

The following dependencies use permissive licenses that allow commercial use
with proper attribution.

### sfizz

- **License:** BSD 2-Clause
- **Repository:** https://github.com/sfztools/sfizz

sfizz is the SFZ sample-playback engine. It powers the embedded sampler derived
from OTTO that plays the bundled Larry Seyer Acoustic Drum Library selection.

```
BSD 2-Clause License

Copyright (c) 2021-2023, sfizz contributors (detailed in AUTHORS.md)
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### CLAP (CLever Audio Plug-in API)

- **License:** MIT
- **Repository:** https://github.com/free-audio/clap

The CLAP SDK is used by Sirius Looper's plugin-hosting layer to host CLAP
plugins inside Constituent effect chains.

```
MIT License

Copyright (c) 2021 Alexandre BIQUE

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## LGPL Licenses (Dynamically Linked)

The following dependencies are licensed under the GNU Lesser General Public
License. They are **dynamically linked** so that Sirius Looper's distribution
remains compliant; GPL-only optional components (e.g. x264) are excluded from
the build.

### libsoxr (SoX Resampler library)

- **License:** LGPL-2.1-or-later
- **Repository:** https://github.com/chirlu/soxr

libsoxr provides continuous async sample-rate conversion at the audio membranes.

### FFmpeg / libav

- **License:** LGPL-2.1-or-later (built without GPL-only components)
- **Website:** https://ffmpeg.org

FFmpeg/libav provides the video decode/encode pipeline. Sirius Looper links only
the LGPL-licensed libraries (libavcodec, libavformat, libavutil, libswscale) and
excludes any GPL-only components from the build.

Full LGPL-2.1 license text: https://www.gnu.org/licenses/lgpl-2.1.txt

---

## Development Dependencies (Not Distributed)

### Catch2

- **License:** Boost Software License 1.0
- **Repository:** https://github.com/catchorg/Catch2

Catch2 is used only for the test suite and is not included in distributed
builds.

```
Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

---

## JUCE Embedded Dependencies

JUCE includes several third-party libraries. The following are embedded within
JUCE and their licenses are included in the JUCE distribution:

| Library      | License                          |
|--------------|----------------------------------|
| AudioUnitSDK | Apache 2.0                       |
| Oboe         | Apache 2.0                       |
| FLAC         | BSD                              |
| Ogg Vorbis   | BSD                              |
| jpeglib      | Independent JPEG Group License   |
| pnglib       | zlib                             |
| zlib         | zlib                             |
| VST3 SDK     | MIT                              |
| HarfBuzz     | Old MIT                          |
| SheenBidi    | Apache 2.0                       |
| Box2D        | zlib                             |
| CHOC         | ISC                              |
| LV2          | ISC                              |

FLAC, in particular, is used as a tape storage format (see the white paper,
Part VI). For complete license texts, see the respective files in the JUCE
distribution.

---

## Bundled Content (Not Open Source)

### Larry Seyer Acoustic Drum Library

- **License:** Proprietary — Copyright (C) 1999 Larry Seyer. All Rights Reserved.
- **Terms:** See `SAMPLE-LICENSE.md`

The bundled drum sample selection is proprietary content, NOT covered by the
AGPLv3, and NOT included in the source repository. It ships only with official
Sirius Looper binary distributions.

---

## License Summary

| Dependency      | License                  | Commercial Use                |
|-----------------|--------------------------|-------------------------------|
| JUCE            | AGPLv3 / Commercial      | Commercial license held       |
| Ableton Link    | GPLv2+ / Proprietary     | Proprietary license held      |
| sfizz           | BSD 2-Clause             | Allowed with attribution      |
| CLAP            | MIT                      | Allowed with attribution      |
| libsoxr         | LGPL-2.1+                | Allowed (dynamically linked)  |
| FFmpeg / libav  | LGPL-2.1+                | Allowed (dynamically linked)  |
| Catch2          | Boost 1.0                | Dev only, not distributed     |
| LSAD Library    | Proprietary              | See SAMPLE-LICENSE.md         |

---

*This file should be included in all Sirius Looper distributions.*
