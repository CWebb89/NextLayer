# Bundled font

`MaterialSymbolsOutlined.ttf` is Material Symbols Outlined, the icon font the
whole interface draws with. It used to be a system package; now it ships inside
the binary, so the icons are the same on a machine that has never installed the
font and inside the AppImage, which bundles Qt but not system fonts.

- Source: <https://github.com/google/material-design-icons>
  (`variablefont/MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf`)
- Licence: Apache-2.0, full text beside this file
- sha256: `f7837ba8573d9a7a74931618ced8307bc81f96d49af296bf6c8052c772ed5282`

The variable file is shipped whole rather than subset to the icons in use.
Icon names reach QML at runtime from model roles, so a subset would silently
render a missing glyph for any name the build never saw, and the `FILL` axis is
what makes `filled: true` a real filled glyph instead of a synthetic bold.
Ten megabytes buys both; a subset would be a few hundred kilobytes and a class
of bug that only appears on someone else's screen.

To update: replace the `.ttf`, regenerate the sha256 above, and run
`tools/check --fast` — the `icons` gate proves the family resolves and that a
ligature name still maps to one glyph.
