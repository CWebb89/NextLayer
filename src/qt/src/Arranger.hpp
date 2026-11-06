// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <Slic3r/Domain/TriangleMesh.hpp>
#include <Slic3r/Domain/Types.hpp>

#include <QVector2D>

#include <optional>
#include <vector>

namespace next_layer {

struct PlaceableObject
{
    int id = 0;
    const Slic3r::Domain::TriangleMesh* mesh = nullptr;
    Slic3r::Domain::Transform3d transform = Slic3r::Domain::Transform3d::Identity();
};

struct Placement
{
    int id = 0;
    Slic3r::Domain::Vec3d offset = Slic3r::Domain::Vec3d::Zero();
    double rotationZ = 0.0;
};

/// Packs the objects onto a rectangular bed with the engine's own arranger --
/// the same NFP kernel the plater uses -- and returns where each one landed.
/// Objects keep their height and their own rotation; only the bed position and
/// the optional in-plane rotation change. An empty result means nothing could be
/// placed, so the caller leaves the plate alone.
std::vector<Placement> arrangeOnBed(const std::vector<PlaceableObject>& objects,
                                    QVector2D bedCentre,
                                    QVector2D bedSize,
                                    bool allowRotations);

}
