# Releasing next-layer

What stands between this tree and a published, versioned piece of software, in the
order it has to happen. Written 2026-11-06. Nothing here is optional-by-taste
except where it says so.

The short version: **the code is closer to releasable than the packaging and the
warranty are.** Nine of the ten things that block a 1.0 are not code.

---

## 1. The one thing that is not a choice: the licence

next-layer links `libslic3r` from PrusaSlicer 3.0, which is **AGPL-3.0**
(`upstream/LICENSE`). Linking it makes next-layer a derivative work, so the whole
program is AGPL-3.0. Qt Quick 3D, Qt Svg and Qt WebEngine are LGPL-3.0, and AGPL-3.0
satisfies LGPL-3.0, so the combination is fine — but the result is AGPL-3.0 with no
room to pick something else.

**AGPL forbids nothing here except being closed.** Publishing the source is
required anyway, so it costs nothing.

What that obliges, concretely:

| Obligation | How it is met |
|---|---|
| Ship the licence | `LICENSE` at the repo root, verbatim AGPL-3.0 text |
| Offer complete corresponding source | The public repo *is* that offer; the About box and README link to it |
| State modifications | Our two `add_subdirectory` lines and our directories are the whole delta, exported as patches (`§3`) |
| Third-party notices | `THIRD-PARTY.md`: PrusaSlicer (AGPL-3.0), OrcaSlicer profiles and calibration meshes (AGPL-3.0), Qt (LGPL-3.0), and each library in `upstream/deps` |
| No implied endorsement | README says plainly: not affiliated with, endorsed by, or supported by Prusa Research or the OrcaSlicer project. PrusaSlicer and OrcaSlicer are their trademarks; the app is called next-layer, not "Prusa-anything" |

The OrcaSlicer material already in this tree — presets converted in
`tools/orca-convert`, the generated printer catalog, and the calibration meshes in
`tools/calib-meshes` — is AGPL-3.0 content from an AGPL-3.0 project. Compatible,
but each file's origin must be named in `THIRD-PARTY.md`, which `tools/calib-meshes/README.md`
already does per model.

## 2. What "ready" means, in stages

Release in stages; each stage has a gate you can actually check.

### 0.1.0-alpha — source only, no binaries
Purpose: get the repo public so there is an issue tracker and a place to point
people. No binary, so no install support burden and no "it segfaulted on my
distro" reports.

Gate:
- `tools/check` green, oracle 12/12 on both runners.
- No secrets, no build output, no machine paths in the tree (§4).
- README exists and states that it is alpha, that it slices and prints, and what
  is unfinished (list the staged/needs-engine rows of `docs/orca-parity.md`).

### 0.2.0 – 0.9.x — binaries, marked beta
Purpose: packs to install without a compiler. Each tag is a real improvement
someone else can run.

Gate per tag: everything in §5 (quality gates) plus the print matrix in §6 having
grown since the last tag.

### 1.0.0 — "this is a slicer you can recommend"
Gate:
- Two months and ~50 real prints on at least two of your printers with no
  unexplained slice or crash.
- The four calibration tests that still print generated geometry wired to Orca's
  meshes, so every calibration path is the one you have already tested.
- A second person has sliced and printed from a binary you shipped, following
  only the README.
- No known data-loss or corrupt-G-code bug open.

**Answer to "when is it ready":** not yet, and the gap is measurable. Code-wise it
is arguably beta quality today — 24 gates, byte-identical G-code on 12 corpus cases,
a real print end to end. But one 20 mm cube is one print. The thing that makes a
slicer shippable is the boring middle: fifty prints across profiles you did not
write, and every failure triaged. That is 6–10 weekends of testing, not 6–10 of
coding.

## 3. Repository shape — the one real restructuring

Today the work lives in two git repos: this harness, and `upstream/` (a fork of
Prusa's tree with our code inside it). That layout is right for development and
wrong for publication, because a visitor cloning the public repo gets `/upstream/`
ignored and an empty program.

Two ways out:

**A. Recommended — one public repo, vendored engine, patch series.**
Move `src/slic3r-app-qt` and `src/slic3r-app-spike` to the repo root as `src/`, and
keep `upstream/` as a gitignored checkout produced by a script. The two
`add_subdirectory` lines, plus the out-of-tree source directory, become
`patches/0001-app-subdirectory.patch`, exported with `git format-patch` from the
`next-layer` branch. `tools/bootstrap` then does:

```fish
git clone --filter=blob:none --branch 3.0.0-alpha12 <prusa> upstream
cd upstream; git am ../patches/*.patch; cd ..
```

`add_subdirectory(<abs path to our src> splic3r-app-qt)` is legal CMake when the
binary dir is given explicitly, so our sources stay outside the engine tree and the
engine stays a clean checkout. Cost: one build-system change, validated by
rebuilding and re-running the oracle (12/12) before it is committed.

**B. Fallback — publish the fork.**
Push the `next-layer` branch to `CWebb89/next-layer` as the program, and the harness
as a second repo. It works, and the AGPL is satisfied, but the public repo carries
Prusa's 143 MB of history, the README has to explain which repo is the program, and
every contribution means a PR against a fork of a fork.

B is zero work. A is a day. A is what a normal project looks like.

Whichever: the pinned engine commit must be recorded in the README, and the branch
must be reproducible from the public history alone.

## 4. Before the first push

- `git log --oneline` in both repos: 19 and 21 commits, all ours, nothing to rewrite.
  Check for secrets: `git grep -iE 'api[_-]?key|password|token|BEGIN .*PRIVATE'`.
  Currently the only hit is `upload.api_key` in `PrinterLink.cpp`, which reads a
  Moonraker key from user settings — not a secret in the tree. Keep it that way; an
  API key never goes in a fixture, a test, or a screenshot.
- Absolute machine paths: none today (`grep -rn /home/cwebb` over our sources is
  empty). Keep `NEXT_LAYER_QML_DIR` baked at configure time.
- `.gitignore` covers `/upstream/`, build trees, `__pycache__`, `dist/`. Add
  `resources/presets/orca-catalog/` to the engine checkout's exclude (done, local
  only) and never to a tracked file.
- Screenshots before publishing: no printer addresses, no hostnames, no API keys in
  the Device tab. `10.0.0.x` is fine; a Moonraker key in a shot is not.
- Add `.github/`: issue templates (bug report asking for version, distro, GPU, and
  `--dump-config` output), a PR template, `SECURITY.md` with a private reporting
  address, `CONTRIBUTING.md` describing `tools/check` as the one command.

## 5. Quality gates to run before any tag

Already automated: `tools/check` (24 gates — build freshness, qmllint baseline,
headless render coverage, oracle byte-identity, updater, printer catalog, filament
presets). A tag must start from a green run on a clean build, not an incremental
one.

Missing, and cheap enough to justify:

| Gate | Why | How |
|---|---|---|
| Sanitizers | A slicer is a parser: STL, 3MF, G-code, and printer JSON all come from strangers | One `-fsanitize=address,undefined` build of `libslic3r` + app, run the oracle corpus and open 20 malformed files. Fix everything that fires. |
| Malformed-input suite | Same, deterministically | A directory of truncated/corrupt STL and 3MF, empty file, 1 GB file, non-manifold mesh, G-code with a lying header: each must report an error, not crash or hang |
| Static analysis | Catches what tests miss | `clang-tidy` on our two source dirs with a fixed check list; `cppcheck --enable=warning,performance` in CI |
| Huge-model test | Portability of the vertex path | A 5 M-triangle mesh loads, orbits at >20 fps, and slicing does not exceed RAM |
| Long-run test | Preview and printer link leak | Open/slice/close 100 models; watch RSS stay flat |

The `project-auditor` skill exists for exactly the manual half of this: a written
review of our code against contract/regression/robustness, which turns into issues
before a release rather than after.

## 6. Print matrix — the part only you can do

A slicer is verified by the printer, and one cube verifies almost nothing. Record
each in `docs/real-prints.md` with the hash of the G-code and the Moonraker job.

Minimum for 1.0:

- **Printers:** 2 of your 3 Vorons, one of them not the one the app was developed
  against.
- **Models (each on both printers):** 20 mm cube (dimensional), Benchy (overhangs,
  bridging, cooling), the Orca temperature tower, a retraction test, a model with
  organic supports whose supports must come off without damaging the part, a
  high-poly organic model (slicing time and preview stability), and one part of
  yours that has a real job.
- **Per print, record:** dimensional error on the cube, support removal result,
  first-layer quality, any stringing, and whether the G-code preview matched what
  came out.
- **Failure triage:** every failure gets an issue with the model and the G-code.
  A failure with no issue filed does not count as tested.

That is the checklist that answers "is it good enough", and no amount of code
reading substitutes for it.

## 7. CI on GitHub

Be realistic: a cold build of PrusaSlicer's dependencies plus `libslic3r` is hours
and tens of GB. Do not put that on every PR.

- **PR workflow (fast, <5 min):** QML lint against `tools/qmllint-baseline`, Python
  tools with `python -m compileall` and their own tests, `clang-format --dry-run` on
  our sources, docs link check, and `tools/check --fast` where a binary is available.
- **Nightly workflow:** full build with `ccache` and a cached `deps/build`
  (GitHub's 10 GB cache limit is the binding constraint — cache deps, not the
  build tree), then the oracle at 12/12 and the headless frame gate. Expect the
  first run to take an hour; after the cache it is 20–30 min.
- **Release workflow, on tag:** build, `tools/release <version>`, upload
  `dist/next-layer-<version>.tar.gz` and `dist/update.json` with `gh release create`.
  The updater already reads
  `https://github.com/CWebb89/next-layer/releases/latest/download/update.json`, so
  the tag has to be a real GitHub release or updates silently stop.

`gh` is not installed on this machine; publishing needs it.

## 8. Release mechanics and one hardening item

`tools/release 0.1.0 "notes"` already produces the tarball and `update.json` and
prints the `gh release create` command. The updater verifies sha256 over TLS.

**Missing: a signature.** The update path replaces the binary the user runs and the
only thing guarding it is the TLS connection to github.com plus a hash published
next to the file. If the release account is compromised, the hash is compromised
with it. Sign the tarball (minisign or `gpg --detach-sign`), publish the signature
alongside, and have the updater check it against a public key baked into the binary.
That is roughly a day's work and it is the difference between "an updater" and "a
supply chain".

Also: keep the version in one place. `NEXT_LAYER_VERSION` in
`src/slic3r-app-qt/CMakeLists.txt` is the source of truth, and today nothing checks
it: `tools/release` takes a version on the command line and will happily stamp
`0.3.0` onto a binary built as `0.1.0`. It should read the constant and refuse a
mismatch.

## 9. Packaging

`tools/release` ships a tarball: binary + `qml/`, expecting the distro's Qt 6.12+,
Qt WebEngine, and a GL driver. That is fine for a beta and wrong for 1.0 — a slicer
that needs you to install Qt WebEngine is a slicer most people cannot run.

Order of value:
1. **AppImage** — bundles Qt, WebEngine and the GL runtime assumptions, one file,
   runs on any glibc distro. `linuxdeploy` + `linuxdeploy-plugin-qt`. Test on a
   clean VM with no Qt installed; that test is the deliverable, not the AppImage.
2. **Flatpak** — better sandboxing and integration, more work, and the printer link
   needs `--share=network` plus access to port 7125. Do it after AppImage.
3. **AUR PKGBUILD** — Arch users only, trivial to write, do it whenever.

Document the GL requirement: Qt Quick 3D needs OpenGL 3.3+ or Vulkan; a headless or
llvmpipe-only machine will be slow or blank. Say so in the README instead of letting
people file it as a bug.

## 10. Order of work, next sessions

1. `LICENSE`, `THIRD-PARTY.md`, `README.md`, `.github/` templates — half a day, and
   it unblocks publication. (Needs a yes on the licence header: "Copyright (C) 2026
   Curtis Webb" and AGPL-3.0-or-later.)
2. Sanitizer build plus the malformed-input suite, fix what fires — 1–2 days, and
   the biggest real bug-finding step available.
3. Restructure to one repo (option A in §3): move `src/`, export the patch, add
   `tools/bootstrap`, rebuild, oracle 12/12 — a day.
4. Push `0.1.0-alpha`, source only, with issues enabled.
5. Print matrix in parallel with 6–10. Every failure becomes an issue, fixed or
   documented as a known limitation.
6. AppImage, `tools/release 0.2.0`, first binary release.
7. Signature on the update path.
8. Keep going until the 1.0 gate in §2 is met.

## Open decisions

- **Licence header name and year** for `LICENSE` and the About box.
- **Repo shape:** option A (restructure, recommended) or B (publish the fork).
- **Public repo name:** the updater already hardcodes `CWebb89/next-layer`.
- **What the README promises:** "prints on my Vorons, your printer may need a
  profile" is honest and cheap; "works with any Klipper printer" is not yet true.
