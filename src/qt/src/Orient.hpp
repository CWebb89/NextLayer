// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <Slic3r/Domain/TriangleMesh.hpp>
#include <Slic3r/Domain/Types.hpp>

#include <cstdint>
#include <optional>

#include "next-layer-app/src/bridge.rs.h"

namespace next_layer {

// The scoring, the candidate list and the rotation construction are Rust's
// (`src/app/src/Orient.rs`). This header is the adapter onto them: it flattens the
// engine's mesh into the flat arrays the bridge carries and turns the sixteen `f64`
// that come back into a `Transform3d`, so the two spellings its callers already use
// keep working unchanged.

namespace orientDetail {

inline rust::Vec<float> vertexRecord(const Slic3r::Domain::TriangleMesh& mesh)
{
    rust::Vec<float> flat;
    flat.reserve(mesh.its.vertices.size() * 3);
    for (const Slic3r::Domain::Vec3f& vertex : mesh.its.vertices) {
        flat.push_back(vertex.x());
        flat.push_back(vertex.y());
        flat.push_back(vertex.z());
    }
    return flat;
}

inline rust::Vec<uint32_t> indexRecord(const Slic3r::Domain::TriangleMesh& mesh)
{
    rust::Vec<uint32_t> flat;
    flat.reserve(mesh.its.indices.size() * 3);
    for (const stl_triangle_vertex_indices& face : mesh.its.indices) {
        flat.push_back(uint32_t(face[0]));
        flat.push_back(uint32_t(face[1]));
        flat.push_back(uint32_t(face[2]));
    }
    return flat;
}

/// Sixteen `f64`, row-major, four rows of four: the shape a pose crosses in.
inline rust::Vec<double> matrixRecord(const Slic3r::Domain::Transform3d& matrix)
{
    rust::Vec<double> record;
    record.reserve(16);
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            record.push_back(matrix(row, column));
    return record;
}

inline rust::Slice<const float> sliceOf(const rust::Vec<float>& values)
{
    return rust::Slice<const float>(values.data(), values.size());
}

inline rust::Slice<const uint32_t> sliceOf(const rust::Vec<uint32_t>& values)
{
    return rust::Slice<const uint32_t>(values.data(), values.size());
}

inline rust::Slice<const double> sliceOf(const rust::Vec<double>& values)
{
    return rust::Slice<const double>(values.data(), values.size());
}

inline std::optional<Slic3r::Domain::Transform3d> poseOf(const rust::Vec<double>& record)
{
    if (record.size() != 16)
        return std::nullopt;
    Slic3r::Domain::Transform3d matrix = Slic3r::Domain::Transform3d::Identity();
    size_t at = 0;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            matrix(row, column) = record[at++];
    return matrix;
}

}

/// The rotation that puts the mesh on one of its own flat faces: the face's normal
/// is turned to point straight down and the object's own up-axis is preserved as far
/// as the face allows. A facet that is out of range, or whose normal is degenerate,
/// leaves the pose as it was.
inline Slic3r::Domain::Transform3d layFacetFlat(const Slic3r::Domain::TriangleMesh& mesh,
                                                const Slic3r::Domain::Transform3d& current,
                                                int facet)
{
    const rust::Vec<float> vertices = orientDetail::vertexRecord(mesh);
    const rust::Vec<uint32_t> indices = orientDetail::indexRecord(mesh);
    const rust::Vec<double> matrix = orientDetail::matrixRecord(current);
    const rust::Vec<double> rotated =
        next_layer::orient_lay_facet_flat(orientDetail::sliceOf(vertices),
                                          orientDetail::sliceOf(indices),
                                          orientDetail::sliceOf(matrix), facet);
    const std::optional<Slic3r::Domain::Transform3d> pose = orientDetail::poseOf(rotated);
    return pose.has_value() ? *pose : current;
}

/// The rotation that makes the object sit as low and as support-free as possible:
/// every distinct face normal is tried as a resting surface, and the orientation
/// scoring best on printed height plus overhanging area wins. Returns nothing when
/// the mesh has no faces.
inline std::optional<Slic3r::Domain::Transform3d> bestRestingRotation(
    const Slic3r::Domain::TriangleMesh& mesh,
    const Slic3r::Domain::Transform3d& current)
{
    const rust::Vec<float> vertices = orientDetail::vertexRecord(mesh);
    const rust::Vec<uint32_t> indices = orientDetail::indexRecord(mesh);
    const rust::Vec<double> matrix = orientDetail::matrixRecord(current);
    const rust::Vec<double> rotated =
        next_layer::orient_best_resting_rotation(orientDetail::sliceOf(vertices),
                                                 orientDetail::sliceOf(indices),
                                                 orientDetail::sliceOf(matrix));
    return orientDetail::poseOf(rotated);
}

}
