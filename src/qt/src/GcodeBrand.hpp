// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QString>

namespace next_layer {

/// Rewrites the branding the engine stamps into an exported G-code file. The
/// engine hardcodes its own application name in the first line, in the JSON
/// producer fields, and in the delimiters of the two config blocks it appends.
/// All of them are rewritten to this application's name; every config key and
/// value inside those blocks is left byte-identical.
QString applicationVersion();

/// Returns the number of lines rewritten, or -1 when the file cannot be read or
/// written. Idempotent: a file that is already branded is only touched where the
/// version line differs.
///
/// \p configurationJson, when given, is the resolved settings document the slice
/// was handed: it is appended as one metadata block, so the file records the
/// settings it was printed with. The engine behind this application writes only
/// header lines, and a print whose settings are nowhere in it cannot be
/// reproduced.
int brandGcode(const QString& path, const QString& configurationJson = {});

/// sha256 of a toolpath's move and command lines, comments left out: the same
/// slice run twice differs in its timestamp and UUIDs and nowhere else, so this
/// is what two runs are compared by.
QString toolpathDigest(const QString& path);

}
