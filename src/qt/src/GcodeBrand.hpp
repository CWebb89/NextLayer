// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QString>

#include <string>

#include "next-layer-app/src/bridge.rs.h"

namespace next_layer {

// These are the Qt-typed adapters onto the Rust implementation in
// src/app/src/GcodeBrand.rs. The rewrite itself, the digest and the version are
// Rust's; this header exists so the C++ that still calls them keeps its spelling.

/// The version this build stamps into G-code and shows in the window.
inline QString applicationVersion()
{
    return QString::fromStdString(static_cast<std::string>(application_version()));
}

inline int brandGcode(const QString& path, const QString& configurationJson = {})
{
    return brand_gcode(path.toStdString(), configurationJson.toStdString());
}

inline QString toolpathDigest(const QString& path)
{
    return QString::fromStdString(static_cast<std::string>(toolpath_digest(path.toStdString())));
}

}
