// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "Paint.hpp"

#include <Slic3r/Biz/Algorithms/FacetsAnnotation.hpp>
#include <Slic3r/Biz/Algorithms/TriangleSelector.hpp>

#include <QtGlobal>

#include <cmath>
#include <cstdio>
#include <vector>

namespace next_layer {

namespace {

using Slic3r::Biz::Algorithms::TriangleSelector;
using Slic3r::Domain::TriangleSelector::TriangleSplittingData;
using Slic3r::Domain::TriangleSelector::TriangleStateType;

TriangleStateType stateFor(PaintKind kind, PaintMode mode, int tool)
{
    if (mode == PaintMode::Erase)
        return TriangleStateType::NONE;
    if (mode == PaintMode::Block)
        return TriangleStateType::BLOCKER;
    switch (kind) {
    case PaintKind::Supports: return TriangleStateType::ENFORCER;
    case PaintKind::Seam: return TriangleStateType::ENFORCER;
    case PaintKind::Tool: return static_cast<TriangleStateType>(tool);
    }
    return TriangleStateType::NONE;
}

int countInState(const TriangleSelector& selector, TriangleStateType state)
{
    int count = 0;
    for (const TriangleSelector::Triangle& triangle : selector.triangles()) {
        if (triangle.valid() && !triangle.is_split() && triangle.get_state() == state)
            ++count;
    }
    return count;
}

}

TriangleStateType paintState(PaintKind kind, PaintMode mode, int tool)
{
    return stateFor(kind, mode, tool);
}

bool paintIsEmpty(const TriangleSplittingData& data)
{
    return data.triangles_to_split.empty();
}

int paintSphere(const Slic3r::Domain::TriangleMesh& mesh, TriangleSplittingData& data,
                const Slic3r::Domain::Vec3f& centre, const Slic3r::Domain::Vec3f& camera,
                const Brush& brush, PaintKind kind, PaintMode mode, int tool, int facetHint,
                const Slic3r::Domain::Transform3d& worldRotation)
{
    if (brush.radius <= 0.0f || mesh.its.indices.empty())
        return 0;

    const TriangleStateType state = stateFor(kind, mode, tool);

    TriangleSelector selector(mesh);
    if (!data.triangles_to_split.empty())
        selector.deserialize(data);

    const int before = countInState(selector, state);

    const Slic3r::Domain::Transform3d identity = Slic3r::Domain::Transform3d::Identity();

    // A circle, not a sphere, and projected from the eye that is actually looking
    // at the part: the engine's sphere cursor paints through the mesh, so a dab on
    // a wall also painted the far side of the hull. The cursor wants the camera in
    // mesh coordinates, which is what makes the circle foreshorten correctly on a
    // face turned away from the view.
    //
    // The edge limit is the stop condition for the subdivision the brush does: the
    // selector splits a triangle until its pieces are inside the circle or shorter
    // than this, and with a limit of zero it never stops. Passing 0 there is what
    // made a drag on a benchy overflow the stack and take the window with it. The
    // value is the reference's: an eighth of the brush, capped at 0.05 mm.
    const float edgeLimit = std::min(brush.radius / 5.0f, 0.05f);
    const TriangleSelector::CursorType cursorType =
        brush.shape == BrushShape::Sphere ? TriangleSelector::CursorType::SPHERE
                                          : TriangleSelector::CursorType::CIRCLE;
    std::unique_ptr<TriangleSelector::Cursor> cursor =
        TriangleSelector::SinglePointCursor::cursor_factory(
            centre, camera, brush.radius, cursorType, identity, {}, edgeLimit);

    const int facet = facetHint >= 0 ? facetHint : selector.select_unsplit_triangle(centre, -1);
    if (facet < 0 || static_cast<size_t>(facet) >= mesh.its.indices.size())
        return 0;

    // Splitting on: the brush cuts the facets it touches instead of repainting the
    // whole triangle, which is the difference between painting a stripe and
    // painting every face the stripe crosses. It is what the reference passes.
    // The angle is the reference's: zero means "no overhang filter", and anything
    // else restricts the dab to facets whose normal lies within it of straight
    // down, judged in printer coordinates -- which is why the object's rotation,
    // not an identity, has to be the matrix the engine reads its down from.
    const float overhangAngle = brush.overhangOnly ? brush.highlightAngleDeg : 0.0f;
    selector.select_patch(facet, std::move(cursor), state, worldRotation, true, overhangAngle);

    data = selector.serialize();

    const int after = countInState(selector, state);
    if (qEnvironmentVariableIsSet("NEXT_LAYER_PAINT_TRACE"))
        std::fprintf(stderr, "paintSphere: centre=(%.2f %.2f %.2f) state=%d before=%d after=%d\n",
                     centre.x(), centre.y(), centre.z(), int(state), before, after);
    return std::abs(after - before);
}

Slic3r::Domain::indexed_triangle_set_with_color paintedTriangles(
    const Slic3r::Domain::TriangleMesh& mesh, const TriangleSplittingData& data, PaintKind kind)
{
    (void) kind;
    if (data.triangles_to_split.empty())
        return {};

    // The selector's own get_all_facets_with_colors() is not used here: it hands
    // back every leaf it holds, painted or not. On a 225k-facet benchy that is
    // 250k leaves and three quarters of a million vertices rebuilt for a brush
    // dab that touched a few thousand triangles, on every plate rebuild. Walking
    // the leaves and keeping the painted ones costs one pass and produces a mesh
    // proportional to what is actually painted.
    TriangleSelector selector(mesh);
    selector.deserialize(data);

    const TriangleSelector::Triangles& triangles = selector.triangles();
    const TriangleSelector::Vertices& vertices = selector.vertices();

    Slic3r::Domain::indexed_triangle_set_with_color out;
    std::vector<int> remap(vertices.size(), -1);
    for (const TriangleSelector::Triangle& triangle : triangles) {
        if (!triangle.valid() || triangle.is_split())
            continue;
        const TriangleStateType state = triangle.get_state();
        if (state == TriangleStateType::NONE)
            continue;
        Slic3r::Domain::Index3 indices{};
        for (int corner = 0; corner < 3; ++corner) {
            const int vertex = triangle.verts_idxs[corner];
            if (remap[vertex] < 0) {
                remap[vertex] = int(out.vertices.size());
                out.vertices.push_back(vertices[vertex].v);
            }
            indices[corner] = remap[vertex];
        }
        out.indices.push_back(indices);
        out.colors.push_back(static_cast<uint8_t>(state));
    }
    if (qEnvironmentVariableIsSet("NEXT_LAYER_PAINT_TRACE"))
        std::fprintf(stderr, "paintedTriangles: painted=%zu foliage=%zu\n",
                     out.indices.size(), triangles.size());
    return out;
}

}
