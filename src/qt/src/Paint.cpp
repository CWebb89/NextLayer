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

/// The painted leaves of a selector, as a triangle set in the mesh's own
/// coordinates. The selector's own get_all_facets_with_colors() hands back every
/// leaf it holds, painted or not; on a 225k-facet benchy that is a quarter of a
/// million leaves rebuilt to draw a few thousand painted triangles. Walking the
/// leaves and keeping the painted ones produces a mesh proportional to what is
/// actually painted.
Slic3r::Domain::indexed_triangle_set_with_color paintedFrom(const TriangleSelector& selector)
{
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
    return out;
}

}

struct PaintSession::Impl
{
    explicit Impl(MeshPtr source, const TriangleSplittingData& data)
        : mesh(std::move(source))
        , selector(*mesh)
    {
        if (!data.triangles_to_split.empty())
            selector.deserialize(data);
    }

    MeshPtr mesh;
    TriangleSelector selector;
};

PaintSession::PaintSession(MeshPtr mesh, const TriangleSplittingData& data)
    : m_impl(std::make_unique<Impl>(mesh, data))
    , m_mesh(m_impl->mesh)
{
}

PaintSession::~PaintSession() = default;

int PaintSession::dab(const Slic3r::Domain::Vec3f& centre,
                      const Slic3r::Domain::Vec3f& camera, const Brush& brush, PaintKind kind,
                      PaintMode mode, int tool, int facetHint,
                      const Slic3r::Domain::Transform3d& worldRotation)
{
    TriangleSelector& selector = m_impl->selector;
    if (brush.radius <= 0.0f || selector.triangles().empty())
        return 0;

    const TriangleStateType state = stateFor(kind, mode, tool);
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

    const int facet = facetHint >= 0 ? facetHint
                                     : selector.select_unsplit_triangle(centre, -1);
    if (facet < 0 || size_t(facet) >= m_impl->mesh->its.indices.size())
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

    const int after = countInState(selector, state);
    if (qEnvironmentVariableIsSet("NEXT_LAYER_PAINT_TRACE"))
        std::fprintf(stderr, "dab: centre=(%.2f %.2f %.2f) state=%d before=%d after=%d\n",
                     centre.x(), centre.y(), centre.z(), int(state), before, after);
    return std::abs(after - before);
}

PaintSession::TriangleSplittingData PaintSession::data() const
{
    return m_impl->selector.serialize();
}

Slic3r::Domain::indexed_triangle_set_with_color PaintSession::painted(PaintKind kind) const
{
    (void) kind;
    return paintedFrom(m_impl->selector);
}

bool PaintSession::empty() const
{
    return m_impl->selector.triangles().empty();
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

    PaintSession session(std::make_shared<const Slic3r::Domain::TriangleMesh>(mesh), data);
    const int changed = session.dab(centre, camera, brush, kind, mode, tool, facetHint,
                                    worldRotation);
    if (changed > 0)
        data = session.data();
    return changed;
}

Slic3r::Domain::indexed_triangle_set_with_color paintedTriangles(
    const Slic3r::Domain::TriangleMesh& mesh, const TriangleSplittingData& data, PaintKind kind)
{
    (void) kind;
    if (data.triangles_to_split.empty())
        return {};

    PaintSession session(std::make_shared<const Slic3r::Domain::TriangleMesh>(mesh), data);
    return session.painted(kind);
}

}
