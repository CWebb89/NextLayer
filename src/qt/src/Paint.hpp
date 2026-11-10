// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <Slic3r/Domain/TriangleMesh.hpp>
#include <Slic3r/Domain/TriangleSelector.hpp>

#include <QColor>

namespace next_layer {

enum class PaintKind
{
    Supports,
    Seam,
    /// A tool changer: the painted facets are printed by a different tool, and
    /// each tool has its own colour. Not MMU and not single-nozzle mixing --
    /// the engine stores it as a per-facet tool index, which is what a tool
    /// change reads.
    Tool
};

/// What the brush writes. Supports and seam both carry the engine's two states
/// (an enforcer and a blocker), and every kind can erase back to no state.
enum class PaintMode
{
    Enforce,
    Block,
    Erase
};

/// The reference's two brush shapes. A sphere paints through the part and a
/// circle paints what the eye can see; the circle is the default because a dab
/// on a wall with a sphere also painted the far side of the hull.
enum class BrushShape
{
    Circle,
    Sphere
};

/// Everything the brush reads besides the kind and the point it lands on. The
/// radius is in world millimetres; the overhang filter is applied by the engine
/// itself, which only takes facets whose normal falls within `highlightAngleDeg`
/// of straight down.
struct Brush
{
    float radius = 4.0f;
    BrushShape shape = BrushShape::Circle;
    bool overhangOnly = false;
    float highlightAngleDeg = 0.0f;
};

/// The engine's triangle-state type a paint mode writes.
Slic3r::Domain::TriangleSelector::TriangleStateType paintState(PaintKind kind, PaintMode mode,
                                                               int tool = 2);

/// Paints every facet within the brush of `centre` (mesh coordinates) and returns
/// how many triangle pieces changed state. `camera` is the eye in the same mesh
/// coordinates: the circle brush is projected from it, so what is painted is what
/// the view can see. `worldRotation` is the object's rotation into printer
/// coordinates, which is what tells the engine which way is down when the brush
/// is restricted to overhangs -- a mesh is stored Y-up and the object carries the
/// rotation that stands it up. The data is the engine's own triangle-splitting
/// representation, so what is stored here is exactly what the slicer reads from a
/// project file.
int paintSphere(const Slic3r::Domain::TriangleMesh& mesh,
                Slic3r::Domain::TriangleSelector::TriangleSplittingData& data,
                const Slic3r::Domain::Vec3f& centre,
                const Slic3r::Domain::Vec3f& camera,
                const Brush& brush,
                PaintKind kind,
                PaintMode mode,
                int tool = 2,
                int facetHint = -1,
                const Slic3r::Domain::Transform3d& worldRotation
                    = Slic3r::Domain::Transform3d::Identity());

/// The painted triangles of a kind, as an indexed triangle set in mesh
/// coordinates, with one colour per state.
Slic3r::Domain::indexed_triangle_set_with_color paintedTriangles(
    const Slic3r::Domain::TriangleMesh& mesh,
    const Slic3r::Domain::TriangleSelector::TriangleSplittingData& data,
    PaintKind kind);

bool paintIsEmpty(const Slic3r::Domain::TriangleSelector::TriangleSplittingData& data);

}
