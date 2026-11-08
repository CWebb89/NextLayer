# Network

next-layer has no account, no telemetry, no analytics and no crash uploader. This
page lists every place it can touch a network at all, so the claim is checkable
rather than a promise.

## The two calls a user causes

| Trigger | Destination | What is sent |
|---|---|---|
| Sending a print, or opening the Device tab for a configured printer | the host in your printer settings, over `http://host:port` | the G-code file, a start/stop command, or a status query — the same traffic any slicer sends to Moonraker, OctoPrint, PrusaLink or Duet |
| Pressing **Check for updates** in Preferences | `https://github.com/CWebb89/NextLayer/releases/latest/download/update.json` and, if you then choose to download, the release asset it names | a plain HTTPS GET for a JSON manifest; no identifiers beyond what any HTTPS request carries |

The update check never runs by itself: there is no timer and no check at startup,
only the button. The channel can be changed in the same dialog.

## The one that is dormant

The engine's own preset updater — which would fetch Prusa's vendor configuration
bundle — is constructed at startup and **never invoked**. No `.update()` call
exists in this tree, and it is additionally gated on the engine's
`enable_preset_update` setting. If a future feature calls it, this page and
`docs/release-audit.md` are the places that must change first.

## What is verified, and how

`tools/check` runs the headless slice inside a network namespace with no
interfaces (`unshare -rn`), so any attempt to connect fails at the syscall:

```
ok    network: sliced 125 layers in a namespace with no network
```

That proves slicing needs no network. It does not prove the windowed app never
phones home — a slice is not a UI session — so this page and the code references
in `docs/release-audit.md` carry that half, and the whole claim rests on the
audit being kept current rather than on the gate alone.

## What is not here

- No Spoolman or other filament-database connection.
- No printer discovery by broadcast or mDNS.
- No usage statistics of any kind, and no identifiers tied to you or your machine.
- Projects, presets and G-code stay on disk unless you send them somewhere.

If you find a connection this page does not list, that is a bug in this document
and in the app — open an issue with the host it contacted.
