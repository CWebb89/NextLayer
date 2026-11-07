// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "PlateScene.hpp"

#include <QElapsedTimer>

#include <cstdio>

#include <Slic3r/Biz/Algorithms/TriangleMesh.hpp>

#include "Paint.hpp"

#include "MeshGeometry.hpp"
#include "PlateDocument.hpp"

#include <Slic3r/Domain/TriangleMesh.hpp>
#include <Slic3r/Domain/Types.hpp>

#include <cmath>
#include <limits>
#include <vector>

using Slic3r::Domain::Index3;
using Slic3r::Domain::Transform3d;
using Slic3r::Domain::TriangleMesh;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;

namespace {

Vec3d toVec3d(const QVector3D& value)
{
    return Vec3d(value.x(), value.y(), value.z());
}

QVector3D toVector(const Vec3d& value)
{
    return QVector3D(value.x(), value.y(), value.z());
}

void appendMesh(TriangleMesh& target, const TriangleMesh& source)
{
    const auto offset = static_cast<int>(target.its.vertices.size());
    target.its.vertices.insert(target.its.vertices.end(),
                               source.its.vertices.begin(), source.its.vertices.end());
    target.its.indices.reserve(target.its.indices.size() + source.its.indices.size());
    for (const Index3& face : source.its.indices)
        target.its.indices.push_back(Index3{face[0] + offset, face[1] + offset, face[2] + offset});
}

bool rayTriangle(const QVector3D& origin,
                 const QVector3D& direction,
                 const QVector3D& p0,
                 const QVector3D& p1,
                 const QVector3D& p2,
                 float& distance)
{
    const QVector3D edge1 = p1 - p0;
    const QVector3D edge2 = p2 - p0;
    const QVector3D pvec = QVector3D::crossProduct(direction, edge2);
    const double det = QVector3D::dotProduct(edge1, pvec);
    if (std::abs(det) < 1e-9)
        return false;

    const double inverse = 1.0 / det;
    const QVector3D tvec = origin - p0;
    const double u = QVector3D::dotProduct(tvec, pvec) * inverse;
    if (u < 0.0 || u > 1.0)
        return false;

    const QVector3D qvec = QVector3D::crossProduct(tvec, edge1);
    const double v = QVector3D::dotProduct(direction, qvec) * inverse;
    if (v < 0.0 || u + v > 1.0)
        return false;

    const double t = QVector3D::dotProduct(edge2, qvec) * inverse;
    if (t <= 1e-4)
        return false;

    distance = static_cast<float>(t);
    return true;
}

}

PlateScene::PlateScene(PlateDocument* document, QObject* parent)
    : QObject(parent)
    , m_document(document)
    , m_plate(new MeshGeometry(nullptr))
    , m_otherPlate(new MeshGeometry(nullptr))
    , m_hover(new MeshGeometry(nullptr))
    , m_selection(new MeshGeometry(nullptr))
    , m_paint(new MeshGeometry(nullptr))
    , m_volumes(new MeshGeometry(nullptr))
    , m_plateMarkers(new MeshGeometry(nullptr))
    , m_gizmo(new MeshGeometry(nullptr))
{
    m_plate->setParent(this);
    m_otherPlate->setParent(this);
    m_hover->setParent(this);
    m_selection->setParent(this);
    m_paint->setParent(this);
    m_volumes->setParent(this);
    m_plateMarkers->setParent(this);
    m_gizmo->setParent(this);

    rebuild();
    QObject::connect(document, &PlateDocument::objectsChanged, this, &PlateScene::rebuild);
    QObject::connect(document, &PlateDocument::geometryChanged, this, &PlateScene::rebuild);
    QObject::connect(document, &PlateDocument::selectionChanged, this, &PlateScene::rebuild);
}

QQuick3DGeometry* PlateScene::plateGeometry() const
{
    return m_plate;
}

QQuick3DGeometry* PlateScene::otherPlateGeometry() const
{
    return m_otherPlate;
}

QQuick3DGeometry* PlateScene::hoverGeometry() const
{
    return m_hover;
}

void PlateScene::setHoverFacet(int id, int facet)
{
    const PlateDocument::Object* object = m_document->object(id);
    if (object == nullptr || object->mesh == nullptr || facet < 0
        || size_t(facet) >= object->mesh->its.indices.size()) {
        if (!m_hasHover)
            return;
        m_hover->setColoredMesh(TriangleMesh{}, {});
        m_hasHover = false;
        m_hoverTriangles = 0;
        emit geometryChanged();
        return;
    }

    const TriangleMesh& mesh = *object->mesh;
    const Index3& seed = mesh.its.indices[size_t(facet)];
    const Vec3f& sa = mesh.its.vertices[seed[0]];
    const Vec3f& sb = mesh.its.vertices[seed[1]];
    const Vec3f& sc = mesh.its.vertices[seed[2]];
    const Vec3f normal = (sb - sa).cross(sc - sa).normalized();
    const float plane = normal.dot(sa);

    // A flat face is several triangles, so the highlight has to be the whole face:
    // marking one triangle of a square reads as a broken highlight, not as a face.
    // Flood out through shared vertices, keeping every triangle in the same plane.
    std::vector<std::vector<size_t>> vertexFacets(mesh.its.vertices.size());
    for (size_t index = 0; index < mesh.its.indices.size(); ++index) {
        for (const int vertex : mesh.its.indices[index])
            vertexFacets[size_t(vertex)].push_back(index);
    }

    std::vector<bool> visited(mesh.its.indices.size(), false);
    std::vector<size_t> pending{size_t(facet)};
    std::vector<size_t> chosen;
    visited[size_t(facet)] = true;
    while (!pending.empty()) {
        const size_t current = pending.back();
        pending.pop_back();
        chosen.push_back(current);
        for (const int vertex : mesh.its.indices[current]) {
            for (const size_t neighbour : vertexFacets[size_t(vertex)]) {
                if (visited[neighbour])
                    continue;
                const Index3& tri = mesh.its.indices[neighbour];
                const Vec3f& a = mesh.its.vertices[tri[0]];
                const Vec3f& b = mesh.its.vertices[tri[1]];
                const Vec3f& c = mesh.its.vertices[tri[2]];
                const Vec3f faceNormal = (b - a).cross(c - a);
                if (faceNormal.norm() <= 0.0f)
                    continue;
                const Vec3f unit = faceNormal.normalized();
                if (unit.dot(normal) < 0.9995f || std::abs(unit.dot(a) - plane) > 0.05f)
                    continue;
                visited[neighbour] = true;
                pending.push_back(neighbour);
            }
        }
    }

    TriangleMesh patch;
    patch.its.indices.reserve(chosen.size());
    QVector<QVector4D> colours;
    colours.reserve(int(chosen.size()));
    for (const size_t index : chosen) {
        const Index3& face = mesh.its.indices[index];
        const auto base = static_cast<int>(patch.its.vertices.size());
        patch.its.vertices.push_back(mesh.its.vertices[face[0]]);
        patch.its.vertices.push_back(mesh.its.vertices[face[1]]);
        patch.its.vertices.push_back(mesh.its.vertices[face[2]]);
        patch.its.indices.push_back(Index3{base, base + 1, base + 2});
        colours.append(QVector4D(0.18f, 0.74f, 0.99f, 1.0f));
    }
    // Lifted off the surface by scaling a hair about the object's own centre, the
    // way the paint overlay does: the face's stored winding may point either way,
    // and a normal-shaped lift disappears inside the model whenever it does.
    const Vec3f centre((object->localMin.x() + object->localMax.x()) * 0.5f,
                       (object->localMin.y() + object->localMax.y()) * 0.5f,
                       (object->localMin.z() + object->localMax.z()) * 0.5f);
    for (Vec3f& vertex : patch.its.vertices)
        vertex = centre + (vertex - centre) * 1.004f;
    patch.transform(object->trafo.get_matrix());

    m_hover->setColoredMesh(patch, colours);
    m_hasHover = true;
    m_hoverTriangles = int(chosen.size());
    emit geometryChanged();
}

QQuick3DGeometry* PlateScene::selectionGeometry() const
{
    return m_selection;
}

QQuick3DGeometry* PlateScene::paintGeometry() const
{
    return m_paint;
}

QQuick3DGeometry* PlateScene::volumeGeometry() const
{
    return m_volumes;
}

QQuick3DGeometry* PlateScene::gizmoGeometry() const
{
    return m_gizmo;
}

namespace {

// The point on an infinite line that comes closest to a ray. A drag along an axis
// is the difference of two of these, which is why the handle follows the pointer
// instead of sliding away from it.
QVector3D closestOnLine(const QVector3D& lineOrigin, const QVector3D& lineDirection,
                        const QVector3D& rayOrigin, const QVector3D& rayDirection)
{
    const QVector3D offset = lineOrigin - rayOrigin;
    const float a = QVector3D::dotProduct(lineDirection, lineDirection);
    const float b = QVector3D::dotProduct(lineDirection, rayDirection);
    const float c = QVector3D::dotProduct(rayDirection, rayDirection);
    const float d = QVector3D::dotProduct(lineDirection, offset);
    const float e = QVector3D::dotProduct(rayDirection, offset);
    const float denominator = a * c - b * b;
    if (std::abs(denominator) < 1e-6f)
        return lineOrigin;
    return lineOrigin + lineDirection * ((b * e - c * d) / denominator);
}

// One arrow: a square shaft with a pyramid head. Drawn as geometry rather than as
// an overlay so it is lit and occluded like everything else in the scene.
void appendArrow(TriangleMesh& mesh, const QVector3D& from, const QVector3D& to, double width)
{
    const QVector3D along = (to - from).normalized();
    const QVector3D helper = std::abs(along.z()) > 0.9f ? QVector3D(1, 0, 0) : QVector3D(0, 0, 1);
    const QVector3D side = QVector3D::crossProduct(along, helper).normalized() * float(width * 0.5);
    const QVector3D up = QVector3D::crossProduct(along, side).normalized() * float(width * 0.5);

    const QVector3D shaftEnd = from + (to - from) * 0.78f;
    const QVector3D corners[4] = {side + up, up - side, -side - up, side - up};

    auto push = [&mesh](const QVector3D& point) {
        mesh.its.vertices.push_back(Slic3r::Domain::Vec3f(point.x(), point.y(), point.z()));
        return int(mesh.its.vertices.size()) - 1;
    };

    int base[4];
    int tip[4];
    for (int i = 0; i < 4; ++i) {
        base[i] = push(from + corners[i]);
        tip[i] = push(shaftEnd + corners[i]);
    }
    for (int i = 0; i < 4; ++i) {
        const int next = (i + 1) % 4;
        mesh.its.indices.push_back(Index3{base[i], base[next], tip[next]});
        mesh.its.indices.push_back(Index3{base[i], tip[next], tip[i]});
    }

    const int apex = push(to);
    for (int i = 0; i < 4; ++i)
        mesh.its.indices.push_back(Index3{tip[i], tip[(i + 1) % 4], apex});
}

}

float PlateScene::gizmoReach() const
{
    const PlateDocument::Object* object = m_document->object(m_document->selectedId());
    if (object == nullptr)
        return 12.0f;
    const QVector3D size = object->size();
    return std::clamp(std::max({size.x(), size.y(), size.z()}) * 0.6f, 12.0f, 60.0f);
}

// Where an axis arrow starts: just outside the object on that axis, so the handle
// is never buried inside the solid it moves.
QVector3D PlateScene::gizmoAxisStart(int axis) const
{
    const PlateDocument::Object* object = m_document->object(m_document->selectedId());
    const QVector3D directions[3] = {QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)};
    if (object == nullptr || axis < 0 || axis > 2)
        return m_gizmoOrigin;
    const QVector3D size = object->size();
    const float half = axis == 0 ? size.x() * 0.5f
                     : axis == 1 ? size.y() * 0.5f
                                 : size.z() * 0.5f;
    return m_gizmoOrigin + directions[axis] * (half * 1.06f + 1.0f);
}

void PlateScene::rebuildGizmo()
{
    TriangleMesh mesh;
    QVector<QVector4D> colours;
    m_hasGizmo = false;

    const PlateDocument::Object* object = m_document->selectedCount() == 1
                                              ? m_document->object(m_document->selectedId())
                                              : nullptr;
    if (object == nullptr || !object->visible) {
        m_gizmo->setColoredMesh(mesh, colours);
        return;
    }

    const QVector3D size = object->size();
    m_gizmoOrigin = object->position() + QVector3D(0.0f, 0.0f, size.z() * 0.5f);

    const float reach = gizmoReach();
    const float width = reach * 0.07f;
    const QVector4D axisColours[3] = {QVector4D(0.90f, 0.35f, 0.32f, 1.0f),
                                      QVector4D(0.36f, 0.85f, 0.42f, 1.0f),
                                      QVector4D(0.36f, 0.62f, 0.95f, 1.0f)};
    const QVector3D directions[3] = {QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)};

    for (int axis = 0; axis < 3; ++axis) {
        const QVector3D start = gizmoAxisStart(axis);
        const int before = int(mesh.its.indices.size());
        appendArrow(mesh, start, start + directions[axis] * reach, width);
        for (int i = before; i < int(mesh.its.indices.size()); ++i)
            colours.append(axisColours[axis]);
    }

    m_hasGizmo = true;
    m_gizmo->setColoredMesh(mesh, colours);
}

int PlateScene::pickGizmo(const QVector3D& origin, const QVector3D& direction)
{
    if (!m_hasGizmo)
        return -1;

    const float reach = gizmoReach();
    const QVector3D directions[3] = {QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)};

    int best = -1;
    float bestDistance = std::max(reach * 0.16f, 3.0f);
    for (int axis = 0; axis < 3; ++axis) {
        const QVector3D start = gizmoAxisStart(axis);
        const QVector3D point = closestOnLine(start, directions[axis], origin, direction);
        const float travel = QVector3D::dotProduct(point - start, directions[axis]);
        if (travel < -0.5f || travel > reach)
            continue;
        const QVector3D toPoint = point - origin;
        const float distance =
            (toPoint - direction * QVector3D::dotProduct(toPoint, direction)).length();
        if (distance < bestDistance) {
            bestDistance = distance;
            best = axis;
        }
    }
    return best;
}

QVector3D PlateScene::gizmoAxisPoint(int axis, const QVector3D& origin,
                                     const QVector3D& direction)
{
    const QVector3D directions[3] = {QVector3D(1, 0, 0), QVector3D(0, 1, 0), QVector3D(0, 0, 1)};
    if (axis < 0 || axis > 2)
        return m_gizmoOrigin;
    return closestOnLine(m_gizmoOrigin, directions[axis], origin, direction);
}

QQuick3DGeometry* PlateScene::bedMarkerGeometry() const
{
    return m_plateMarkers;
}

bool PlateScene::hasSelection() const
{
    return m_hasSelection;
}

void PlateScene::rebuild()
{
    QElapsedTimer timer;
    timer.start();
    TriangleMesh plate;
    TriangleMesh otherPlates;
    TriangleMesh selection;

    // Every plate is drawn, not just the active one: a project with three plates
    // shows three beds in the project view. Each plate's objects
    // are shifted by that plate's offset in the scene. The other plates are a
    // separate mesh so they can be drawn dimmed: the one you are working on is
    // the one that stands out, and only it is sliced.
    for (const int id : m_document->ids()) {
        const PlateDocument::Object* object = m_document->object(id);
        if (object == nullptr || !object->visible)
            continue;
        TriangleMesh world = m_document->worldMesh(id);
        if (m_document->isSelected(id))
            appendMesh(selection, world);
        else
            appendMesh(plate, world);
    }

    for (int other = 0; other < m_document->plateCount(); ++other) {
        if (other == m_document->activePlate())
            continue;
        const QVector3D offset = m_document->plateOffset(other);
        for (const int id : m_document->idsOnPlate(other)) {
            const PlateDocument::Object* object = m_document->object(id);
            if (object == nullptr || !object->visible)
                continue;
            TriangleMesh world = m_document->worldMesh(id);
            world.translate(Slic3r::Domain::Vec3f(offset.x(), offset.y(), offset.z()));
            appendMesh(otherPlates, world);
        }
    }

    m_hasOtherPlates = !otherPlates.its.indices.empty();
    m_hasSelection = !selection.its.indices.empty();
    m_plate->setMesh(plate);
    m_otherPlate->setMesh(otherPlates);
    m_selection->setMesh(selection);

    // Modifier volumes of every visible object, in world coordinates and tinted
    // by role: red subtracts, amber modifies, purple blocks support and green
    // forces it.
    TriangleMesh volumeMesh;
    QVector<QVector4D> volumeColors;
    m_hasVolumes = false;
    for (const int id : m_document->ids()) {
        const PlateDocument::Object* owner = m_document->object(id);
        if (owner == nullptr || !owner->visible)
            continue;
        for (const PlateDocument::Volume& volume : owner->volumes) {
            if (!volume.visible || volume.mesh == nullptr)
                continue;
            QVector4D colour;
            switch (volume.type) {
            case PlateDocument::VolumeType::NEGATIVE_VOLUME:
                colour = QVector4D(0.92f, 0.28f, 0.24f, 0.45f);
                break;
            case PlateDocument::VolumeType::PARAMETER_MODIFIER:
                colour = QVector4D(0.98f, 0.72f, 0.25f, 0.40f);
                break;
            case PlateDocument::VolumeType::SUPPORT_BLOCKER:
                colour = QVector4D(0.62f, 0.36f, 0.92f, 0.40f);
                break;
            case PlateDocument::VolumeType::SUPPORT_ENFORCER:
                colour = QVector4D(0.32f, 0.85f, 0.42f, 0.40f);
                break;
            default:
                continue;
            }
            if (m_document->isSelected(id))
                m_hasVolumes = true;
            TriangleMesh world = *volume.mesh;
            world.transform(volume.worldMatrix(owner->trafo));
            const int base = int(volumeMesh.its.vertices.size());
            volumeMesh.its.vertices.insert(volumeMesh.its.vertices.end(),
                                           world.its.vertices.begin(),
                                           world.its.vertices.end());
            for (const Index3& face : world.its.indices) {
                volumeMesh.its.indices.push_back(
                    Index3{face[0] + base, face[1] + base, face[2] + base});
                volumeColors.append(colour);
            }
        }
    }
    m_volumes->setColoredMesh(volumeMesh, volumeColors);

    // One thin slab per plate so the gaps between plates read as beds rather
    // than as empty space. Only worth drawing when there is more than one.
    TriangleMesh markers;
    if (m_document->plateCount() > 1) {
        const QVector3D size = m_document->plateSize();
        for (int other = 0; other < m_document->plateCount(); ++other) {
            const QVector3D offset = m_document->plateOffset(other);
            TriangleMesh slab =
                Slic3r::Biz::Algorithms::TriangleMesh::make_cube(size.x(), size.y(), 0.6);
            slab.translate(Slic3r::Domain::Vec3f(offset.x(), offset.y(), -0.6f));
            appendMesh(markers, slab);
        }
    }
    m_plateMarkers->setMesh(markers);

    rebuildGizmo();

    // The painted facets of the selected object, in the object's own frame with one
    // colour per paint kind: support enforcers and blockers, seam, and each
    // multi-material state.
    TriangleMesh painted;
    QVector<QVector4D> paintedColors;
    const int selected = m_document->selectedId();
    const PlateDocument::Object* object =
        selected > 0 ? m_document->object(selected) : nullptr;
    if (object != nullptr && object->mesh != nullptr && object->visible) {
        const auto addPaint = [&](const Slic3r::Domain::TriangleSelector::TriangleSplittingData& data,
                                  next_layer::PaintKind kind, const QVector4D& paintColor,
                                  const QVector4D& eraseColor) {
            if (next_layer::paintIsEmpty(data))
                return;
            const Slic3r::Domain::indexed_triangle_set_with_color colored =
                next_layer::paintedTriangles(*object->mesh, data, kind);
            for (size_t index = 0; index < colored.indices.size(); ++index) {
                const auto& face = colored.indices[index];
                const int base = int(painted.its.vertices.size());
                painted.its.vertices.push_back(colored.vertices[face[0]]);
                painted.its.vertices.push_back(colored.vertices[face[1]]);
                painted.its.vertices.push_back(colored.vertices[face[2]]);
                painted.its.indices.push_back(Index3{base, base + 1, base + 2});
                const uint8_t state = index < colored.colors.size() ? colored.colors[index] : 0;
                const bool blocker =
                    state == uint8_t(Slic3r::Domain::TriangleSelector::TriangleStateType::BLOCKER);
                paintedColors.append(blocker ? eraseColor : paintColor);
            }
        };

        addPaint(object->supportPaint, next_layer::PaintKind::Supports,
                 QVector4D(0.95f, 0.55f, 0.10f, 1.0f), QVector4D(0.25f, 0.55f, 0.95f, 1.0f));
        addPaint(object->seamPaint, next_layer::PaintKind::Seam,
                 QVector4D(0.85f, 0.20f, 0.85f, 1.0f), QVector4D(0.85f, 0.20f, 0.85f, 1.0f));
        // Tool paint is the one kind whose colour per facet is meaningful: each
        // tool has its own colour, so the overlay reads as the printed result.
        const auto addToolPaint = [&]() {
            if (next_layer::paintIsEmpty(object->toolPaint))
                return;
            const Slic3r::Domain::indexed_triangle_set_with_color colored =
                next_layer::paintedTriangles(*object->mesh, object->toolPaint,
                                             next_layer::PaintKind::Tool);
            for (size_t index = 0; index < colored.indices.size(); ++index) {
                const auto& face = colored.indices[index];
                const int base = int(painted.its.vertices.size());
                painted.its.vertices.push_back(colored.vertices[face[0]]);
                painted.its.vertices.push_back(colored.vertices[face[1]]);
                painted.its.vertices.push_back(colored.vertices[face[2]]);
                painted.its.indices.push_back(Index3{base, base + 1, base + 2});
                const int tool = index < colored.colors.size() ? int(colored.colors[index]) : 1;
                const QColor colour = m_document->toolColour(tool);
                paintedColors.append(QVector4D(float(colour.redF()), float(colour.greenF()),
                                               float(colour.blueF()), 1.0f));
            }
        };
        addToolPaint();

        if (!painted.its.indices.empty())
            painted.transform(object->trafo.get_matrix());
    }
    m_paint->setColoredMesh(painted, paintedColors);

    if (qEnvironmentVariableIsSet("NEXT_LAYER_DEBUG_PRESETS"))
        std::fprintf(stderr, "rebuild: %lld ms, %lld objects\n",
                     static_cast<long long>(timer.elapsed()),
                     static_cast<long long>(m_document->ids().size()));

    emit geometryChanged();
}

QVector4D PlateScene::pick(const QVector3D& origin, const QVector3D& direction)
{
    const QVector3D worldDirection = direction.normalized();

    double nearest = std::numeric_limits<double>::max();
    QVector3D hit;
    int picked = 0;
    int pickedFacet = -1;

    for (const int id : m_document->ids()) {
        const PlateDocument::Object* object = m_document->object(id);
        if (object == nullptr || !object->visible || object->mesh == nullptr)
            continue;

        const TriangleMesh& mesh = *object->mesh;
        const Transform3d matrix = object->trafo.get_matrix();
        const Transform3d inverse = matrix.inverse();
        const Vec3d localOrigin = inverse * toVec3d(origin);
        const Vec3d localDirection = inverse.linear() * toVec3d(worldDirection);

        const QVector3D rayOrigin = toVector(localOrigin);
        const QVector3D rayDirection = toVector(localDirection).normalized();

        float best = std::numeric_limits<float>::max();
        bool any = false;
        int bestFacet = -1;
        int facetIndex = -1;
        for (const Index3& face : mesh.its.indices) {
            ++facetIndex;
            const Vec3f& a = mesh.its.vertices[face[0]];
            const Vec3f& b = mesh.its.vertices[face[1]];
            const Vec3f& c = mesh.its.vertices[face[2]];
            float distance = 0.0f;
            if (rayTriangle(rayOrigin, rayDirection,
                            QVector3D(a.x(), a.y(), a.z()),
                            QVector3D(b.x(), b.y(), b.z()),
                            QVector3D(c.x(), c.y(), c.z()),
                            distance) && distance < best) {
                best = distance;
                bestFacet = facetIndex;
                any = true;
            }
        }
        if (!any)
            continue;

        const Vec3d localHit = localOrigin + localDirection.normalized() * double(best);
        const Vec3d worldHit = matrix * localHit;
        const double distance = (worldHit - toVec3d(origin)).norm();
        if (distance < nearest) {
            nearest = distance;
            hit = toVector(worldHit);
            picked = id;
            pickedFacet = bestFacet;
        }
    }

    if (picked == 0)
        return QVector4D(0.0f, 0.0f, 0.0f, -1.0f);

    if (picked != m_pickedId || pickedFacet != m_pickedFacet) {
        m_pickedId = picked;
        m_pickedFacet = pickedFacet;
        emit pickedIdChanged();
    }
    return QVector4D(hit.x(), hit.y(), hit.z(), static_cast<float>(nearest));
}
