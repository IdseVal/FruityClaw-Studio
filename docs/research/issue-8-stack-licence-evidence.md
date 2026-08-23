# Issue #8 — Evidence log: technology stack and audio engine

Primary-source verification behind [`ADR-008`](../adrs/ADR-008-technology-stack-and-audio-engine.md).
Checked 2026-08-23. Nothing in this file is a decision; every row is something that was looked up
rather than remembered.

The rule applied throughout: **a licence claim is only usable if it came from the project's own
licence file, its own licensing page, or a first-party announcement.** Second-hand summaries are
recorded only where they agree with a primary source.

---

## 1. The finding that decided the ADR — JUCE is AGPLv3

`LICENSE.md` on `juce-framework/JUCE@master`:

> "The JUCE Framework modules are dual-licensed under the AGPLv3 and the commercial JUCE licence."
> — [LICENSE.md](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md)

The same file names the commercial option as the **JUCE 9** licence, so this is the current state as
of August 2026, not a snapshot of the JUCE 8 transition.

The predecessor's terms are the relevant contrast:

- **JUCE 7** offered GPLv3 as its open-source option
  ([JUCE 7 EULA](https://juce.com/legal/juce-7-license/)).
- **JUCE 8 onward** replaced that with AGPLv3
  ([JUCE 8 EULA](https://juce.com/legal/juce-8-licence/), [Get JUCE](https://juce.com/get-juce/)).

So the framework the audio industry defaults to moved *away* from the licence this project has
fixed in core document section 3.2, one major version before the project started.

### Why AGPLv3 is not a shrug

GPLv3 section 13 permits the combination — this is not a licence conflict. What it is not is a way
to keep calling the result GPLv3:

> "you have permission to link or combine any covered work with a work licensed under version 3 of
> the GNU Affero General Public License into a single combined work … but the special requirements
> of the GNU Affero General Public License, section 13, concerning interaction through a network
> will apply to the combination as such."
> — GNU GPL v3, section 13

Two consequences, both recorded in ADR-008:

1. The **stated licence** would have to become AGPLv3. Core document 3.2 says GPLv3, and 3.2 is an
   owner decision. A spec may not contradict the core document silently.
2. Core document 8.3 adopts a CLA specifically to keep a future proprietary relicense possible.
   That option survives a GPLv3 dependency set only if every copyleft dependency can be replaced;
   it does not survive an AGPLv3 dependency at the centre of the application.

This closes **ADR-001 open item 2**, which flagged exactly this question for the architecture track.

---

## 2. Steinberg's October 2025 relicensing — both halves confirmed

### VST 3 SDK to MIT

- Steinberg press release, VST 3.8, 2025-10-15
  ([PDF](https://ocl-steinberg-live.steinberg.net/_storage/asset/819253/storage/master/Press%20Release%20-%202025-10-29%20-%20VST%203.8%20-%20EN.pdf))
- [VST 3 Developer Portal — licensing](https://steinbergmedia.github.io/vst3_dev_portal/pages/VST+3+Licensing/VST3+License.html):
  since 3.8, VST 3 is under the MIT licence.
- Version 3.8.0 released 2025-10-20
  ([Steinberg forum announcement](https://forums.steinberg.net/t/vst-3-8-0-sdk-released/1011988)).
- No agreement to sign, no membership, no fee
  ([Sound On Sound](https://www.soundonsound.com/news/steinberg-adopt-mit-license-vst3),
  [CDM](https://cdm.link/open-steinberg-vst3-and-asio/)).

This corrects the caveat in core document Appendix B.1, which warned that "building with JUCE plus
the VST3 SDK can pull GPL3 dependencies into the result." For SDK 3.8 and later the VST3 half of
that warning is obsolete; ADR-001 had already recorded the correction.

### ASIO SDK to GPLv3-or-later, dual with proprietary

- [KVR](https://www.kvraudio.com/news/steinberg-moves-vst-3-sdk-to-mit-open-source-license-asio-now-gplv3-65179),
  [Libre Arts](https://librearts.org/2025/11/steinberg-relicenses-vst3-and-asio/),
  [heise](https://www.heise.de/en/news/Steinberg-Releases-ASIO-and-VST-Audio-Interfaces-Under-Open-Source-Licenses-10963451.html):
  ASIO moved from proprietary-only to "GPLv3+/proprietary".
- The SDK distribution carries three documents — `LICENSE.txt` (the dual offer), a proprietary
  *Steinberg ASIO Licensing Agreement*, and *Steinberg ASIO Usage Guidelines*
  ([SDK mirror](https://github.com/audiosdk/asio)).

Two operational facts that matter for this project:

| Question | Answer found |
|---|---|
| Does the GPLv3 option require signing anything? | No. The signed agreement belongs to the proprietary option. |
| Are there trademark obligations? | Only if the ASIO **name or logo** is displayed. Logo use is optional under GPLv3; if used, the unaltered-logo usage guidelines bind. Putting "ASIO" in a product or company name is prohibited under either option. |

This is a clean fit with core document 8.1 (the product name must stay cheap to change): the
project can *use* ASIO without taking on any branding obligation, simply by not displaying the mark.

Core document 3.2 already anticipated this: "This closes the ASIO interaction raised in round 2."
The evidence confirms the premise the owner decided on was correct.

---

## 3. Audio backend candidates

| Library | Licence (verified) | Windows APIs | macOS | Linux | Verdict |
|---|---|---|---|---|---|
| **PortAudio** | MIT ([repo](https://github.com/PortAudio/portaudio)) | ASIO, WASAPI (shared **and exclusive**), WDM-KS, DirectSound, MME | CoreAudio | ALSA, JACK, OSS | **Selected** |
| RtAudio | MIT **plus a non-binding request** (below) | DirectSound, ASIO, WASAPI ([README](https://github.com/thestk/rtaudio)) | CoreAudio, JACK | ALSA, JACK, PulseAudio, OSS | Pre-qualified alternate |
| miniaudio | Public domain / MIT-0 | WASAPI, DirectSound, WinMM — **no ASIO backend** | CoreAudio | ALSA, PulseAudio, JACK | Rejected on capability |
| libsoundio | MIT ([libsound.io](http://libsound.io/)) | WASAPI — no ASIO | CoreAudio | ALSA, PulseAudio, JACK | Rejected on capability and activity |

### The Windows sub-10ms question

This is what separated PortAudio from RtAudio, and it is a capability difference rather than a
preference:

> "Only exclusive backends (WASAPI Exclusive, WDM-KS) can achieve an actual latency below 10 ms."
> — [FlexASIO BACKENDS.md](https://github.com/dechamps/FlexASIO/blob/master/BACKENDS.md), a
> PortAudio-based project documenting its own backends

PortAudio documents both of those paths, plus ASIO behind the `PA_USE_ASIO` build flag
([PortAudio Windows build docs](https://files.portaudio.com/docs/v19-doxydocs/compile_windows.html)).
RtAudio's own README lists its Windows APIs as "DirectSound, ASIO, and WASAPI" and does not document
a WASAPI exclusive-mode or WDM-KS path.

**Stated precisely, because the difference is narrower than it looks:** RtAudio's README does not
document exclusive-mode support; that is not the same as proving it absent. The decision does not
rest on RtAudio being incapable — it rests on PortAudio having the documented path today, and on the
`AudioDevice` seam making the choice cheap to revisit if that turns out to be wrong.

### RtAudio's extra licence clause

Worth recording because it is easy to mistake for a condition:

> "Any person wishing to distribute modifications to the Software is asked to send the modifications
> to the original developer so that they can be incorporated into the canonical version. This is,
> however, not a binding provision of this license."
> — [RtAudio LICENSE](https://github.com/thestk/rtaudio/blob/master/LICENSE)

It is a request, self-described as non-binding. RtAudio is GPLv3-compatible.

### PortAudio's maintenance risk — recorded, not hidden

The last tagged release is **v19.7.0, April 2021**. The v19.8 milestone is still open as of
August 2026, though daily snapshots continue (most recent seen: 2026-08-17)
([releases](https://github.com/PortAudio/portaudio/releases),
[v19.8 milestone](https://github.com/PortAudio/portaudio/milestone/10)).

Mitigations are in ADR-008. The short version: pin a commit rather than a tag — which is what
Audacity and Mixxx do — and keep the backend behind a seam that has a second qualified adapter
ready.

### Precedent

Mixxx: Qt user interface, PortAudio and JACK real-time engine, GPLv2, ASIO and WASAPI-exclusive
recommended to users for live work at 64–128 sample buffers
([architecture overview](https://deepwiki.com/mixxxdj/mixxx),
[Wikipedia](https://en.wikipedia.org/wiki/Mixxx)). The stack selected here is the same shape, one
licence version newer.

---

## 4. Qt

- Essential modules are offered under **LGPLv3**, and everything available under LGPL is also
  available under GPL: "All parts that are licensed under LGPL are also available under GPL." Some
  parts are GPL-only for open-source users
  ([Qt open-source obligations](https://www.qt.io/licensing/open-source-lgpl-obligations)).
- Either way the result is GPLv3-compatible, and for a GPLv3 application LGPLv3 adds nothing:
  the relinking freedom LGPL exists to protect is already guaranteed by shipping GPLv3 source.

### The LTS trap, avoided

> "While the initial patch releases of such an LTS version are also available to open-source users,
> immediate access to LTS releases is limited to commercial customers of The Qt Company, under the
> commercial license."
> — [Qt LTS](https://www.qt.io/development/qt-framework/qt-lts)

Confirmed by the pattern of release announcements: Qt 6.8.6, 6.8.7 and 6.8.8 were all published as
**Commercial LTS** releases through 2026
([6.8.8](https://www.qt.io/blog/commercial-lts-qt-6.8.8-released)), while the open feature stream
reached **Qt 6.11** ([Qt Releases](https://doc.qt.io/qt-6/qt-releases.html)).

Consequence: an open-source project that pins "Qt 6.8 LTS" for stability gets the *opposite* — a
branch whose fixes it cannot receive. ADR-008 therefore tracks the feature stream.

---

## 5. Supporting libraries

| Library | Licence (verified) | GPLv3-compatible | Source |
|---|---|---|---|
| libsndfile | LGPL-2.1-or-later | Yes — LGPLv2.1 upgrades to LGPLv3, which is GPLv3-compatible | [libsndfile.github.io](https://libsndfile.github.io/libsndfile/) |
| libsamplerate | BSD-2-Clause | Yes — permissive | [repo](https://github.com/libsndfile/libsamplerate) |
| CLAP | MIT | Yes — permissive | [free-audio/clap](https://github.com/free-audio/clap) |
| clap-helpers | MIT | Yes — permissive | [free-audio/clap-helpers](https://github.com/free-audio/clap-helpers) |
| VST 3 SDK 3.8+ | MIT | Yes — permissive | section 2 above |
| ASIO SDK | GPL-3.0-or-later (dual) | Yes — same licence family | section 2 above |
| Qt 6 essentials | LGPLv3 (also available GPLv3) | Yes | section 4 above |
| PortAudio | MIT | Yes — permissive | section 3 above |

CLAP's own position, for the post-MVP Plugin work: "no fees, memberships or proprietary license
agreements required before developing or distributing a CLAP capable host or plug-in"
([u-he](https://u-he.com/community/clap/),
[Wikipedia](https://en.wikipedia.org/wiki/CLever_Audio_Plug-in)).
Host support as of 2026 includes Bitwig, REAPER 7, FL Studio and Studio One — relevant because
core document 1.5 makes FL Studio the behavioural reference.

---

## 6. Tracktion Engine — checked and set aside

- Dual **GPLv3-or-later / commercial**
  ([LICENSE.md](https://github.com/Tracktion/tracktion_engine/blob/develop/LICENSE.md)).
- The licence is not the problem. Tracktion Engine is a **JUCE module** — it is built on JUCE and
  distributed as one. Taking it under GPLv3 still means linking JUCE, and JUCE's open-source option
  is AGPLv3 (section 1). The AGPL problem returns transitively.

Recorded so that a future reader does not re-suggest it on the strength of its own licence line.

---

## 7. macOS distribution — a cost, not a technical question

Notarization has been a hard requirement for software distributed outside the Mac App Store since
macOS 10.15. It requires an active **Apple Developer Program** membership and a *Developer ID*
certificate; unsigned or un-notarized apps are blocked by Gatekeeper regardless of distribution
format, DMG included, and regardless of whether the project is open source
([Apple — Gatekeeper](https://support.apple.com/guide/security/gatekeeper-and-runtime-protection-sec5599b66df/web),
[Apple — safely open apps](https://support.apple.com/en-us/102445)).

Core document 3.3 makes GitHub the distribution channel. Nothing about GitHub-first distribution
removes this requirement, so it is raised in ADR-008 as an owner decision with a recurring cost,
not resolved here.

---

## Sources

- [JUCE LICENSE.md](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md) · [JUCE 8 EULA](https://juce.com/legal/juce-8-licence/) · [JUCE 7 EULA](https://juce.com/legal/juce-7-license/) · [Get JUCE](https://juce.com/get-juce/)
- [Steinberg VST 3.8 press release](https://ocl-steinberg-live.steinberg.net/_storage/asset/819253/storage/master/Press%20Release%20-%202025-10-29%20-%20VST%203.8%20-%20EN.pdf) · [VST 3 licensing](https://steinbergmedia.github.io/vst3_dev_portal/pages/VST+3+Licensing/VST3+License.html) · [VST 3.8.0 release thread](https://forums.steinberg.net/t/vst-3-8-0-sdk-released/1011988) · [Sound On Sound](https://www.soundonsound.com/news/steinberg-adopt-mit-license-vst3)
- [KVR — ASIO now GPLv3](https://www.kvraudio.com/news/steinberg-moves-vst-3-sdk-to-mit-open-source-license-asio-now-gplv3-65179) · [Libre Arts](https://librearts.org/2025/11/steinberg-relicenses-vst3-and-asio/) · [heise](https://www.heise.de/en/news/Steinberg-Releases-ASIO-and-VST-Audio-Interfaces-Under-Open-Source-Licenses-10963451.html) · [CDM](https://cdm.link/open-steinberg-vst3-and-asio/) · [ASIO SDK mirror](https://github.com/audiosdk/asio)
- [PortAudio repo](https://github.com/PortAudio/portaudio) · [releases](https://github.com/PortAudio/portaudio/releases) · [v19.8 milestone](https://github.com/PortAudio/portaudio/milestone/10) · [Windows build docs](https://files.portaudio.com/docs/v19-doxydocs/compile_windows.html) · [latency docs](https://portaudio.com/docs/latency.html)
- [RtAudio](https://github.com/thestk/rtaudio) · [RtAudio LICENSE](https://github.com/thestk/rtaudio/blob/master/LICENSE) · [libsoundio](http://libsound.io/)
- [FlexASIO BACKENDS.md](https://github.com/dechamps/FlexASIO/blob/master/BACKENDS.md) · [FlexASIO FAQ](https://github.com/dechamps/FlexASIO/blob/master/FAQ.md)
- [Qt open-source obligations](https://www.qt.io/licensing/open-source-lgpl-obligations) · [Qt LTS](https://www.qt.io/development/qt-framework/qt-lts) · [Qt Releases](https://doc.qt.io/qt-6/qt-releases.html) · [Commercial LTS Qt 6.8.8](https://www.qt.io/blog/commercial-lts-qt-6.8.8-released)
- [libsndfile](https://libsndfile.github.io/libsndfile/) · [libsamplerate](https://github.com/libsndfile/libsamplerate)
- [CLAP](https://github.com/free-audio/clap) · [clap-helpers](https://github.com/free-audio/clap-helpers) · [u-he CLAP](https://u-he.com/community/clap/) · [CLAP on Wikipedia](https://en.wikipedia.org/wiki/CLever_Audio_Plug-in)
- [Tracktion Engine LICENSE.md](https://github.com/Tracktion/tracktion_engine/blob/develop/LICENSE.md) · [Tracktion Engine](https://engine.tracktion.com/)
- [Mixxx architecture](https://deepwiki.com/mixxxdj/mixxx) · [Mixxx](https://en.wikipedia.org/wiki/Mixxx)
- [Apple — Gatekeeper](https://support.apple.com/guide/security/gatekeeper-and-runtime-protection-sec5599b66df/web) · [Apple — safely open apps](https://support.apple.com/en-us/102445)
