// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include "next-layer-app/src/bridge.rs.h"

#include <Slic3r/Domain/Transformation.hpp>
#include <Slic3r/Domain/TriangleMesh.hpp>

#include <QVector>
#include <QVector3D>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace next_layer {

// The document's mesh is Rust's (src/app/src/mesh.rs). This is the handle the C++
// that is still here passes around: geometry never changes after import -- a move
// produces a new mesh -- so every pointer is const and undo shares one. The
// conversions below are the seam that is still open: the paint selector, the cut
// and the 3MF writer take the engine's mesh, and each of them is one step of
// `docs/rust-engine-plan.md` away from taking this one.

/// An owned Rust mesh. Not copyable: the box is the only owner.
struct AppMesh
{
    explicit AppMesh(rust::Box<Mesh> native) : native(std::move(native)) {}

    AppMesh(const AppMesh&) = delete;
    AppMesh& operator=(const AppMesh&) = delete;
    AppMesh(AppMesh&&) = delete;
    AppMesh& operator=(AppMesh&&) = delete;

    rust::Box<Mesh> native;
};

using MeshPtr = std::shared_ptr<const AppMesh>;

/// Twelve `f64`, row-major, three rows of four: an affine transform's linear part
/// and its translation.
using Affine = std::array<double, 12>;

inline const Mesh& meshOf(const MeshPtr& mesh)
{
    return *mesh->native;
}

Affine identityAffine();
Affine affineOf(const Slic3r::Domain::Transform3d& matrix);
Affine affineOf(const Slic3r::Domain::Transformation& trafo);

/// A mesh from the engine's, which is what the importer still produces.
MeshPtr meshFromEngine(const Slic3r::Domain::TriangleMesh& mesh);

/// A mesh from the flat arrays a project file or the engine's read-back hands over.
MeshPtr meshFromArrays(const QVector<double>& vertices, const QVector<uint32_t>& indices);

/// The engine's mesh, for the paint selector, the cut and the 3MF writer.
Slic3r::Domain::TriangleMesh engineMesh(const MeshPtr& mesh);

/// The bounding box, or false for a mesh with no vertices.
bool boundsOf(const MeshPtr& mesh, QVector3D& low, QVector3D& high);

MeshPtr translated(const MeshPtr& mesh, const QVector3D& offset);
MeshPtr transformed(const MeshPtr& mesh, const Affine& matrix);
MeshPtr appended(const MeshPtr& mesh, const MeshPtr& other);

double volumeOf(const MeshPtr& mesh, const Affine& matrix);
double surfaceAreaOf(const MeshPtr& mesh, const Affine& matrix);

/// One label per triangle; triangles that share a vertex share a label.
std::vector<uint32_t> shellLabels(const MeshPtr& mesh);

/// The triangles named by {@param faces}, renumbered onto their own vertices.
MeshPtr subsetOf(const MeshPtr& mesh, const std::vector<uint32_t>& faces);

}
