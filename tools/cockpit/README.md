# cockpit

Drives a GUI app on its own Hyprland workspace, so a viewport or a control can be
looked at and clicked instead of reasoned about. Built for driving next-layer and
a reference slicer side by side against the same model — and for driving the
reference slicer's own dialogs, which is a harder problem than it sounds.

```
cockpit start next-layer --model oracle/models/cube_20.stl
cockpit shot /tmp/model.png            # cropped to the target window
cockpit click-text "Slice plate"       # or: cockpit click 1682 92
cockpit windows --all                  # every window of the app, and the target
cockpit window "Printer settings"      # point later commands at that window
cockpit describe                        # windows, target, every readable label
cockpit want "Filament"                # on screen? if not, what would show it
cockpit atlas record --page Filament   # remember this page's controls
cockpit run tools/cockpit/scripts/orca-settings.drive
cockpit stop
```

Coordinates on the command line are pixels **inside the target window**, from its
top-left corner, so they do not move when the window is placed elsewhere. Every
reported coordinate says which frame it is in, and every capture is cropped to
the same window, so a click and a capture always speak the same language.

## The target window

A session starts with the app's main window, and the target follows whichever
window the tool is actually driving:

1. `--window` on the command (or before it): an address prefix, a title glob or
   an index from `cockpit windows`.
2. the window the session last clicked — so a sequence stays pointed at the dialog
   it just opened. `cockpit window TITLE` sets it by hand, `--clear` drops it.
3. the focused window, if it belongs to the app on the session workspace.
4. the main window.

`cockpit` prints a line whenever the target is not the main window, once per
change. Orca and Prusa open their preset editors as separate top-level windows,
often modal and often offset; the line is how a stale popup stops being invisible.

## Commands

| Command | Purpose |
| --- | --- |
| `start <app> [--command BIN] [--model FILE] [--workspace N] [--window-title GLOB] [--no-fullscreen]` | launch alone on an unused workspace; `--window-title` waits for the real window instead of a splash |
| `status` / `windows [--all] [--json]` / `focus` / `stop` | session and window management; `stop` closes every window of the app, dialogs first |
| `window [SPEC] [--clear]` | set, show or clear the remembered target |
| `log [--lines N]` | what the app wrote to stdout and stderr since it started |
| `describe` | windows, target, and every readable label with its coordinates |
| `want TEXT` | one line: where it is, or what the window shows and what to open |
| `shot <out.png> [--workspace-frame]` | capture the target window (or the whole workspace) |
| `ocr [--json]` / `find-text TEXT [--near X,Y] [--json]` | read the target window |
| `click-text TEXT [--index N] [--near X,Y] [--no-atlas]` | click a label, else its recorded atlas position |
| `move X Y [--hover]`, `click X Y [--no-focus]` | pointer; `--no-focus` leaves an open menu alone |
| `drag X1 Y1 X2 Y2 [--steps N]`, `scroll N [--x X --y Y]` | drag and wheel, in the target window |
| `key NAME [--modifier ctrl] [--times N] [--no-focus]`, `keys`, `type TEXT` | keyboard (`keys` lists `KEY_*` names) |
| `assert-text TEXT` / `wait-text TEXT [--timeout N]` | fail unless a label is on screen |
| `tip X Y [--wait N] [--above] [--json]` | hover a control and read its tooltip — the only label an icon has |
| `tips --row Y --from X --to X --step N` | sweep a row of controls, one tooltip per line |
| `assert-tip X Y TEXT` | fail unless that control's tooltip says this |
| `assert-window GLOB` / `wait-window GLOB [--timeout N]` | fail unless a window exists |
| `atlas record [--title T] [--page P]` / `atlas show [--labels]` / `atlas find TEXT` | the per-app record of controls |
| `run FILE` | a script of the above, one command per line, `sleep N` allowed; stops at the first failing step. A step prefixed `try` still prints and its failure is forgiven — for closing a dialog the app under test raises about its own environment |

## How it works

| Piece | What it does |
| --- | --- |
| workspace | `start` takes the next unused workspace id, focuses it, launches the command there, waits for a window, and records `ws`, window class and address in `/tmp/cockpit/<app>.json` |
| pointer position | `hl.dsp.cursor.move({x,y})` — Hyprland moves the cursor exactly, with no pointer acceleration |
| buttons, wheel | a throwaway virtual pointer on `/dev/uinput` (the udev ACL already grants this user access), created per command and destroyed afterwards |
| keys, type | a second virtual device that advertises only keys. One device carrying both a relative pointer and a keyboard is classified by libinput as a pointer, and the compositor then delivers clicks while dropping every key — which is why typing and Escape used to do nothing |
| capture | `shot onws <ws> out.png`, so only the app's workspace is captured and the agent's terminal is never in frame, then `magick -crop` down to the target window |
| labels | tesseract on the window crop: raw psm 3 and 11, plus a grey/inverted/contrast-stretched 250% copy at psm 6 and 11. Matches are merged across the four passes and deduplicated; a query matches a run of words on one line with spaces ignored, with a fuzzy fallback for the labels wxWidgets renders at 11 px |
| atlas | `<repo>/tools/cockpit/atlas/<app>.json`: window title → page → label → window-relative box, recorded once by `atlas record` and used when OCR cannot read a control |

A click warps to four pixels short of the point and then *walks* the last four
with real relative motion. A warp sends the client no pointer event, and a wx
control that only exists while hovered (Orca's preset edit pencil) is then not
there when the press arrives: the press lands on the panel behind it and the
click looks dead. Hover state in general still needs `move --hover`.

Windows are re-resolved by class on the session workspace, because a reference
app replaces its window during start-up and a fullscreen toggle remaps it.

## Reading a toolbar of icons

An icon-only button has no label anywhere in the frame; its tooltip is the whole
interface. `tip` hovers a point, waits for the tooltip, and OCRs the band around
the pointer — the tooltip is its own popup, so it is not in the window crop, and
the workspace frame has to be read instead.

Three things about tooltips are worth knowing before blaming the OCR:

- **The band is searched, not assumed.** wx puts a tooltip left or right of the
  cursor depending on the widget, and Orca's Cut opens it *above*. The band around
  the pointer is cropped generously, thresholded and trimmed to the tooltip's own
  dark box, and the plainest usable preprocessing wins: `-auto-level` over a crop
  that is one small dark box destroys the glyph edges that plain negate keeps.
- **A tooltip does not appear on every dwell.** `shot onws` *switches* to the
  session workspace when it is not active, and the app reads that as the pointer
  leaving: the tooltip is hidden and its timer restarted. The workspace is focused
  first, the pointer is *walked* into the control with real motion (a warp sends
  no pointer event and the hover timer never starts), and a miss is retried from a
  fresh approach rather than reported as a label the tool could not read.
- **A disabled button may say nothing at all.** `--tries` retries the hover; the
  drive asserts only the buttons whose tooltip is stable, and says so in its own
  comment rather than pretending the row is fully covered.

A tooltip is short and OCR mangles single letters (`Boolean` → `Boolkean`), so
`assert-tip` compares with the same fuzzy ratio `find-text` uses, on text folded
to lowercase with spaces and the `[shortcut]` suffix removed.

## The atlas

Small wx labels OCR badly (`Slice plate` → `sicepate`), but they do not move. The
atlas records what one capture saw — single words and runs of up to three words,
with their boxes — under the window title and a page name, and `click-text` falls
back to it only when OCR finds nothing. A hit prints the date it was recorded, so
a stale entry is visible rather than silent. Recording a page replaces that page's
labels, because a merge cannot tell "this label moved" from "this page is blank".

An empty page records zero labels and that is honest: Orca's Notes pages are empty
text boxes, and the drive asserts only that the dialog survived the switch.

## Notes

- Everything runs through the compositor, so it is the same input path a person
  would take. There is no test-only hook in either application.
- `start` redirects the app's output to `/tmp/cockpit/<app>.log` and sets
  `QT_ASSUME_STDERR_HAS_CONSOLE=1`. Without that variable Qt drops every message —
  warnings and QML `console.log` alike — whenever stderr is not a terminal.
- Orca, BambuStudio and PrusaSlicer are wxWidgets/GTK, not Qt. A wx child takes the
  wheel only with the pointer over it, notebook tabs are custom-painted, and a tab
  that is not selected is grey enough that no OCR pass reads it: click tabs by
  coordinate from the atlas, and assert the page instead of the tab.

## Scripts

- `scripts/next-layer-cut.drive` — slice, switch to preview, drag the layer slider
  down and capture the cut.
- `scripts/reference-open.drive` — open a model in the reference slicer and capture
  the prepare view.
- `scripts/orca-settings.drive` — open Orca's Printer settings and its Filament
  ("Material settings") editor, visit every tab, and assert each page's own label.
- `scripts/orca-process.drive` — widen the left panel so all six process pages fit,
  then visit Quality, Strength, Speed, Support, Multimaterial and Others, asserting
  a label only that page shows; puts the panel width back before it returns.
- `scripts/orca-viewport.drive` — hover every button of the 3D viewport's toolbar
  and assert what its tooltip says it does (16 buttons; the disabled gizmo
  toggles show no tooltip and are named in the script).

All three assume the left panel at its default width — widening it moves the whole
viewport toolbar right — and the process drive restores it for the next one.

`tools/check --ui` runs all three as gates; they skip with a named reason when
`orca-slicer` is not installed.
