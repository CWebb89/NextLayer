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

/// The engine's triangle-state type a paint kind writes. Support painting has
/// two states (enforcer and blocker); seam and multi-material write one.
Slic3r::Domain::TriangleSelector::TriangleStateType paintState(PaintKind kind, bool erase,
                                                               int tool = 2);

/// Paints every facet within `radius` of `centre` (mesh coordinates) and returns
/// how many triangle pieces changed state. The data is the engine's own
/// triangle-splitting representation, so what is stored here is exactly what the
/// slicer reads from a project file.
int paintSphere(const Slic3r::Domain::TriangleMesh& mesh,
                Slic3r::Domain::TriangleSelector::TriangleSplittingData& data,
                const Slic3r::Domain::Vec3f& centre,
                float radius,
                PaintKind kind,
                bool erase,
                int tool = 2,
                int facetHint = -1);

/// The painted triangles of a kind, as an indexed triangle set in mesh
/// coordinates, with one colour per state.
Slic3r::Domain::indexed_triangle_set_with_color paintedTriangles(
    const Slic3r::Domain::TriangleMesh& mesh,
    const Slic3r::Domain::TriangleSelector::TriangleSplittingData& data,
    PaintKind kind);

bool paintIsEmpty(const Slic3r::Domain::TriangleSelector::TriangleSplittingData& data);

}
