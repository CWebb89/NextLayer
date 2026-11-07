# cockpit

Drives a GUI app on its own Hyprland workspace, so a viewport or a control can be
looked at and clicked instead of reasoned about. Built for driving next-layer and
a reference slicer side by side against the same model.

```
cockpit start next-layer --model oracle/models/cube_20.stl
cockpit shot /tmp/model.png
cockpit click-text "Slice plate"       # or: cockpit click 1682 92
cockpit shot /tmp/sliced.png
cockpit drag 1905 267 1905 600         # drag the layer slider handle
cockpit key escape
cockpit stop
```

Coordinates are pixels **inside the app window**, from its top-left corner, so
they do not change when the window is placed elsewhere. `cockpit windows` prints
the geometry that conversion uses, and `cockpit ocr` prints every word on screen
with its position in the same space — the fastest way to find a control.

## How it works

| Piece | What it does |
| --- | --- |
| workspace | `cockpit start` takes the next unused workspace id, focuses it, launches the command there, waits for a window, and records `ws`, window class and address in `/tmp/cockpit/<app>.json` |
| pointer position | `hl.dsp.cursor.move({x,y})` — Hyprland moves the cursor exactly, with no pointer acceleration |
| buttons, wheel | a throwaway virtual pointer on `/dev/uinput` (the udev ACL already grants this user access), created per command and destroyed afterwards |
| keys, type | a second virtual device that advertises only keys. One device carrying both a relative pointer and a keyboard is classified by libinput as a pointer, and the compositor then delivers clicks while dropping every key — which is why typing and Escape used to do nothing |
| capture | `shot onws <ws> out.png`, so only the app's workspace is captured and the agent's terminal is never in frame |
| labels | `find-text` / `click-text` OCR the capture with tesseract, matching a run of words on one line so `Slice plate` is found even though OCR returns two words |

Windows are re-resolved by class on the session workspace, because a reference
app replaces its window during start-up and a fullscreen toggle remaps it.

## Commands

| Command | Purpose |
| --- | --- |
| `start <app> [--command BIN] [--model FILE] [--workspace N] [--no-fullscreen]` | launch alone on an unused workspace |
| `status` / `windows` / `focus` / `stop` | session management |
| `shot <out.png>` | capture the workspace |
| `ocr` / `find-text TEXT` / `click-text TEXT [--index N]` | read the screen and click a label |
| `move X Y [--hover]`, `click X Y [--double] [--button]` | pointer; `--hover` also refreshes hover state |
| `drag X1 Y1 X2 Y2 [--steps N]` | press, move, release |
| `scroll N [--x X --y Y]` | wheel |
| `key NAME [--modifier ctrl] [--times N]` | keys, named from `linux/input-event-codes.h` (`keys` lists them) |
| `type TEXT` | US-layout text entry |
| `run FILE` | a script of the above, one command per line, `sleep N` allowed |

## Notes

- next-layer's dark theme OCRs exactly with the plain pass; a reference app's
  toolbar needs the inverted enlargement, so `find-text` tries both and prefers
  the first that matches. That app still garbles some small labels
  (`Slice plate` → `siceplate`); when a label will not resolve, read the position
  off a screenshot with `cockpit shot` + `cockpit ocr` and click coordinates
  directly.
- `move` warps the cursor and a warp sends the client no pointer event, so Qt
  keeps whatever hover state it had: hover fills, `HoverHandler`s and tooltips
  stay frozen where the pointer last *was*. `move --hover` warps and then turns
  the wheel one step each way at that point, which is what makes the client
  notice; the two steps cancel out.
- Everything runs through the compositor, so it is the same input path a person
  would take. There is no test-only hook in either application.
- The session file survives between commands, so the app keeps running while
  other work happens; `cockpit stop` closes it.

## Scripts

`scripts/` holds recorded sequences:

- `next-layer-cut.drive` — slice, switch to preview, drag the layer slider down
  and capture the cut.
- `reference-open.drive` — open a model in the reference slicer and capture the
  prepare view.
