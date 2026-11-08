// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QString>

namespace next_layer {

// The engine's resources tree: presets, bed meshes, icons, fonts. A packaged
// build carries it beside the binary and names it through NEXT_LAYER_RESOURCES;
// a build-tree run falls back to the path baked in at configure time.
QString resourcesDir();

}
