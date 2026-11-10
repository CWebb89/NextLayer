// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "MeshGeometry.hpp"

#include <Slic3r/Domain/TriangleMesh.hpp>

#include <array>
#include <cmath>

MeshGeometry::MeshGeometry(QQuick3DObject* parent)
    : QQuick3DGeometry(parent)
{
}

void MeshGeometry::setMesh(const Slic3r::Domain::TriangleMesh& mesh)
{
    const indexed_triangle_set& its = mesh.its;
    if (its.vertices.empty() || its.indices.empty()) {
        clear();
        m_positions.clear();
        update();
        return;
    }

    Slic3r::Domain::Vec3f low = its.vertices.front();
    Slic3r::Domain::Vec3f high = its.vertices.front();
    for (const Slic3r::Domain::Vec3f& vertex : its.vertices) {
        low = low.cwiseMin(vertex);
        high = high.cwiseMax(vertex);
    }

    const Slic3r::Domain::Vec3f offset(0.0f, 0.0f, 0.0f);

    const int floats_per_vertex = 6;
    QVector<float> vertices;
    vertices.reserve(int(its.indices.size()) * 3 * floats_per_vertex);

    for (const stl_triangle_vertex_indices& face : its.indices) {
        const Slic3r::Domain::Vec3f p0 = its.vertices[face[0]] + offset;
        const Slic3r::Domain::Vec3f p1 = its.vertices[face[1]] + offset;
        const Slic3r::Domain::Vec3f p2 = its.vertices[face[2]] + offset;
        Slic3r::Domain::Vec3f normal = (p1 - p0).cross(p2 - p0);
        const float length = normal.norm();
        if (length == 0.0f)
            continue;
        normal /= length;

        for (const Slic3r::Domain::Vec3f& position : {p0, p1, p2}) {
            vertices.append(position.x());
            vertices.append(position.y());
            vertices.append(position.z());
            vertices.append(normal.x());
            vertices.append(normal.y());
            vertices.append(normal.z());
        }
    }

    clear();
    m_positions.clear();
    if (vertices.isEmpty())
        return;

    m_positions.reserve(vertices.size() / floats_per_vertex * 3);
    for (int index = 0; index + floats_per_vertex <= vertices.size(); index += floats_per_vertex) {
        m_positions.append(vertices.at(index));
        m_positions.append(vertices.at(index + 1));
        m_positions.append(vertices.at(index + 2));
    }

    const QByteArray vertex_data(reinterpret_cast<const char*>(vertices.constData()),
                                 vertices.size() * int(sizeof(float)));
    setVertexData(vertex_data);
    setStride(floats_per_vertex * int(sizeof(float)));
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 3 * int(sizeof(float)), QQuick3DGeometry::Attribute::F32Type);
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    m_min = QVector3D(low.x() + offset.x(), low.y() + offset.y(), low.z() + offset.z());
    m_max = QVector3D(high.x() + offset.x(), high.y() + offset.y(), high.z() + offset.z());
    setBounds(m_min, m_max);
    update();
    emit boundsChanged();
}

void MeshGeometry::setRing(const QVector3D& centre, const QVector3D& normal, float radius,
                            float thickness)
{
    const int segments = 64;
    const int floats_per_vertex = 6;
    QVector3D up = QVector3D::crossProduct(normal, QVector3D(0.0f, 0.0f, 1.0f));
    if (up.lengthSquared() < 1e-4f)
        up = QVector3D::crossProduct(normal, QVector3D(1.0f, 0.0f, 0.0f));
    up.normalize();
    const QVector3D right = QVector3D::crossProduct(normal, up).normalized();
    const QVector3D lift = normal.normalized() * 0.06f;
    const float inner = qMax(radius - thickness, radius * 0.5f);

    const auto point_at = [&](float angle, float distance) {
        return centre + lift + right * (std::cos(angle) * distance)
                        + up * (std::sin(angle) * distance);
    };

    QVector<float> vertices;
    vertices.reserve(segments * 6 * floats_per_vertex);
    for (int segment = 0; segment < segments; ++segment) {
        const float a0 = float(segment) / float(segments) * 2.0f * float(M_PI);
        const float a1 = float(segment + 1) / float(segments) * 2.0f * float(M_PI);
        const std::array<QVector3D, 4> quad = {point_at(a0, inner), point_at(a0, radius),
                                               point_at(a1, radius), point_at(a1, inner)};
        for (const int corner : {0, 1, 2, 0, 2, 3}) {
            const QVector3D& position = quad[size_t(corner)];
            vertices.append(position.x());
            vertices.append(position.y());
            vertices.append(position.z());
            vertices.append(normal.x());
            vertices.append(normal.y());
            vertices.append(normal.z());
        }
    }

    clear();
    m_positions.clear();
    const QByteArray vertex_data(reinterpret_cast<const char*>(vertices.constData()),
                                 vertices.size() * int(sizeof(float)));
    setVertexData(vertex_data);
    setStride(floats_per_vertex * int(sizeof(float)));
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 3 * int(sizeof(float)), QQuick3DGeometry::Attribute::F32Type);
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    setBounds(centre - QVector3D(radius, radius, radius), centre + QVector3D(radius, radius, radius));
    update();
}

void MeshGeometry::setColoredMesh(const Slic3r::Domain::TriangleMesh& mesh,
                                  const QVector<QVector4D>& colors)
{
    const indexed_triangle_set& its = mesh.its;
    const int faceCount = int(its.indices.size());
    if (its.vertices.empty() || faceCount == 0 || colors.size() < faceCount) {
        clear();
        update();
        return;
    }

    const int floats_per_vertex = 10;
    QVector<float> vertices;
    vertices.reserve(faceCount * 3 * floats_per_vertex);

    Slic3r::Domain::Vec3f low = its.vertices.front();
    Slic3r::Domain::Vec3f high = its.vertices.front();
    for (const Slic3r::Domain::Vec3f& vertex : its.vertices) {
        low = low.cwiseMin(vertex);
        high = high.cwiseMax(vertex);
    }

    for (int facet = 0; facet < faceCount; ++facet) {
        const QVector4D& color = colors.at(facet);
        if (color.w() <= 0.0f)
            continue;
        const stl_triangle_vertex_indices& face = its.indices[size_t(facet)];
        const Slic3r::Domain::Vec3f p0 = its.vertices[face[0]];
        const Slic3r::Domain::Vec3f p1 = its.vertices[face[1]];
        const Slic3r::Domain::Vec3f p2 = its.vertices[face[2]];
        Slic3r::Domain::Vec3f normal = (p1 - p0).cross(p2 - p0);
        const float length = normal.norm();
        if (length == 0.0f)
            continue;
        normal /= length;

        for (const Slic3r::Domain::Vec3f& position : {p0, p1, p2}) {
            vertices.append(position.x());
            vertices.append(position.y());
            vertices.append(position.z());
            vertices.append(normal.x());
            vertices.append(normal.y());
            vertices.append(normal.z());
            vertices.append(color.x());
            vertices.append(color.y());
            vertices.append(color.z());
            vertices.append(color.w());
        }
    }

    clear();
    if (vertices.isEmpty())
        return;

    const QByteArray vertex_data(reinterpret_cast<const char*>(vertices.constData()),
                                 vertices.size() * int(sizeof(float)));
    setVertexData(vertex_data);
    setStride(floats_per_vertex * int(sizeof(float)));
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0,
                 QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 3 * int(sizeof(float)),
                 QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::ColorSemantic, 6 * int(sizeof(float)),
                 QQuick3DGeometry::Attribute::F32Type);
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    setBounds(QVector3D(low.x(), low.y(), low.z()), QVector3D(high.x(), high.y(), high.z()));
    update();
}

QVector4D MeshGeometry::raycast(const QVector3D& origin, const QVector3D& direction) const
{
    const QVector3D dir = direction.normalized();
    float nearest = -1.0f;
    QVector3D hit;

    for (int index = 0; index + 9 <= m_positions.size(); index += 9) {
        const QVector3D p0(m_positions.at(index), m_positions.at(index + 1), m_positions.at(index + 2));
        const QVector3D p1(m_positions.at(index + 3), m_positions.at(index + 4), m_positions.at(index + 5));
        const QVector3D p2(m_positions.at(index + 6), m_positions.at(index + 7), m_positions.at(index + 8));

        const QVector3D edge1 = p1 - p0;
        const QVector3D edge2 = p2 - p0;
        const QVector3D pvec = QVector3D::crossProduct(dir, edge2);
        const float det = QVector3D::dotProduct(edge1, pvec);
        if (std::abs(det) < 1e-9f)
            continue;

        const float invDet = 1.0f / det;
        const QVector3D tvec = origin - p0;
        const float u = QVector3D::dotProduct(tvec, pvec) * invDet;
        if (u < 0.0f || u > 1.0f)
            continue;

        const QVector3D qvec = QVector3D::crossProduct(tvec, edge1);
        const float v = QVector3D::dotProduct(dir, qvec) * invDet;
        if (v < 0.0f || u + v > 1.0f)
            continue;

        const float t = QVector3D::dotProduct(edge2, qvec) * invDet;
        if (t > 1e-4f && (nearest < 0.0f || t < nearest)) {
            nearest = t;
            hit = origin + dir * t;
        }
    }

    return QVector4D(hit.x(), hit.y(), hit.z(), nearest);
}
