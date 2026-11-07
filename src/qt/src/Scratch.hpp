// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QString>

#include <memory>

class QTemporaryDir;

namespace next_layer {

/// A scratch directory named for the process that owns it, so a later run can
/// tell a leftover from one that is still in use. A plate is staged as a
/// multi-megabyte STL, and on a machine whose /tmp is a small tmpfs the
/// directories a crash or a killed session leaves behind are enough to fill it
/// and break every other program that writes there.
QString scratchTemplate();

std::unique_ptr<QTemporaryDir> makeScratchDir();

/// Removes the scratch directories of processes that no longer exist, and any
/// older than a day whose owner cannot be read.
void sweepScratchDirs();

} // namespace next_layer
