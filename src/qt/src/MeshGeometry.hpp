// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QByteArray>
#include <QQuick3DGeometry>
#include <QVector>
#include <QVector3D>
#include <QVector4D>

#include <Slic3r/Domain/TriangleMesh.hpp>

#include <array>
#include <cstdint>
#include <utility>

#include "next-layer-app/src/bridge.rs.h"

// The engine's mesh as the flat arrays the Rust builder takes. A `Vec3f` is
// Eigen's, so its coordinates are copied out one by one rather than the vector's
// storage being reinterpreted as `f32`.
namespace meshGeometryDetail {

inline rust::Vec<float> positionsOf(const Slic3r::Domain::TriangleMesh& mesh)
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

inline rust::Vec<uint32_t> indicesOf(const Slic3r::Domain::TriangleMesh& mesh)
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

inline rust::Vec<float> colourRecordOf(const QVector<QVector4D>& colours)
{
    rust::Vec<float> flat;
    flat.reserve(size_t(colours.size()) * 4);
    for (const QVector4D& colour : colours) {
        flat.push_back(colour.x());
        flat.push_back(colour.y());
        flat.push_back(colour.z());
        flat.push_back(colour.w());
    }
    return flat;
}

inline rust::Slice<const float> sliceOf(const rust::Vec<float>& values)
{
    return rust::Slice<const float>(values.data(), values.size());
}

inline rust::Slice<const uint32_t> sliceOf(const rust::Vec<uint32_t>& values)
{
    return rust::Slice<const uint32_t>(values.data(), values.size());
}

}

// Loaded STL as an interleaved position+normal mesh in printer coordinates:
// millimetres, Z-up, minimum Z at the bed plane, and the model's XY centre moved
// onto the bed centre. No scaling -- the bed grid, the toolpath and the model all
// share one space, and the camera fits itself to boundsMin/boundsMax.
//
// The layouts, the bounding box, the brush ring and the ray/triangle
// intersection are Rust's (`src/app/src/MeshGeometry.rs`); this class is the
// `QQuick3DGeometry` shell those buffers are handed to, and the one member Qt
// cannot hold for Rust -- the triangle corners a raycast answers against.
class MeshGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    Q_PROPERTY(QVector3D boundsMin READ boundsMin NOTIFY boundsChanged)
    Q_PROPERTY(QVector3D boundsMax READ boundsMax NOTIFY boundsChanged)

public:
    explicit MeshGeometry(QQuick3DObject* parent = nullptr)
        : QQuick3DGeometry(parent)
    {
    }

    void setMesh(const Slic3r::Domain::TriangleMesh& mesh)
    {
        const rust::Vec<float> positions = meshGeometryDetail::positionsOf(mesh);
        const rust::Vec<uint32_t> indices = meshGeometryDetail::indicesOf(mesh);
        next_layer::MeshGeometryFrame frame = next_layer::mesh_geometry_frame(
            meshGeometryDetail::sliceOf(positions), meshGeometryDetail::sliceOf(indices));
        m_positions = std::move(frame.positions);
        upload(frame);
        update();
        if (frame.vertices.empty())
            return;
        m_min = QVector3D(frame.low[0], frame.low[1], frame.low[2]);
        m_max = QVector3D(frame.high[0], frame.high[1], frame.high[2]);
        emit boundsChanged();
    }

    /// The same mesh with one colour per triangle, for the painted-facet overlay.
    /// Colours are straight RGBA in 0..1; a triangle with no colour is skipped, so
    /// an empty colour vector draws nothing rather than an unpainted copy of the
    /// object on top of itself.
    ///
    /// The raycast corners are left as they are, so a painted overlay does not
    /// change what the pointer answers against: this keeps the behaviour the C++
    /// class had, where a coloured mesh cleared the geometry buffer and left the
    /// raycast copy alone.
    void setColoredMesh(const Slic3r::Domain::TriangleMesh& mesh,
                        const QVector<QVector4D>& colors)
    {
        if (mesh.its.vertices.empty() || mesh.its.indices.empty()
            || colors.size() < qsizetype(mesh.its.indices.size())) {
            clear();
            update();
            return;
        }

        const rust::Vec<float> positions = meshGeometryDetail::positionsOf(mesh);
        const rust::Vec<uint32_t> indices = meshGeometryDetail::indicesOf(mesh);
        const rust::Vec<float> colours = meshGeometryDetail::colourRecordOf(colors);
        const next_layer::MeshGeometryFrame frame = next_layer::mesh_geometry_coloured_frame(
            meshGeometryDetail::sliceOf(positions), meshGeometryDetail::sliceOf(indices),
            meshGeometryDetail::sliceOf(colours));
        upload(frame);
        if (frame.vertices.empty())
            return;
        update();
    }

    /// A flat annulus in the plane perpendicular to a normal, for the brush
    /// cursor. Drawn with no lighting, so the shading of the surface under it does
    /// not change its apparent thickness.
    void setRing(const QVector3D& centre, const QVector3D& normal, float radius,
                 float thickness)
    {
        const next_layer::MeshGeometryFrame frame = next_layer::mesh_geometry_ring_frame(
            {centre.x(), centre.y(), centre.z()}, {normal.x(), normal.y(), normal.z()}, radius,
            thickness);
        m_positions.clear();
        upload(frame);
        update();
    }

    QVector3D boundsMin() const { return m_min; }
    QVector3D boundsMax() const { return m_max; }

    // Nearest triangle hit for a ray in printer coordinates. The returned w is
    // the ray parameter; a negative w means the ray missed the mesh.
    Q_INVOKABLE QVector4D raycast(const QVector3D& origin, const QVector3D& direction) const
    {
        const std::array<float, 4> hit = next_layer::mesh_geometry_raycast(
            rust::Slice<const float>(m_positions.data(), m_positions.size()),
            {origin.x(), origin.y(), origin.z()},
            {direction.x(), direction.y(), direction.z()});
        return QVector4D(hit[0], hit[1], hit[2], hit[3]);
    }

signals:
    void boundsChanged();

private:
    /// Hands one buffer to the renderer: the vertices, the stride and the
    /// attributes Rust laid them out in, and the bounds they cover. Whatever the
    /// geometry held is cleared first, and an empty buffer uploads nothing.
    void upload(const next_layer::MeshGeometryFrame& frame)
    {
        clear();
        if (frame.vertices.empty())
            return;

        setVertexData(QByteArray(reinterpret_cast<const char*>(frame.vertices.data()),
                                 int(frame.vertices.size())));
        setStride(int(frame.stride));
        addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0,
                     QQuick3DGeometry::Attribute::F32Type);
        addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 3 * int(sizeof(float)),
                     QQuick3DGeometry::Attribute::F32Type);
        if (frame.colour_offset >= 0)
            addAttribute(QQuick3DGeometry::Attribute::ColorSemantic, int(frame.colour_offset),
                         QQuick3DGeometry::Attribute::F32Type);
        setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
        setBounds(QVector3D(frame.low[0], frame.low[1], frame.low[2]),
                  QVector3D(frame.high[0], frame.high[1], frame.high[2]));
    }

    /// The triangle corners the raycast answers against, nine `f32` each. Written
    /// by `setMesh` and cleared by `setRing`; `setColoredMesh` deliberately does
    /// not touch it, which is the C++ behaviour this port reproduces: a ray
    /// answers against the last mesh that was set, not the painted overlay.
    rust::Vec<float> m_positions;
    QVector3D m_min;
    QVector3D m_max;
};
