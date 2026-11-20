// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "AppMesh.hpp"

#include "rust/cxx.h"

namespace next_layer {
namespace {

MeshPtr owned(rust::Box<Mesh> mesh)
{
    return std::make_shared<const AppMesh>(std::move(mesh));
}

rust::Slice<const float> floatsOf(const std::vector<float>& values)
{
    return values.empty() ? rust::Slice<const float>()
                          : rust::Slice<const float>(values.data(), values.size());
}

rust::Slice<const std::uint32_t> indicesOf(const std::vector<std::uint32_t>& values)
{
    return values.empty() ? rust::Slice<const std::uint32_t>()
                          : rust::Slice<const std::uint32_t>(values.data(), values.size());
}

rust::Slice<const double> affineValues(const Affine& matrix)
{
    return rust::Slice<const double>(matrix.data(), matrix.size());
}

MeshPtr make(const std::vector<float>& vertices, const std::vector<std::uint32_t>& indices)
{
    try {
        return owned(mesh_new(floatsOf(vertices), indicesOf(indices)));
    } catch (const rust::Error&) {
        return nullptr;
    }
}

}

Affine identityAffine()
{
    return {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0};
}

Affine affineOf(const Slic3r::Domain::Transform3d& matrix)
{
    const auto& values = matrix.matrix();
    Affine out{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column)
            out[size_t(row * 4 + column)] = values(row, column);
    }
    return out;
}

Affine affineOf(const Slic3r::Domain::Transformation& trafo)
{
    return affineOf(trafo.get_matrix());
}

MeshPtr meshFromEngine(const Slic3r::Domain::TriangleMesh& mesh)
{
    std::vector<float> vertices;
    vertices.reserve(mesh.its.vertices.size() * 3);
    for (const Slic3r::Domain::Vec3f& vertex : mesh.its.vertices) {
        vertices.push_back(vertex.x());
        vertices.push_back(vertex.y());
        vertices.push_back(vertex.z());
    }
    std::vector<std::uint32_t> indices;
    indices.reserve(mesh.its.indices.size() * 3);
    for (const Slic3r::Domain::Index3& face : mesh.its.indices) {
        indices.push_back(face[0]);
        indices.push_back(face[1]);
        indices.push_back(face[2]);
    }
    return make(vertices, indices);
}

MeshPtr meshFromArrays(const QVector<double>& vertices, const QVector<uint32_t>& indices)
{
    std::vector<float> corners;
    corners.reserve(size_t(vertices.size()));
    for (double value : vertices)
        corners.push_back(float(value));
    std::vector<std::uint32_t> faces;
    faces.reserve(size_t(indices.size()));
    for (uint32_t value : indices)
        faces.push_back(value);
    return make(corners, faces);
}

Slic3r::Domain::TriangleMesh engineMesh(const MeshPtr& mesh)
{
    Slic3r::Domain::TriangleMesh out;
    if (mesh == nullptr)
        return out;
    const rust::Slice<const float> vertices = mesh_vertices(meshOf(mesh));
    out.its.vertices.reserve(vertices.size() / 3);
    for (size_t index = 0; index + 2 < vertices.size(); index += 3) {
        out.its.vertices.emplace_back(vertices[index], vertices[index + 1], vertices[index + 2]);
    }
    const rust::Slice<const std::uint32_t> indices = mesh_indices(meshOf(mesh));
    out.its.indices.reserve(indices.size() / 3);
    for (size_t index = 0; index + 2 < indices.size(); index += 3) {
        out.its.indices.push_back(Slic3r::Domain::Index3{int(indices[index]),
                                                          int(indices[index + 1]),
                                                          int(indices[index + 2])});
    }
    return out;
}

bool boundsOf(const MeshPtr& mesh, QVector3D& low, QVector3D& high)
{
    if (mesh == nullptr || mesh_vertices(meshOf(mesh)).empty())
        return false;
    const std::array<float, 6> box = mesh_bounds(meshOf(mesh));
    low = QVector3D(box[0], box[1], box[2]);
    high = QVector3D(box[3], box[4], box[5]);
    return true;
}

MeshPtr translated(const MeshPtr& mesh, const QVector3D& offset)
{
    if (mesh == nullptr)
        return nullptr;
    return owned(mesh_translated(meshOf(mesh), {offset.x(), offset.y(), offset.z()}));
}

MeshPtr transformed(const MeshPtr& mesh, const Affine& matrix)
{
    if (mesh == nullptr)
        return nullptr;
    return owned(mesh_transformed(meshOf(mesh), affineValues(matrix)));
}

MeshPtr appended(const MeshPtr& mesh, const MeshPtr& other)
{
    if (mesh == nullptr)
        return other;
    if (other == nullptr)
        return mesh;
    return owned(mesh_appended(meshOf(mesh), meshOf(other)));
}

double volumeOf(const MeshPtr& mesh, const Affine& matrix)
{
    if (mesh == nullptr)
        return 0.0;
    return mesh_volume(meshOf(mesh), affineValues(matrix));
}

double surfaceAreaOf(const MeshPtr& mesh, const Affine& matrix)
{
    if (mesh == nullptr)
        return 0.0;
    return mesh_surface_area(meshOf(mesh), affineValues(matrix));
}

std::vector<uint32_t> shellLabels(const MeshPtr& mesh)
{
    if (mesh == nullptr)
        return {};
    const rust::Vec<std::uint32_t> labels = mesh_shell_labels(meshOf(mesh));
    return std::vector<uint32_t>(labels.begin(), labels.end());
}

MeshPtr subsetOf(const MeshPtr& mesh, const std::vector<uint32_t>& faces)
{
    if (mesh == nullptr)
        return nullptr;
    return owned(
        mesh_subset(meshOf(mesh), rust::Slice<const std::uint32_t>(faces.data(), faces.size())));
}

}
