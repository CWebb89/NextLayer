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
int brandGcode(const QString& path);

}
