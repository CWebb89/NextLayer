// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <Slic3r/Domain/TriangleMesh.hpp>
#include <Slic3r/Domain/TriangleSelector.hpp>

#include <QColor>
#include <QVector>

#include <memory>

namespace Slic3r::Biz::Algorithms {
class TriangleSelector;
}

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

/// One object's paint state, kept selected-side up.
///
/// The engine's TriangleSelector *is* the split representation: building one
/// from a mesh and deserialising the stored bitstream costs a pass over every
/// facet and re-splits every painted triangle. That cost was paid three times
/// per brush dab -- once to paint, once to serialise back, once more to draw
/// the overlay -- which is why painting a large part crawled. A session keeps
/// the selector alive between dabs, so a dab costs the brush's own work and
/// nothing else; the plate's stored data is still written back after every dab,
/// because the slicer, undo and the project file all read it.
class PaintSession
{
public:
    using MeshPtr = std::shared_ptr<const Slic3r::Domain::TriangleMesh>;
    using TriangleSplittingData = Slic3r::Domain::TriangleSelector::TriangleSplittingData;

    PaintSession(MeshPtr mesh, const TriangleSplittingData& data);
    ~PaintSession();
    PaintSession(const PaintSession&) = delete;
    PaintSession& operator=(const PaintSession&) = delete;

    /// The mesh this session was built from. A session is only usable while the
    /// object still has this exact mesh: a cut, a split or an undo hands the
    /// object a different one, and painting into the old triangles would write
    /// paint onto geometry that no longer exists.
    const MeshPtr& mesh() const { return m_mesh; }

    /// Paints every facet within the brush of `centre` (mesh coordinates) and
    /// returns how many triangle pieces changed state. `camera` is the eye in the
    /// same mesh coordinates: the circle brush is projected from it, so what is
    /// painted is what the view can see. `worldRotation` is the object's rotation
    /// into printer coordinates, which is what tells the engine which way is down
    /// when the brush is restricted to overhangs -- a mesh is stored Y-up and the
    /// object carries the rotation that stands it up.
    int dab(const Slic3r::Domain::Vec3f& centre, const Slic3r::Domain::Vec3f& camera,
            const Brush& brush, PaintKind kind, PaintMode mode, int tool = 2,
            int facetHint = -1,
            const Slic3r::Domain::Transform3d& worldRotation
                = Slic3r::Domain::Transform3d::Identity());

    /// The current state in the engine's own serialised form, which is what a
    /// project file, undo and the slicer read.
    TriangleSplittingData data() const;

    /// The painted triangles of a kind, as an indexed triangle set in mesh
    /// coordinates, with one colour per state.
    Slic3r::Domain::indexed_triangle_set_with_color painted(PaintKind kind) const;

    /// The engine's own selector behind this session, for readers that need the
    /// split representation itself rather than a triangle soup.
    const Slic3r::Biz::Algorithms::TriangleSelector& selector() const;

    bool empty() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    MeshPtr m_mesh;
};

/// A single dab with no session behind it: builds one, paints, serialises, and
/// throws the selector away. Used where a stroke is not involved -- the headless
/// harnesses -- and by everything that needs the plain function.
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

/// One paint channel as a state per facet of \p mesh: 0 none, 1 enforcer, 2 blocker.
///
/// The engine's paint is finer than a facet: a brush splits the triangles it touches
/// and paints the pieces, so a facet can be half an enforcer and half nothing. HFW's
/// paint is one state per facet, so the pieces are reduced to the state covering the
/// largest area of their facet. That is a real loss of resolution and it is the reason
/// a painted edge lands on a facet boundary rather than inside a facet; the slicer has
/// no smaller unit to paint.
///
/// Empty when the channel was never painted.
QVector<quint8> paintFacetStates(
    const Slic3r::Domain::TriangleMesh& mesh,
    const Slic3r::Domain::TriangleSelector::TriangleSplittingData& data);

bool paintIsEmpty(const Slic3r::Domain::TriangleSelector::TriangleSplittingData& data);

/// Rebuilds one paint channel from one state per facet, the inverse of
/// `paintFacetStates`: 0 none, 1 enforcer, 2 blocker.
///
/// The states are what the engine holds and what a plate hands back, so this is how
/// a document recovers the paint the slicer will read. A facet is painted whole, at
/// the brush covering all of it, so the result is exactly the per-facet state and
/// never a subdivision. Returns how many facet pieces changed state.
int paintFromFacetStates(const Slic3r::Domain::TriangleMesh& mesh,
                        Slic3r::Domain::TriangleSelector::TriangleSplittingData& data,
                        const QVector<quint8>& states, PaintKind kind);

}
