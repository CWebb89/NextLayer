# Security policy

next-layer is a desktop application that parses files from other people and then
sends G-code to a machine that moves at speed and heats to 300 °C. Both halves
are treated as security-relevant: a memory-safety bug in a mesh parser and a
malformed config that produces a crashing toolpath are the same class of problem
here.

## Reporting

Open a **private security advisory** on this repository (Security ▸ Advisories ▸
Report a vulnerability). Do not open a public issue for anything that could be
exploited or that could damage a printer.

Include what you can: version (`About next-layer`, in the application menu), the
input file that triggers it if it is not confidential, and whether the failure is a
crash, memory corruption, wrong G-code, or a network/host problem.

There is no bug bounty and no paid support. Expect a reply within a week.

## Supported versions

Only the current `main` and the most recent tag receive fixes. This is alpha
software: there are no backports to older tags.

## What the threat model covers

- **File parsers.** STL, OBJ, 3MF, AMF, SVG and the project format are handled by
  the linked engine; G-code and preset YAML/JSON are handled by this application.
  A malicious or corrupt file must not read or write outside its own data, and
  must not execute anything.
- **Preset and config input.** Presets carry custom G-code, which is emitted into
  a print. Custom start/end G-code from an imported preset is therefore executable
  on the printer, by design — importing a preset bundle you did not write is
  equivalent to running someone's script on your machine. Report anything that
  makes that reachable without an explicit import.
- **Network.** The application listens on nothing. It makes outbound connections
  in exactly two cases: to a print host you configured (Moonraker, OctoPrint,
  Duet, PrusaLink), and to the update channel, which fetches a release manifest
  and verifies the artifact's SHA-256 against it. Host API keys are written to the
  app's own JSON config file on disk in clear text — not encrypted, not in a
  keyring.
- **No telemetry.** Nothing about your models, prints or usage leaves the machine
  unless you send a print or check for updates.

## What is not a vulnerability

- Custom G-code in a preset you imported doing what it says.
- A print failing because a profile's values are wrong for your machine.
- The update channel pointing at a repository you do not trust — `NEXT_LAYER_UPDATE_URL`
  overrides it, and the checksum check is what makes a release acceptable.
- Denial of service by opening a deliberately enormous mesh, unless it defeats a
  documented limit rather than just using a lot of memory.
