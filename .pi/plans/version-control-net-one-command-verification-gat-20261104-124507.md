---
id: version-control-net-one-command-verification-gat-20261104-124507
status: awaiting_approval
created: 2026-11-04T12:45:07.901Z
updated: 2026-11-04T12:45:07.901Z
---

# Version-control net, one-command verification gate, preset fixtures

## Summary

Two git repos (our code on a branch inside the vendored upstream repo, everything else in a repo at the project root) behind a two-command wrapper, plus tools/check and tools/fixtures so "is it still good" and "keep the preset state" are one command each.

## Risks

- Two repos instead of one: the wrapper hides it, but a manual git command must know which side it is on
- upstream is a shallow, detached clone of Prusa's repo — our commits live only here until pushed somewhere; updating the engine pin becomes a rebase of our patch series, not a re-clone
- qmllint baseline must not hide new warnings: it compares file+category sets and fails on anything not listed
- tools/check --ui needs a free Hyprland workspace and real input, so it stays opt-in rather than in the default gate
- oracle verify re-slices the whole corpus, ~1s per case, so the full check is ~1 minute rather than instant

## Steps

1. [ ] Create the project repo at ~/dev/next-layer (.gitignore ignoring /upstream/), and commit the harness: AGENTS.md, docs/, oracle/ (models + baselines + configs), tools/, .pi/plans/
   - files: /home/cwebb/dev/next-layer/.gitignore
2. [ ] Branch our own commits inside upstream/: git switch -c next-layer at the pinned 30ef591, commit src/slic3r-app-qt, src/slic3r-app-spike and the two add_subdirectory lines in src/CMakeLists.txt as a patch series; nothing pushed, origin stays Prusa's
   - files: /home/cwebb/dev/next-layer/upstream/src/CMakeLists.txt
3. [ ] Write tools/save (commit both repos with one message, refuse on detached HEAD or unmerged state) and tools/status (what is uncommitted, in either repo)
   - files: /home/cwebb/dev/next-layer/tools/save, /home/cwebb/dev/next-layer/tools/status
4. [ ] Write tools/check: staleness gate, qmllint against a warning baseline, --dump-tabs invariants, --selftest-slice, oracle verify --runner qt, headless eglfs smoke coverage; flags --fast (skip oracle), --ui (cockpit window check), --update-qmllint-baseline
   - files: /home/cwebb/dev/next-layer/tools/check, /home/cwebb/dev/next-layer/tools/qmllint-baseline
5. [ ] Prove tools/check end to end: run it green, then break one thing deliberately to confirm each gate actually fails
6. [ ] Write tools/fixtures save|restore|list|diff over the custom printer bundle and user presets, with a timestamped backup before any restore
   - files: /home/cwebb/dev/next-layer/tools/fixtures
7. [ ] Update AGENTS.md (how verification and saving now run) and the project memory file (layout, the two-repo rule, the upstream branch and what updating the pin now means)
   - files: /home/cwebb/dev/next-layer/AGENTS.md
