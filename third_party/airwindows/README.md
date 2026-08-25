# Airwindows — vendored sources

Five of the six stock Effects of ADR-001, taken from the Airwindows LinuxVST
sources and kept **verbatim** so an upstream update is a file copy.

| Effect | Directory | Upstream path |
|---|---|---|
| EQ | `Parametric/` | `plugins/LinuxVST/src/Parametric` |
| Compressor | `Pressure6/` | `plugins/LinuxVST/src/Pressure6` |
| Reverb | `Verbity2/` | `plugins/LinuxVST/src/Verbity2` |
| Delay | `TapeDelay2/` | `plugins/LinuxVST/src/TapeDelay2` |
| Distortion | `Distortion/` | `plugins/LinuxVST/src/Distortion` |

- **Upstream:** https://github.com/airwindows/airwindows
- **Commit:** `a2a6f9abdcda6ac1f39d504fe9313f507fba9261` (master, 2026-08-23)
- **Licence:** MIT — `LICENSE` in this directory is the repository's licence
  file, unchanged. It ships in every artefact through `THIRD_PARTY_NOTICES`.

`shim/audioeffectx.h` is **not** upstream code. It is the project's stand-in
for the VST2 SDK base class these files derive from, so that they compile
unmodified without the SDK. See the header for what it covers.

Only `src/effects/` may include anything under this directory
(architecture-seams rule 3).
