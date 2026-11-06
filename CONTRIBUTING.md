# Contributing

next-layer is alpha and single-maintainer, so the useful contributions right now
are bug reports with a reproduction, wrong-slicing reports with the profile that
caused them, and printer profiles for machines it does not know.

Everything here is licensed AGPL-3.0-or-later. By opening a pull request you agree
that your contribution is too.

## Before you change anything

```sh
tools/check          # every gate, ~25 s; needs the build tree configured
tools/check --fast   # the same minus the slicing oracle, ~8 s
```

A green run is the bar. `tools/check` does not build, but it fails the first gate
when the binary is older than its sources, naming the files — so a stale binary
cannot give you a false green.

The tools under `tools/` are Fish scripts. If you do not have Fish, read them: they
are short, and every one of them is a few `git`/`python3` commands you can run by
hand.

## The rules that are not negotiable

1. **`upstream/` is a dependency, not a place to keep work.** It is fetched by
   `tools/bootstrap` at a pinned commit and gitignored. Our footprint in it is
   `patches/0001-app-subdirectories.patch`, which adds two `add_subdirectory` lines
   to the engine's `src/CMakeLists.txt` and nothing else. Needing a third edit
   there is a signal to solve the problem in `src/`, not to extend the patch.
2. **Do not move a slicing baseline by accident.** `oracle/baseline/manifest.json`
   holds a SHA-256 per case. A change that alters G-code must say so and be
   re-captured deliberately (`python3 oracle/oracle.py capture`), with the diff
   read. If you did not mean to change toolpaths, a moved baseline is a bug.
3. **Do not change a model in `oracle/models/` without re-capturing.** Models are
   inputs to the fingerprints.
4. **Printer coordinates are Z-up** everywhere except the single transform into Qt
   Quick 3D's Y-up world.
5. **No inline comments.** Write code whose intent survives without them; module
   and public-API doc comments are welcome.
6. **No speculative error handling** and no defensive checks for things the code
   already guarantees. Validate what crosses a boundary — files, presets, network,
   user input.

## Adding a printer

`tools/printer-catalog/` generates a vendor bundle from another slicer's profile
checkout, and `tools/preset-import/` imports your own presets. A machine that
neither covers is a pull request against the templates in
`tools/printer-catalog/templates/`. After a change there, regenerate the catalog
and run `tools/check` — the catalog has its own gates.

## Commits

One commit, one change. Imperative subject, 72 characters or less; a body only when
the reason is not visible in the diff. `tools/save "what changed"` stages and
commits, and refuses to commit a clean tree.

## Reporting a bug

Include the version (`About next-layer`), your OS and GPU, and the smallest input
that reproduces it. For a slicing problem, `File ▸ Save project` gives a single
file containing the model and the exact settings, which is worth more than any
description.

Security problems do not go in the issue tracker — see `SECURITY.md`.
