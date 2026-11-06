#pragma once

#include <QString>

namespace next_layer {

/// Rewrites the branding the engine stamps into an exported G-code file. The
/// engine hardcodes its own application name in the first line and in the JSON
/// producer fields, and those are the two strings a user sees when the file is
/// opened in an editor or handed to a printer host. The compatibility tokens the
/// engine and PrusaSlicer/Orca use to parse a G-code config block
/// (`; prusaslicer_config`, `; prusaslicer_json_config`) are deliberately left
/// alone: they are a wire format, not branding.
QString applicationVersion();

/// Returns the number of lines rewritten, or -1 when the file cannot be read or
/// written. Idempotent: a file that is already branded is only touched where the
/// version line differs.
int brandGcode(const QString& path);

}
