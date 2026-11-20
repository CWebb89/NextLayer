// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QString>
#include <QTemporaryDir>

#include <memory>

#include "next-layer-app/src/bridge.rs.h"

namespace next_layer {

// The template, the ownership rule and the sweep are Rust's
// (src/app/src/Scratch.rs). Making a QTemporaryDir from a template is Qt's, so it
// stays here as an inline adapter rather than in a translation unit of its own.

/// A scratch directory named for the process that owns it, so a later run can
/// tell a leftover from one that is still in use.
inline QString scratchTemplate()
{
    return QString::fromStdString(static_cast<std::string>(scratch_template()));
}

inline std::unique_ptr<QTemporaryDir> makeScratchDir()
{
    return std::make_unique<QTemporaryDir>(scratchTemplate());
}

/// Removes the scratch directories of processes that no longer exist, and any
/// older than a day whose owner cannot be read.
inline void sweepScratchDirs()
{
    sweep_scratch_dirs();
}

}
