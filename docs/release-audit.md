# Release audit

What this tree actually does, checked against the code on 2026-11-08 for the
`0.1.0` alpha. Every answer names the place it came from, because a claim without
one is the thing this page exists to stop.

## 1. Crash reporting: none, and nothing links it

The engine can build against Sentry (`upstream/CMakeLists.txt:54`), and our
configured build has it off:

```
$ grep SLIC3R_SENTRY upstream/build/CMakeCache.txt
SLIC3R_SENTRY_DSN:STRING=
```

`upstream/CMakeLists.txt:67` takes that branch and prints "Disabling sentry". No
Sentry object ends up in the binary (`ldd …/next-layer-qt | grep -ci sentry` → 0),
and no crash handler of ours replaces anything: there is no `std::set_terminate`,
no `SIGSEGV` handler, and no `qInstallMessageHandler` in `src/qt/src`.

So a crash produces no network traffic and no report. It produces stderr.

## 2. Everything that can open a socket

Three, one of them dormant:

| Where | What it talks to | When |
|---|---|---|
| `src/qt/src/PrinterLink.cpp` | the host in your printer settings — Moonraker, OctoPrint, PrusaLink or Duet/RepRapFirmware, over `http://host:port` built from your own fields | only when the Device tab or a send action uses it |
| `src/qt/src/Updater.cpp` | `https://github.com/CWebb89/NextLayer/releases/latest/download/update.json` | only when you press *Check for updates* in Preferences (`PreferencesDialog.qml:338`) — there is no timer and no startup check |
| `src/qt/src/SliceRuntime.cpp:107` | the engine's preset updater, which would fetch Prusa's vendor config bundle | never called: the controller is constructed, no `.update()` call exists anywhere in our tree, and it is additionally gated on `enable_preset_update` |

No telemetry, no analytics, no account, no cloud, no crash upload. Spoolman is not
implemented, so there is no filament-database connection to disclose.

`docs/network.md` is the page that states this for users.

## 3. Icons

**Were** a system dependency: `qml/components/Icon.qml` drew Material Symbols by
ligature and the font was resolved from fontconfig, so a machine without it got a
missing-glyph box for every icon in the interface — the AppImage included, since
it bundles Qt and not fonts.

Now bundled: `src/qt/assets/fonts/MaterialSymbolsOutlined.ttf` (Apache-2.0),
registered at startup, `THIRD-PARTY.md` names it. The gate `icons` runs the
QML's own lookup with `FONTCONFIG_FILE` pointed at a directory that does not
exist — 0 system fonts available — and requires the family to resolve and a
ligature name to measure as one glyph rather than eight.

## 4. CI

`.github/workflows/check.yml` runs `tools/check --static` on push, pull request and
manual dispatch: `fish -n` over every script in `tools/`, and qmllint against its
baseline. Nothing else.

It cannot run the rest. The binary gates need a build of the pinned engine, whose
dependency tree is hours and tens of gigabytes — no hosted runner does that, and
a workflow pretending otherwise would be a red build nobody reads. `--static`
prints a skip line for the gates it does not run, so a green check is visibly a
partial one.

## 5. What is tested

- `tools/check --fast`: 34 gates, ~4 minutes warm on this machine.
- `tools/check`: the same plus the oracle, 12 cases over 4 models
  (`oracle/models/`: cube 20 mm, bridge 30 mm, cylinder r10 h20, sphere r12),
  byte-identical after header normalisation, on both the spike and the app binary.
- `--ui`: adds a real window, driven with real input, captured.

## 6. Verified by actual prints

One. `docs/real-prints.md` records a 20 mm cube on a Voron 2.4 R2 350
(Klipper + Moonraker), sliced in this app, uploaded from it, finished, with the
Moonraker job id, the file hash and the filament figures. It also records that the
print used the *default* 220 mm profile and Generic PLA at 230 °C, i.e. the
pipeline was proven and the presets were wrong. Every other printer, firmware and
multi-tool path in this tree is unverified on hardware.

## 7. The feature ledger

`docs/features.md`, 256 table rows: **177 marked done, 6 staged, 1 needs-engine.**
The counts are a tally of the status column, not a hand-maintained figure; the rows
themselves carry the evidence. The one engine-blocked row is the plater's bed shape
editor; the staged six are listed in `docs/known-issues.md`.

## 8. Project files

`.nextproj` carries an integer `version` (`ProjectFile.cpp:190` writes
`kFormatVersion`).

- **Newer than this build**: refused, with a message naming the file's version and
  this build's (`ProjectFile.cpp:290`).
- **Current**: loaded.
- **Older, or missing the key**: falls back to `0` and loads with defaults, which is
  correct today because `kFormatVersion` is 1 and nothing older exists. There are no
  migration functions, and writing a migration ladder for zero historical versions
  would be untested code pretending to be foresight. `tests` in
  `tools/check` cover old, current and newer explicitly, so the day version 2
  exists the policy is already pinned by a failing gate rather than by memory.

## 9. Logs and crash output

stderr, and only stderr. There is no log file, no crash dump written, and nothing
that leaves the machine: no `QStandardPaths::AppDataLocation` write path for logs,
no `freopen`, no handler. A headless knob's diagnostics go to stdout. Qt's own
warnings go wherever Qt's message handler sends them, which is stderr.

Consequence worth stating plainly: **when the app crashes on someone's machine,
we get nothing.** They have to run it from a terminal and paste the output. That
is a support cost accepted deliberately — the alternative sends data off the
machine without asking — but it belongs in `docs/known-issues.md`, not only here.

## 10. README claims, and what stands behind each

| Claim in `README.md` | Evidence status |
|---|---|
| "Supports, seams, variable layer height, adaptive infill, the works" | engine capabilities, exposed and marked done in the ledger; variable layer height has a layer-count proof, "the works" is unmeasurable |
| "Built for tool changers. Per-tool filaments, per-tool colours, tool changes" | implemented and in the ledger, but **no gate slices a multi-tool machine and no print with more than one tool is recorded** — implemented, not verified |
| "It slices and prints" | one print, one machine, recorded with its own two errors in `docs/real-prints.md` |
| "No account, no telemetry, no cloud… talks to a network only when you send a print or check for an update" | verified above (§2) and now published as `docs/network.md` |
| "Qt 6.12 or newer" | verified on 6.12.0 only; no CMake minimum is declared, so "or newer" is a hope |
| "A GPU with a working OpenGL driver" | one GPU, one driver, ever |
| "The binary finds its QML through a compile-time path" | stale since packaging: QML and resources are resolved relocatably first, and the compile-time path is the fallback |
| Nothing about the AppImage or the AUR package | stale since `tools/package` and the `v0.1.0` release |
