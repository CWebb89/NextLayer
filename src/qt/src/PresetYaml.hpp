// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QString>

#include <string>

#include "next-layer-app/src/bridge.rs.h"

namespace next_layer::yaml {

// These are the Qt-typed adapters onto the Rust implementation in
// src/app/src/PresetYaml.rs. Every byte of the scalar is Rust's; this header
// exists so the C++ that still writes preset files keeps its spelling.

/// One YAML scalar the engine's preset reader accepts. Single-quoted style is
/// used everywhere it can be, which keeps names, enum spellings and `x xy`
/// points verbatim; a multi-line string (start/end G-code) has to switch to the
/// double-quoted style so its newlines survive as escapes.
inline QString quoted(const QString& text)
{
    return QString::fromStdString(static_cast<std::string>(yaml_quoted(text.toStdString())));
}

/// `QString::number(value, 'g', 15)`, as the engine's reader has always been fed
/// it: fifteen significant digits, trailing zeros dropped, a signed two-digit
/// exponent, and `-0`, `inf` and `nan` spelled Qt's way.
inline QString number(double value)
{
    return QString::fromStdString(static_cast<std::string>(yaml_number(value)));
}

/// The inverse of quoted() for one scalar: the catalog writes `id: 'Anycubic'`
/// where the engine's own bundles write `id: Anycubic`, and a comparison that
/// does not strip the quotes silently finds no bundle at all.
inline QString unquoted(const QString& text)
{
    return QString::fromStdString(static_cast<std::string>(yaml_unquoted(text.toStdString())));
}

/// One `x xy` point: `quoted(number(x) + 'x' + number(y))`, so the separator sits
/// inside the quotes and a bed shape reads `- '0x0'` and `- '350x0'`.
inline QString point(double x, double y)
{
    return QString::fromStdString(static_cast<std::string>(yaml_point(x, y)));
}

} // namespace next_layer::yaml
