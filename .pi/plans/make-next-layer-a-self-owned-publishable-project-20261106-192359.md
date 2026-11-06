---
id: make-next-layer-a-self-owned-publishable-project-20261106-192359
status: awaiting_approval
created: 2026-11-06T19:23:59.781Z
updated: 2026-11-06T19:39:08.073Z
---

# Make next-layer a self-owned, publishable project

## Summary

Restructure to one repo whose history is 100% ours (engine becomes a bootstrapped dependency applied via a 2-line patch), scrub vendor names from all our code and docs, add licence/README/community files, fix release tooling, publish 0.1.0-alpha, then harden with an ASan + malformed-input gate. Each step ends in a measured check.

## Risks

- Step 1 is the only step that can break the build: adding our sources from outside the engine tree needs the explicit binary-directory form of add_subdirectory, and the patch context will fail loudly on any engine version bump (intended — it forces a look).
- Step 2 is a ~60-file comment-and-string diff. Behaviour is proven unchanged by 24 gates and 12 byte-identical G-code cases, so a regression cannot hide, but the diff is large and comment-only.
- Step 5 needs a pacman install (github-cli), which is a confirm-first action on this machine.
- Step 6 rebuilds the entire engine under sanitizers — hours of CPU under the memory-capped cgroup. Disk is fine (355 GB free).
- The three protocol identifiers (prusaslicer_config wire tokens, the prusa-research-fff bundle path, the PrusaLink host type) stay in code because renaming them changes behaviour; they will be the only vendor strings left outside the licence files.
- Repo name assumed to be CWebb89/next-layer because src/qt/src/Updater.cpp already hardcodes that channel.

## Steps

1. [x] Restructure to one repo: move upstream/src/slic3r-app-qt to src/qt and upstream/src/slic3r-app-spike to src/cli; export the 2 add_subdirectory lines as patches/0001-add-app-subdirectories.patch; add tools/bootstrap (shallow-clone the pinned engine commit, git am the patch); update every path in tools/check, tools/release, tools/save, tools/status, tools/fixtures, tools/cockpit, oracle/oracle.py, .gitignore and AGENTS.md; reduce tools/save to the single repo. Build tree stays at upstream/build so the 11 GB dependency build is not invalidated. Gate: reconfigure, build, tools/check 24/24 and oracle 12/12 on both runners.
   - files: src/qt, src/cli, patches/0001-add-app-subdirectories.patch, tools/bootstrap, tools/check, tools/release, tools/save, tools/status, tools/fixtures, tools/cockpit/cockpit, oracle/oracle.py, .gitignore, AGENTS.md
   - note: One repo: src/qt + src/cli at the root, patches/0001-app-subdirectories.patch, tools/bootstrap, single-repo save/status. Rebuilt from the new location; tools/check 24/24, oracle 12/12 qt and 11/11 app.
2. [ ] Neutralise naming across our tree: rename tools/orca-convert to tools/import-presets, tools/orca-import to tools/import-configs, tools/orca-catalog to tools/catalog, docs/orca-parity.md to docs/features.md; strip vendor names from comments and UI strings in ~60 C++/QML files; rename the calibration tests to next-layer's own labels. Leave exactly the three protocol/vendor-bundle identifiers, each with one comment stating it is a wire format or an on-disk engine path. Gate: grep -ri 'prusa\|orca' over src/, tools/, docs/ returns only allowlisted lines; tools/check still 24/24 and oracle 12/12 (proves the scrub changed no behaviour).
   - files: src/qt, src/cli, tools/import-presets, tools/import-configs, tools/catalog, docs/features.md, docs/real-prints.md, AGENTS.md
3. [ ] Write the public face and the legal notices: LICENSE (AGPL-3.0-or-later, Copyright (C) 2026 Curtis Webb), THIRD-PARTY.md naming the engine, the borrowed presets and meshes, and Qt with their licences, README.md (what it is, alpha status, build via tools/bootstrap, GL 3.3+ requirement, honest printing claim, not-affiliated disclaimer, pinned engine commit), CONTRIBUTING.md pointing at tools/check, SECURITY.md, .github issue and PR templates. Wire the About dialog to the licence and the repo URL. Gate: an About-dialog capture plus a tools/check run.
   - files: LICENSE, THIRD-PARTY.md, README.md, CONTRIBUTING.md, SECURITY.md, .github, src/qt/qml/views/AboutDialog.qml, src/qt/src/main.cpp
4. [ ] Fix release tooling: make tools/release read NEXT_LAYER_VERSION from the CMake source of truth and refuse a version mismatch, and add that as a tools/check gate. Gate: tools/release with a deliberately wrong version fails with a clear message; a matching one produces dist/next-layer-<version>.tar.gz and dist/update.json.
   - files: tools/release, tools/check, src/qt/CMakeLists.txt
5. [ ] Create the public repo: install github-cli, create CWebb89/next-layer, push branch main with tag 0.1.0-alpha, enable issues. Requires a yes for the package install.
6. [ ] Harden against hostile input: add a separate build-asan directory configured with -fsanitize=address,undefined, build the engine and app there, add oracle/malformed/ (truncated and corrupt STL/3MF, empty file, non-manifold mesh, lying G-code header) and a tools/check --asan gate, then fix everything it reports.
   - files: oracle/malformed, tools/check
7. [ ] Start the print matrix in docs/real-prints.md: every print on the second Voron plus the Orca-agnostic test set (cube, benchy, temperature tower, retraction, organic supports, high-poly), each with G-code hash and Moonraker job id, and every failure filed as an issue.
   - files: docs/real-prints.md
