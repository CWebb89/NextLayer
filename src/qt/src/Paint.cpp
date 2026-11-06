// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "Paint.hpp"

#include <Slic3r/Biz/Algorithms/FacetsAnnotation.hpp>
#include <Slic3r/Biz/Algorithms/TriangleSelector.hpp>

#include <vector>

namespace next_layer {

namespace {

using Slic3r::Biz::Algorithms::TriangleSelector;
using Slic3r::Domain::TriangleSelector::TriangleSplittingData;
using Slic3r::Domain::TriangleSelector::TriangleStateType;

TriangleStateType enforcerState(PaintKind kind, int tool)
{
    switch (kind) {
    case PaintKind::Supports: return TriangleStateType::ENFORCER;
    case PaintKind::Seam: return TriangleStateType::ENFORCER;
    case PaintKind::Tool: return static_cast<TriangleStateType>(tool);
    }
    return TriangleStateType::NONE;
}

TriangleStateType blockerState(PaintKind kind)
{
    switch (kind) {
    case PaintKind::Supports: return TriangleStateType::BLOCKER;
    case PaintKind::Seam: return TriangleStateType::NONE;
    case PaintKind::Tool: return TriangleStateType::NONE;
    }
    return TriangleStateType::NONE;
}

}

TriangleStateType paintState(PaintKind kind, bool erase, int tool)
{
    return erase ? blockerState(kind) : enforcerState(kind, tool);
}

bool paintIsEmpty(const TriangleSplittingData& data)
{
    return data.triangles_to_split.empty();
}

int paintSphere(const Slic3r::Domain::TriangleMesh& mesh, TriangleSplittingData& data,
                const Slic3r::Domain::Vec3f& centre, float radius, PaintKind kind, bool erase,
                int tool, int facetHint)
{
    if (radius <= 0.0f || mesh.its.indices.empty())
        return 0;

    const TriangleStateType state = paintState(kind, erase, tool);
    if (state == TriangleStateType::NONE)
        return 0;

    TriangleSelector selector(mesh);
    if (!data.triangles_to_split.empty())
        selector.deserialize(data);

    int before = 0;
    for (const TriangleSelector::Triangle& triangle : selector.triangles()) {
        if (triangle.valid() && !triangle.is_split() && triangle.get_state() == state)
            ++before;
    }

    // The sphere cursor is in mesh coordinates, with an identity mesh-to-world
    // matrix: paint must not depend on where the object currently sits.
    const Slic3r::Domain::Transform3d identity = Slic3r::Domain::Transform3d::Identity();
    const Slic3r::Domain::Vec3f camera = centre + Slic3r::Domain::Vec3f(0.0f, 0.0f, 1.0f);
    std::unique_ptr<TriangleSelector::Cursor> cursor =
        TriangleSelector::SinglePointCursor::cursor_factory(
            centre, camera, radius, TriangleSelector::CursorType::SPHERE, identity, {},
            0.0f);

    // The pick already knows which face was under the cursor, and it is the only
    // reliable answer: a point that lies exactly on a face is ambiguous to a
    // nearest-triangle query.
    const int facet = facetHint >= 0 ? facetHint : selector.select_unsplit_triangle(centre, -1);
    if (facet < 0 || static_cast<size_t>(facet) >= mesh.its.indices.size())
        return 0;

    selector.select_patch(facet, std::move(cursor), state, identity, false);

    const TriangleSplittingData updated = selector.serialize();
    data = updated;

    int after = 0;
    for (const TriangleSelector::Triangle& triangle : selector.triangles()) {
        if (triangle.valid() && !triangle.is_split() && triangle.get_state() == state)
            ++after;
    }
    return after - before;
}

Slic3r::Domain::indexed_triangle_set_with_color paintedTriangles(
    const Slic3r::Domain::TriangleMesh& mesh, const TriangleSplittingData& data, PaintKind kind)
{
    if (data.triangles_to_split.empty())
        return {};

    TriangleSelector selector(mesh);
    selector.deserialize(data);
    return selector.get_all_facets_with_colors();
}

}
