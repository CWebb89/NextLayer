// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include <array>

namespace next_layer {

/// What a project file says about where its objects go, read straight from the
/// records its authoring slicer writes.
///
/// The engine's reader returns each object centred on its own bounding box, so
/// a file authored on a plate with fifteen parts arrives with all fifteen in the
/// same place. The scene's build list is where the arrangement survives: one
/// `<item>` per placed object, in the order the engine returns them, each with
/// the 4x3 placement matrix that positions it on the plate. That matrix is what
/// this reader collects, and it is the only reason a multi-part project loads as
/// the author left it.
///
/// The plate each object belongs to is in the same project's plate records (the
/// scene holds every plate in one coordinate space, hundreds of millimetres
/// apart, so position alone cannot identify a plate).
struct ThreeMfProject
{
    /// Twelve numbers per build item, the 3MF `<item transform>` in row-major
    /// 4x3 form: the first three rows are the rotation and scale, the last row
    /// is the translation.
    using Placement = std::array<double, 12>;

    bool valid = false;
    int plateCount = 0;
    QVector<int> buildOrder;
    QVector<Placement> buildTransforms;
    QHash<int, int> plateOfObject;
};

ThreeMfProject readThreeMfProject(const QString& path);

} // namespace next_layer
