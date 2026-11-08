# Packaging

How a release is built, what it ships, and why the choices are these ones. The
numbers are measured on this machine, 2026-11-08, `0.1.0`.

## What a release consists of

| Artifact | Size | Who it is for |
|---|---|---|
| `NextLayer-<version>-x86_64.AppImage` | 268.7 MB | anyone who wants to run it without installing anything |
| `next-layer-<version>.tar.gz` | 63.9 MB | the AUR package, and anyone unpacking it beside their own files |
| `update.json` | < 1 KB | the in-app update check |
| `PKGBUILD`, `.SRCINFO`, desktop file, icon | — | pushed to the AUR |

All three are built by `tools/package` and `tools/release`, on this machine, from
the build tree. Both artefacts were run from a clean prefix before publishing:
the AppImage sliced `bridge_30` to the same toolpath hash as the build tree, and
the tarball sliced a cube from a bare `/tmp` extraction.

## AppImage or Flatpak

Both are viable for a Qt 6.12 app. They are not close for this one.

| | AppImage | Flatpak |
|---|---|---|
| Download | 268.7 MB, self-contained | ~30 MB, plus the `org.kde.Platform` runtime at **~1.9 GB** (Qt, QtWebEngine, everything we link) |
| Sandbox | none — runs as you, with your files | bubblewrap; `--filesystem=home` still needed for the preset importer and projects |
| 3D viewport | works today | needs `--device=dri` and a GL driver inside the runtime; Qt Quick 3D through a sandboxed GL stack is the part most likely to break on someone else's machine |
| QtWebEngine | bundled | inside the runtime, one shared copy |
| Update path | the app replaces its own install root (`Updater::installRoot()` is `applicationDirPath()`) | must be `flatpak update`; the built-in updater would be writing into a read-only `/app`, so it has to be disabled and the code path reworked |
| Build cost here | linuxdeploy, minutes | a manifest, the KDE SDK, and a runtime build/test cycle on a machine that has neither installed |

**Decision: AppImage plus AUR for 0.1.0.** Flatpak stays a real option for 1.0,
when the update path can be redesigned around it and someone has a machine to test
GL inside the sandbox on. Shipping one package that is verified beats shipping two
where one is unverified.

The AUR package (`next-layer-bin`) unpacks the tarball into `/opt/next-layer` and
symlinks `/usr/bin/next-layer`. It does not compile anything, which is why it is
`-bin`: an AUR build of this engine is hours of Boost, CGAL, OpenVDB and
OpenCASCADE on the user's machine.

## The release procedure

There is no CI build, deliberately:

1. `tools/check` green on the commit being released, oracle included.
2. `tools/release <version> "<one line about this release>"` — writes
   `dist/next-layer-<version>.tar.gz`, prints its sha256 and writes
   `dist/update.json`.
3. `tools/package appimage` — writes the AppImage.
4. `git tag -a v<version> -m "next-layer <version>" && git push origin v<version>`.
5. `gh release create v<version> dist/next-layer-<version>.tar.gz dist/update.json \
   dist/NextLayer-<version>-x86_64.AppImage --title 'next-layer <version>' --notes '…'`
6. `tools/package aur` and push `dist/aur/` to the AUR repository.

**Why no hosted-runner workflow builds this.** The binary links an engine whose
dependency tree is tens of gigabytes and hours of compilation. A GitHub-hosted
runner has neither a warm cache nor that much time per job, so a workflow claiming
to build it would either fail or cache-build a lie. CI runs what fits — `fish -n`
and qmllint through `tools/check --static` — and the release is built where the
build tree already exists. If this ever moves to CI, it needs a self-hosted runner
with `upstream/deps/build` warm, not a cleverer workflow file.
