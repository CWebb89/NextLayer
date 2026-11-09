// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "PlateDocument.hpp"

#include "ConfigBoxJson.hpp"
#include "ForeignSettings.hpp"
#include "Scratch.hpp"
#include "ThreeMfProject.hpp"

#include "Arranger.hpp"
#include "GcodeBrand.hpp"
#include "ProjectFile.hpp"
#include "Paint.hpp"
#include "Orient.hpp"

#include "Cut.hpp"
#include "Hollow.hpp"
#include "Text.hpp"

#include <Slic3r/Biz/Algorithms/ModelVolume.hpp>
#include <Slic3r/Biz/Algorithms/TriangleMesh.hpp>
#include <Slic3r/Biz/FileLoadingLogic.hpp>
#include <Slic3r/Biz/Format/STL.hpp>
#include <Slic3r/Domain/ConfigBoxesFDM.hpp>
#include <Slic3r/Domain/FacetsAnnotation.hpp>
#include <Slic3r/Domain/Model.hpp>
#include <Slic3r/Domain/ModelInstance.hpp>
#include <Slic3r/Domain/ModelObject.hpp>
#include <Slic3r/Domain/Types.hpp>

#include <nlohmann/json.hpp>

#include <QCryptographicHash>
#include <QByteArrayView>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUrl>
#include <QtMath>

#include <algorithm>
#include <functional>
#include <unordered_map>

using Slic3r::Domain::Transform3d;
using Slic3r::Domain::TriangleMesh;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Vec3f;

namespace {

constexpr double ObjectGap = 5.0;

QString toLocalPath(const QString& path)
{
    return path.startsWith(QStringLiteral("file:")) ? QUrl(path).toLocalFile() : path;
}

QVector3D toVector(const Vec3f& value)
{
    return QVector3D(value.x(), value.y(), value.z());
}

Vec3f toVec3f(const QVector3D& value)
{
    return Vec3f(value.x(), value.y(), value.z());
}

bool meshBounds(const TriangleMesh& mesh, QVector3D& low, QVector3D& high)
{
    if (mesh.its.vertices.empty())
        return false;
    Vec3f lo = mesh.its.vertices.front();
    Vec3f hi = mesh.its.vertices.front();
    for (const Vec3f& vertex : mesh.its.vertices) {
        lo = lo.cwiseMin(vertex);
        hi = hi.cwiseMax(vertex);
    }
    low = toVector(lo);
    high = toVector(hi);
    return true;
}

void normalise(TriangleMesh& mesh, const QVector3D& low, const QVector3D& high)
{
    mesh.translate(toVec3f(QVector3D(-(low.x() + high.x()) * 0.5f,
                                     -(low.y() + high.y()) * 0.5f,
                                     -low.z())));
}

bool boxOf(const TriangleMesh& mesh,
           const Slic3r::Domain::Transformation& trafo,
           const QVector3D& low,
           const QVector3D& high,
           QVector3D& outMin,
           QVector3D& outMax)
{
    const Transform3d matrix = trafo.get_matrix();
    const Vec3d corners[8] = {
        Vec3d(low.x(), low.y(), low.z()),
        Vec3d(high.x(), low.y(), low.z()),
        Vec3d(low.x(), high.y(), low.z()),
        Vec3d(high.x(), high.y(), low.z()),
        Vec3d(low.x(), low.y(), high.z()),
        Vec3d(high.x(), low.y(), high.z()),
        Vec3d(low.x(), high.y(), high.z()),
        Vec3d(high.x(), high.y(), high.z()),
    };
    Vec3d lo = matrix * corners[0];
    Vec3d hi = lo;
    for (int i = 1; i < 8; ++i) {
        const Vec3d corner = matrix * corners[i];
        lo = lo.cwiseMin(corner);
        hi = hi.cwiseMax(corner);
    }
    outMin = QVector3D(lo.x(), lo.y(), lo.z());
    outMax = QVector3D(hi.x(), hi.y(), hi.z());
    return true;
}

void appendMesh(TriangleMesh& target, const TriangleMesh& source)
{
    const auto offset = static_cast<int>(target.its.vertices.size());
    target.its.vertices.insert(target.its.vertices.end(),
                               source.its.vertices.begin(), source.its.vertices.end());
    target.its.indices.reserve(target.its.indices.size() + source.its.indices.size());
    for (const auto& face : source.its.indices) {
        target.its.indices.push_back(
            Slic3r::Domain::Index3{face[0] + offset, face[1] + offset, face[2] + offset});
    }
}


struct MeshMeasures
{
    double volume = 0.0;
    double area = 0.0;
    int facets = 0;
};

// Signed tetrahedra against the origin give the volume a closed mesh encloses,
// and every triangle contributes half the cross product of its edges to the
// surface area. Both are measured on the transformed mesh: the number the user
// reads is the printed one, not the file's.
MeshMeasures measure(const TriangleMesh& mesh, const Slic3r::Domain::Transform3d& matrix)
{
    MeshMeasures out;
    out.facets = static_cast<int>(mesh.its.indices.size());

    for (const auto& face : mesh.its.indices) {
        const Vec3d a = matrix * mesh.its.vertices[face[0]].cast<double>();
        const Vec3d b = matrix * mesh.its.vertices[face[1]].cast<double>();
        const Vec3d c = matrix * mesh.its.vertices[face[2]].cast<double>();
        out.volume += a.dot(b.cross(c)) / 6.0;
        out.area += (b - a).cross(c - a).norm() / 2.0;
    }
    out.volume = std::abs(out.volume);
    return out;
}

// Faces belong to the same shell when they share a vertex. Union-find over the
// vertex table, then one bucket per root, is the whole of a part split.
std::vector<std::vector<int>> shellsOf(const TriangleMesh& mesh)
{
    const auto& faces = mesh.its.indices;
    std::vector<int> parent(faces.size());
    for (size_t i = 0; i < parent.size(); ++i)
        parent[i] = static_cast<int>(i);

    std::function<int(int)> find = [&parent, &find](int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    auto unite = [&parent, &find](int a, int b) {
        const int ra = find(a);
        const int rb = find(b);
        if (ra != rb)
            parent[rb] = ra;
    };

    std::unordered_map<int, int> firstFaceOfVertex;
    for (size_t face = 0; face < faces.size(); ++face) {
        for (int corner = 0; corner < 3; ++corner) {
            const int vertex = faces[face][corner];
            const auto inserted = firstFaceOfVertex.emplace(vertex, static_cast<int>(face));
            if (!inserted.second)
                unite(inserted.first->second, static_cast<int>(face));
        }
    }

    std::unordered_map<int, int> shellOfRoot;
    std::vector<std::vector<int>> shells;
    for (size_t face = 0; face < faces.size(); ++face) {
        const int root = find(static_cast<int>(face));
        const auto inserted = shellOfRoot.emplace(root, static_cast<int>(shells.size()));
        if (inserted.second)
            shells.emplace_back();
        shells[inserted.first->second].push_back(static_cast<int>(face));
    }
    return shells;
}

TriangleMesh subsetOf(const TriangleMesh& mesh, const std::vector<int>& faces)
{
    TriangleMesh out;
    std::unordered_map<int, int> remap;
    for (const int face : faces) {
        Slic3r::Domain::Index3 indices{};
        for (int corner = 0; corner < 3; ++corner) {
            const int vertex = mesh.its.indices[face][corner];
            const auto inserted = remap.emplace(vertex, static_cast<int>(out.its.vertices.size()));
            if (inserted.second)
                out.its.vertices.push_back(mesh.its.vertices[vertex]);
            indices[corner] = inserted.first->second;
        }
        out.its.indices.push_back(indices);
    }
    return out;
}

}

QVector3D PlateDocument::Object::position() const
{
    const Vec3d offset = trafo.get_offset();
    return QVector3D(offset.x(), offset.y(), offset.z());
}

QVector3D PlateDocument::Object::rotation() const
{
    // The engine stores rotation in radians; the plate, the panel and every
    // other surface speaks degrees.
    const Vec3d rotation = trafo.get_rotation();
    return QVector3D(qRadiansToDegrees(rotation.x()),
                     qRadiansToDegrees(rotation.y()),
                     qRadiansToDegrees(rotation.z()));
}

QVector3D PlateDocument::Object::scale() const
{
    const Vec3d factor = trafo.get_scaling_factor();
    return QVector3D(factor.x(), factor.y(), factor.z());
}

QVector3D PlateDocument::Object::size() const
{
    QVector3D low;
    QVector3D high;
    if (mesh == nullptr || !boxOf(*mesh, trafo, localMin, localMax, low, high))
        return QVector3D();
    return high - low;
}

PlateDocument::PlateDocument(QVector2D bedCentre, QVector2D bedSize, QObject* parent)
    : QObject(parent)
    , m_bedCentre(bedCentre)
    , m_bedSize(bedSize)
{
}

PlateDocument::~PlateDocument() = default;

void PlateDocument::setBed(QVector2D centre, QVector2D size)
{
    m_bedCentre = centre;
    m_bedSize = size;
}

QString PlateDocument::displayNameOf(int id) const
{
    const Object* object = find(id);
    return object == nullptr ? QStringLiteral("object") : object->displayName();
}

void PlateDocument::changed()
{
    ++m_version;
    if (!m_modified) {
        m_modified = true;
        emit modifiedChanged();
    }
    emit objectsChanged();
    emit undoChanged();
}

PlateDocument::Object* PlateDocument::find(int id)
{
    for (Object& object : m_objects) {
        if (object.id == id)
            return &object;
    }
    return nullptr;
}

const PlateDocument::Object* PlateDocument::find(int id) const
{
    for (const Object& object : m_objects) {
        if (object.id == id)
            return &object;
    }
    return nullptr;
}

QVector<int> PlateDocument::ids() const
{
    QVector<int> result;
    result.reserve(static_cast<int>(m_objects.size()));
    for (const Object& object : m_objects) {
        if (object.plate != m_activePlate)
            continue;
        result.append(object.id);
    }
    return result;
}

QVariantMap PlateDocument::objectInfo(int id) const
{
    const Object* object = find(id);
    if (object == nullptr)
        return QVariantMap();

    const QVector3D low = objectBoundsMin(id);
    const QVector3D high = objectBoundsMax(id);
    const QVector3D position = object->position();
    const QVector3D rotation = object->rotation();
    const QVector3D scale = object->scale();

    QVariantMap info;
    info.insert(QStringLiteral("id"), object->id);
    info.insert(QStringLiteral("name"), object->displayName());
    info.insert(QStringLiteral("source"), object->path);
    info.insert(QStringLiteral("visible"), object->visible);
    info.insert(QStringLiteral("selected"), isSelected(object->id));
    info.insert(QStringLiteral("x"), position.x());
    info.insert(QStringLiteral("y"), position.y());
    info.insert(QStringLiteral("z"), position.z());
    info.insert(QStringLiteral("rotation"), rotation.z());
    info.insert(QStringLiteral("scale"), scale.x());
    info.insert(QStringLiteral("width"), high.x() - low.x());
    info.insert(QStringLiteral("depth"), high.y() - low.y());
    info.insert(QStringLiteral("height"), high.z() - low.z());
    // World bounds, so a rubber-band selection can project an object without
    // guessing at its centre from a position that is the object's origin.
    info.insert(QStringLiteral("min"), QVariantList{low.x(), low.y(), low.z()});
    info.insert(QStringLiteral("max"), QVariantList{high.x(), high.y(), high.z()});
    info.insert(QStringLiteral("volumes"), volumes(object->id));
    return info;
}

QVariantMap PlateDocument::objectStatistics(int id) const
{
    const Object* object = find(id);
    if (object == nullptr || object->mesh == nullptr)
        return QVariantMap();

    const MeshMeasures measures = measure(*object->mesh, object->trafo.get_matrix());
    const QVector3D low = objectBoundsMin(id);
    const QVector3D high = objectBoundsMax(id);

    QVariantMap info;
    info.insert(QStringLiteral("id"), object->id);
    info.insert(QStringLiteral("name"), object->displayName());
    info.insert(QStringLiteral("volume"), measures.volume);
    info.insert(QStringLiteral("volume_cm3"), measures.volume / 1000.0);
    info.insert(QStringLiteral("area"), measures.area);
    info.insert(QStringLiteral("area_cm2"), measures.area / 100.0);
    info.insert(QStringLiteral("facets"), measures.facets);
    info.insert(QStringLiteral("width"), high.x() - low.x());
    info.insert(QStringLiteral("depth"), high.y() - low.y());
    info.insert(QStringLiteral("height"), high.z() - low.z());
    info.insert(QStringLiteral("min_x"), low.x());
    info.insert(QStringLiteral("min_y"), low.y());
    info.insert(QStringLiteral("min_z"), low.z());
    info.insert(QStringLiteral("max_x"), high.x());
    info.insert(QStringLiteral("max_y"), high.y());
    info.insert(QStringLiteral("max_z"), high.z());
    info.insert(QStringLiteral("volumes"), static_cast<int>(object->volumes.size()));
    return info;
}

QVariantMap PlateDocument::plateStatistics(int plate) const
{
    double volume = 0.0;
    int facets = 0;
    int objects = 0;
    for (const Object& object : m_objects) {
        if (object.plate != plate || object.mesh == nullptr)
            continue;
        const MeshMeasures measures = measure(*object.mesh, object.trafo.get_matrix());
        volume += measures.volume;
        facets += measures.facets;
        ++objects;
    }

    QVariantMap info;
    info.insert(QStringLiteral("plate"), plate);
    info.insert(QStringLiteral("objects"), objects);
    info.insert(QStringLiteral("volume"), volume);
    info.insert(QStringLiteral("volume_cm3"), volume / 1000.0);
    info.insert(QStringLiteral("facets"), facets);
    return info;
}

bool PlateDocument::mergeSelection()
{
    return mergeObjects(QVector<int>(m_selectedIds.begin(), m_selectedIds.end()));
}

bool PlateDocument::mergeObjects(const QVector<int>& ids)
{
    if (ids.size() < 2)
        return false;

    Object* target = find(ids.front());
    if (target == nullptr || target->mesh == nullptr)
        return false;

    TriangleMesh merged;
    for (const int id : ids) {
        const Object* object = find(id);
        if (object == nullptr || object->mesh == nullptr)
            continue;
        TriangleMesh baked = *object->mesh;
        const Transform3d matrix = object->trafo.get_matrix();
        for (Vec3f& vertex : baked.its.vertices)
            vertex = (matrix * vertex.cast<double>()).cast<float>();
        appendMesh(merged, baked);
    }

    if (merged.its.indices.empty())
        return false;

    pushUndo(QStringLiteral("Merge %1 objects").arg(ids.size()));

    target = find(ids.front());
    target->mesh = std::make_shared<TriangleMesh>(std::move(merged));
    target->trafo = Slic3r::Domain::Transformation();
    meshBounds(*target->mesh, target->localMin, target->localMax);

    for (const int id : ids) {
        if (id == ids.front())
            continue;
        m_objects.erase(std::remove_if(m_objects.begin(), m_objects.end(),
                                       [id](const Object& object) { return object.id == id; }),
                        m_objects.end());
    }

    m_selectedIds = {ids.front()};
    m_error.clear();
    emit errorChanged();
    emit selectionChanged();
    changed();
    emit geometryChanged();
    return true;
}

int PlateDocument::splitToParts(int id)
{
    if (refuseOnLockedPlate())
        return 0;
    Object* object = find(id);
    if (object == nullptr || object->mesh == nullptr)
        return 0;

    const std::vector<std::vector<int>> shells = shellsOf(*object->mesh);
    if (shells.size() < 2)
        return 0;

    // Faces are grouped by connectivity, but a shell's vertices are its own
    // extent, not the whole file's: each piece is re-centred on the object's own
    // frame so it does not jump when the split happens.
    const Vec3d objectOffset = object->trafo.get_offset();
    const Vec3d objectRotation = object->trafo.get_rotation();
    const Vec3d objectScale = object->trafo.get_scaling_factor();

    std::vector<Object> pieces;
    for (size_t index = 1; index < shells.size(); ++index) {
        Object piece;
        piece.id = m_nextId++;
        piece.plate = object->plate;
        piece.path = object->path;
        piece.name = object->name;
        piece.label = QStringLiteral("%1 part %2")
                          .arg(object->displayName())
                          .arg(int(index) + 1);
        piece.mesh = std::make_shared<TriangleMesh>(subsetOf(*object->mesh, shells[index]));
        meshBounds(*piece.mesh, piece.localMin, piece.localMax);
        piece.trafo.set_offset(objectOffset);
        piece.trafo.set_rotation(objectRotation);
        piece.trafo.set_scaling_factor(objectScale);
        piece.visible = object->visible;
        pieces.push_back(std::move(piece));
    }

    pushUndo(QStringLiteral("%1: split into %2 parts")
                 .arg(object->displayName())
                 .arg(int(shells.size())));

    object = find(id);
    object->mesh = std::make_shared<TriangleMesh>(subsetOf(*object->mesh, shells.front()));
    meshBounds(*object->mesh, object->localMin, object->localMax);

    for (Object& piece : pieces)
        m_objects.push_back(std::move(piece));

    m_selectedIds.clear();
    for (const Object& stored : m_objects) {
        if (stored.plate == m_activePlate)
            m_selectedIds.push_back(stored.id);
    }

    m_error.clear();
    emit errorChanged();
    emit selectionChanged();
    changed();
    emit geometryChanged();
    return static_cast<int>(shells.size()) - 1;
}

QVariantList PlateDocument::fonts() const
{
    QVariantList list;
    for (const next_layer::TextFont& font : next_layer::textFonts()) {
        QVariantMap entry;
        entry.insert(QStringLiteral("family"), QString::fromStdString(font.family));
        entry.insert(QStringLiteral("path"), QString::fromStdString(font.path));
        list.append(entry);
    }
    return list;
}

int PlateDocument::addText(const QString& text, double height, double depth,
                           const QString& fontPath)
{
    TriangleMesh mesh;
    std::string error;
    if (!next_layer::textMesh(text.toStdString(), height, depth, fontPath.toStdString(), mesh,
                              error)) {
        fail(QString::fromStdString(error));
        return -1;
    }

    Object object;
    object.id = m_nextId++;
    object.plate = m_activePlate;
    object.name = QStringLiteral("text");
    object.label = text.trimmed().left(24);
    object.mesh = std::make_shared<TriangleMesh>(std::move(mesh));
    meshBounds(*object.mesh, object.localMin, object.localMax);
    placeNext(object);

    pushUndo(QStringLiteral("Add text %1").arg(object.label));
    m_objects.push_back(std::move(object));
    m_selectedIds = {m_objects.back().id};
    m_error.clear();
    emit errorChanged();
    emit selectionChanged();
    changed();
    emit geometryChanged();
    return m_objects.back().id;
}

int PlateDocument::cutAtZ(int id, double z, bool keepUpper)
{
    Object* object = find(id);
    if (object == nullptr || object->mesh == nullptr)
        return 0;

    // The plane arrives in printer coordinates; the mesh is in the object's own
    // frame, so the height is rotated back through the object's transform.
    const Transform3d matrix = object->trafo.get_matrix();
    const Vec3d local = matrix.inverse() * Vec3d(0.0, 0.0, z);

    next_layer::CutHalves halves;
    if (!next_layer::cutMesh(*object->mesh, local.z(), halves)) {
        fail(QStringLiteral("%1: nothing to cut at %2 mm").arg(object->displayName()).arg(z, 0, 'f', 1));
        return 1;
    }

    pushUndo(QStringLiteral("%1: cut at %2 mm").arg(object->displayName()).arg(z, 0, 'f', 1));

    TriangleMesh kept = keepUpper ? std::move(halves.upper) : std::move(halves.lower);
    TriangleMesh other = keepUpper ? std::move(halves.lower) : std::move(halves.upper);

    Object* target = find(id);
    target->mesh = std::make_shared<TriangleMesh>(std::move(kept));
    meshBounds(*target->mesh, target->localMin, target->localMax);

    Object piece;
    piece.id = m_nextId++;
    piece.plate = target->plate;
    piece.path = target->path;
    piece.name = target->name;
    piece.label = QStringLiteral("%1 %2").arg(target->displayName(),
                                              keepUpper ? QStringLiteral("lower")
                                                        : QStringLiteral("upper"));
    piece.mesh = std::make_shared<TriangleMesh>(std::move(other));
    meshBounds(*piece.mesh, piece.localMin, piece.localMax);
    piece.trafo = target->trafo;
    piece.visible = target->visible;

    // Off the bed, so the two halves are two objects rather than two coincident
    // solids the user has to drag apart.
    const Vec3d offset = piece.trafo.get_offset();
    piece.trafo.set_offset(Vec3d(offset.x() + objectBoundsMax(id).x() - objectBoundsMin(id).x() + 5.0,
                                 offset.y(), offset.z()));
    m_objects.push_back(std::move(piece));

    m_selectedIds = {id, m_objects.back().id};
    m_error.clear();
    emit errorChanged();
    emit selectionChanged();
    changed();
    emit geometryChanged();
    return 2;
}

bool PlateDocument::trimToBand(int id, double bottom, double top)
{
    Object* object = find(id);
    if (object == nullptr || object->mesh == nullptr || top <= bottom)
        return false;

    next_layer::CutHalves halves;
    if (!next_layer::cutMesh(*object->mesh, top, halves) || halves.lower.its.indices.empty())
        return false;

    next_layer::CutHalves band;
    const bool cut_at_bottom = next_layer::cutMesh(halves.lower, bottom, band);
    Slic3r::Domain::TriangleMesh slab = cut_at_bottom ? std::move(band.upper)
                                                     : std::move(halves.lower);
    if (slab.its.indices.empty())
        return false;

    // The band keeps the tower's own height coordinates, so it would sit in the air
    // where the tower had it. Moving the vertices down is what puts it on the bed.
    for (Slic3r::Domain::Vec3f& vertex : slab.its.vertices)
        vertex.z() -= static_cast<float>(bottom);

    pushUndo(QStringLiteral("%1: trimmed to %2-%3 mm")
                 .arg(object->displayName())
                 .arg(bottom, 0, 'f', 1)
                 .arg(top, 0, 'f', 1));

    object->mesh = std::make_shared<Slic3r::Domain::TriangleMesh>(std::move(slab));
    meshBounds(*object->mesh, object->localMin, object->localMax);
    m_error.clear();
    emit errorChanged();
    changed();
    emit geometryChanged();
    return true;
}

bool PlateDocument::hollowObject(int id, double thickness)
{
    Object* object = find(id);
    if (object == nullptr || object->mesh == nullptr)
        return false;

    const double wall = std::clamp(thickness, 0.2, 20.0);

    TriangleMesh hollowed = *object->mesh;
    if (!next_layer::hollowMesh(hollowed, wall)) {
        fail(QStringLiteral("%1: hollowing failed at %2 mm")
                 .arg(object->displayName())
                 .arg(wall, 0, 'f', 1));
        return false;
    }

    pushUndo(QStringLiteral("%1: hollow %2 mm").arg(object->displayName()).arg(wall, 0, 'f', 1));
    object = find(id);
    object->mesh = std::make_shared<TriangleMesh>(std::move(hollowed));
    meshBounds(*object->mesh, object->localMin, object->localMax);
    m_error.clear();
    emit errorChanged();
    changed();
    emit geometryChanged();
    return true;
}

QVariantList PlateDocument::toolColours() const
{
    QVariantList list;
    list.reserve(m_toolColours.size());
    for (const QColor& colour : m_toolColours)
        list.append(colour.name(QColor::HexRgb));
    return list;
}

void PlateDocument::setToolColours(const QVariantList& colours)
{
    QVector<QColor> next;
    next.reserve(colours.size());
    for (const QVariant& entry : colours) {
        const QColor colour(entry.toString());
        next.append(colour.isValid() ? colour : QColor(QStringLiteral("#808080")));
    }
    if (next.isEmpty())
        next.append(QColor(QStringLiteral("#808080")));
    if (next == m_toolColours)
        return;
    m_toolColours = std::move(next);
    emit toolsChanged();
    emit geometryChanged();
}

QColor PlateDocument::toolColour(int tool) const
{
    if (m_toolColours.isEmpty())
        return QColor(QStringLiteral("#808080"));
    const int index = std::clamp(tool - 1, 0, int(m_toolColours.size()) - 1);
    return m_toolColours.at(index);
}

int PlateDocument::count() const
{
    int total = 0;
    for (const Object& object : m_objects)
        total += object.plate == m_activePlate ? 1 : 0;
    return total;
}

QVariantList PlateDocument::objects() const
{
    QVariantList list;
    list.reserve(static_cast<int>(m_objects.size()));
    for (const Object& object : m_objects) {
        if (object.plate != m_activePlate)
            continue;
        list.append(objectInfo(object.id));
    }
    return list;
}

int PlateDocument::objectsOnPlate(int plate) const
{
    int count = 0;
    for (const Object& object : m_objects)
        count += object.plate == plate ? 1 : 0;
    return count;
}

QVector<int> PlateDocument::idsOnPlate(int plate) const
{
    QVector<int> result;
    for (const Object& object : m_objects) {
        if (object.plate == plate)
            result.append(object.id);
    }
    return result;
}

bool PlateDocument::plateLocked(int plate) const
{
    return plate >= 0 && plate < m_plateLocked.size() && m_plateLocked[plate];
}

void PlateDocument::setPlateLocked(int plate, bool locked)
{
    if (plate < 0 || plate >= plateCount())
        return;
    while (m_plateLocked.size() < plateCount())
        m_plateLocked.append(false);
    if (m_plateLocked[plate] == locked)
        return;
    m_plateLocked[plate] = locked;
    m_error.clear();
    // A lock is part of the document, so it is undoable and it dirties the file.
    pushUndo(locked ? QStringLiteral("Lock plate %1").arg(plate + 1)
                    : QStringLiteral("Unlock plate %1").arg(plate + 1));
    m_modified = true;
    emit plateChanged();
}

QVariantList PlateDocument::plateList() const
{
    QVariantList result;
    for (int i = 0; i < plateCount(); ++i) {
        const QVector3D offset = plateOffset(i);
        QVariantMap entry;
        entry.insert(QStringLiteral("index"), i);
        entry.insert(QStringLiteral("name"), plateName(i));
        entry.insert(QStringLiteral("locked"), plateLocked(i));
        entry.insert(QStringLiteral("active"), i == m_activePlate);
        entry.insert(QStringLiteral("count"), objectsOnPlate(i));
        entry.insert(QStringLiteral("x"), double(offset.x()));
        entry.insert(QStringLiteral("y"), double(offset.y()));
        result.append(entry);
    }
    return result;
}

QString PlateDocument::plateName(int plate) const
{
    if (plate < 0 || plate >= m_plateNames.size() || m_plateNames[plate].isEmpty())
        return QStringLiteral("Plate %1").arg(plate + 1);
    return m_plateNames[plate];
}

void PlateDocument::setPlateName(int plate, const QString& name)
{
    if (plate < 0 || plate >= plateCount())
        return;
    while (m_plateNames.size() < plateCount())
        m_plateNames.append(QString());
    const QString trimmed = name.trimmed();
    m_plateNames[plate] = trimmed.startsWith(QStringLiteral("Plate "), Qt::CaseInsensitive)
                              ? QString() : trimmed;
    m_modified = true;
    emit plateChanged();
}

void PlateDocument::setActivePlate(int plate)
{
    if (plate < 0 || plate == m_activePlate)
        return;
    m_activePlate = plate;
    m_selectedIds.clear();
    emit plateChanged();
    emit objectsChanged();
    emit selectionChanged();
    emit geometryChanged();
}

QVector3D PlateDocument::plateOffset(int plate) const
{
    // Four plates to a row, the way a multi-plate project is laid out in the
    // plate list of every slicer worth copying: a long line of beds runs off the
    // screen at the fifth plate and makes "which plate is where" unguessable.
    // Offsets are relative to the active plate, which always sits at the origin
    // where the camera is.
    constexpr int columns = 4;
    const float stepX = m_bedSize.x() + 40.0f;
    const float stepY = m_bedSize.y() + 40.0f;
    const int column = plate % columns - m_activePlate % columns;
    const int row = plate / columns - m_activePlate / columns;
    return QVector3D(stepX * float(column), stepY * float(row), 0.0f);
}

int PlateDocument::plateAt(const QVector3D& point) const
{
    for (int plate = 0; plate < plateCount(); ++plate) {
        const QVector3D centre = plateOffset(plate);
        if (std::abs(point.x() - centre.x()) <= m_bedSize.x() / 2.0f
            && std::abs(point.y() - centre.y()) <= m_bedSize.y() / 2.0f)
            return plate;
    }
    return -1;
}

int PlateDocument::addPlate()
{
    const int plate = plateCount();
    pushUndo(QStringLiteral("Add plate %1").arg(plate + 1));
    ++m_plateCount;
    while (m_plateLocked.size() < m_plateCount)
        m_plateLocked.append(false);
    while (m_plateNames.size() < m_plateCount)
        m_plateNames.append(QString());
    m_activePlate = plate;
    m_selectedIds.clear();
    emit plateChanged();
    emit objectsChanged();
    emit selectionChanged();
    emit geometryChanged();
    return plate;
}

int PlateDocument::duplicatePlate(int plate)
{
    const int target = plateCount();
    pushUndo(QStringLiteral("Duplicate plate %1").arg(plate + 1));
    std::vector<Object> copies;
    for (const Object& object : m_objects) {
        if (object.plate != plate)
            continue;
        Object copy = object;
        copy.id = m_nextId++;
        for (Volume& volume : copy.volumes)
            volume.id = m_nextId++;
        copy.plate = target;
        copies.push_back(std::move(copy));
    }
    for (Object& copy : copies)
        m_objects.push_back(std::move(copy));
    m_activePlate = target;
    m_selectedIds.clear();
    if (!m_objects.empty()) {
        for (const Object& object : m_objects) {
            if (object.plate == target) {
                m_selectedIds = {object.id};
                break;
            }
        }
    }
    emit plateChanged();
    changed();
    emit selectionChanged();
    emit geometryChanged();
    return target;
}

void PlateDocument::clearPlate(int plate)
{
    if (plateLocked(plate)) {
        m_error = QStringLiteral("plate %1 is locked").arg(plate + 1);
        emit errorChanged();
        return;
    }
    const bool any = std::any_of(m_objects.begin(), m_objects.end(),
                                 [plate](const Object& object) {
                                     return object.plate == plate;
                                 });
    if (!any)
        return;
    pushUndo(QStringLiteral("Clear plate %1").arg(plate + 1));
    m_objects.erase(std::remove_if(m_objects.begin(), m_objects.end(),
                                   [plate](const Object& object) {
                                       return object.plate == plate;
                                   }),
                    m_objects.end());
    m_selectedIds.clear();
    changed();
    emit selectionChanged();
    emit geometryChanged();
}

void PlateDocument::removePlate(int plate)
{
    if (plateCount() <= 1 || plate < 0 || plate >= plateCount())
        return;
    if (plateLocked(plate)) {
        m_error = QStringLiteral("plate %1 is locked").arg(plate + 1);
        emit errorChanged();
        return;
    }

    pushUndo(QStringLiteral("Remove plate %1").arg(plate + 1));
    std::vector<Object> kept;
    kept.reserve(m_objects.size());
    for (const Object& object : m_objects) {
        if (object.plate == plate)
            continue;
        Object moved = object;
        if (moved.plate > plate)
            --moved.plate;
        kept.push_back(std::move(moved));
    }
    m_objects = std::move(kept);
    --m_plateCount;
    if (plate < m_plateLocked.size())
        m_plateLocked.removeAt(plate);
    if (plate < m_plateNames.size())
        m_plateNames.removeAt(plate);
    m_selectedIds.clear();
    m_activePlate = std::clamp(m_activePlate, 0, std::max(0, plateCount() - 1));
    emit plateChanged();
    emit objectsChanged();
    emit selectionChanged();
    emit geometryChanged();
}

QString PlateDocument::undoLabel() const
{
    return m_undo.empty() ? QString() : m_undo.back().label;
}

QString PlateDocument::redoLabel() const
{
    return m_redo.empty() ? QString() : m_redo.back().label;
}

void PlateDocument::fail(const QString& message)
{
    m_error = message;
    emit errorChanged();
}

void PlateDocument::note(const QString& message)
{
    if (!m_pendingNotes.contains(message))
        m_pendingNotes.append(message);
}

void PlateDocument::pushUndo(const QString& label, int coalesceKey)
{
    if (!m_undo.empty() && coalesceKey != 0 && m_undo.back().coalesceKey == coalesceKey
        && m_undo.back().label == label) {
        m_redo.clear();
        emit undoChanged();
        return;
    }
    m_undo.push_back(Snapshot{m_objects, m_selectedIds, m_activePlate, label, coalesceKey});
    if (m_undo.size() > 100)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
    emit undoChanged();
}

void PlateDocument::restore(const Snapshot& snapshot)
{
    m_objects = snapshot.objects;
    m_selectedIds = snapshot.selection;
    m_activePlate = snapshot.activePlate;
    emit objectsChanged();
    emit selectionChanged();
    emit undoChanged();
}

void PlateDocument::undo()
{
    if (m_undo.empty())
        return;
    Snapshot snapshot = m_undo.back();
    m_undo.pop_back();
    m_redo.push_back(Snapshot{m_objects, m_selectedIds, m_activePlate, snapshot.label, 0});
    restore(snapshot);
}

void PlateDocument::redo()
{
    if (m_redo.empty())
        return;
    Snapshot snapshot = m_redo.back();
    m_redo.pop_back();
    m_undo.push_back(Snapshot{m_objects, m_selectedIds, m_activePlate, snapshot.label, 0});
    restore(snapshot);
}

int PlateDocument::addObject(const QString& path, const QString& label)
{
    return importObject(path, label, true);
}

int PlateDocument::addObjectAtFilePosition(const QString& path, const QString& label)
{
    return importObject(path, label, false);
}

bool PlateDocument::refuseOnLockedPlate()
{
    if (!plateLocked(m_activePlate))
        return false;
    m_error = QStringLiteral("plate %1 is locked").arg(m_activePlate + 1);
    emit errorChanged();
    return true;
}

QString PlateDocument::importWildcards() const
{
    QStringList patterns;
    for (const std::string& extension : Slic3r::Biz::FileLoadingLogic::get_import_extensions()) {
        const QString bare = QString::fromStdString(extension);
        patterns << QStringLiteral("*") + bare;
        patterns << QStringLiteral("*") + bare.toUpper();
    }
    return patterns.join(QLatin1Char(' '));
}

int PlateDocument::importObject(const QString& path, const QString& label, bool placeOnBed)
{
    if (refuseOnLockedPlate())
        return -1;
    const QString local = toLocalPath(path);

    // Everything the engine can read arrives through its own reader, which knows
    // STL, OBJ, 3MF, STEP and SVG and returns a scene rather than one mesh. A
    // single-mesh file keeps the placement behaviour this app has always had
    // (normalise to the bed, next free spot); a scene file carries its own
    // coordinates and its own object names, so it is taken as authored.
    const auto scene = Slic3r::Biz::FileLoadingLogic::read_model_from_file(local.toStdString(), nullptr);
    if (!scene) {
        fail(QStringLiteral("%1: %2")
                 .arg(QFileInfo(local).fileName(), QString::fromStdString(scene.error())));
        return -1;
    }
    if (scene->objects.empty()) {
        fail(QStringLiteral("%1: the file holds no object").arg(QFileInfo(local).fileName()));
        return -1;
    }

    // A 3MF is a scene file: its objects sit where its author put them, one
    // instance each with that placement on it. Everything else that arrives in one
    // piece is a mesh with no coordinates of its own, and keeps the placement
    // behaviour this app has always had -- normalise to the bed, next free spot.
    m_pendingNotes.clear();
    const bool authoredPlacement = local.endsWith(QLatin1String(".3mf"), Qt::CaseInsensitive);
    const bool single = !authoredPlacement && scene->objects.size() == 1
                        && scene->objects.front()->instances.size() <= 1;
    if (single)
        return importSceneObject(*scene->objects.front(),
                                 scene->objects.front()->instances.empty()
                                     ? nullptr
                                     : scene->objects.front()->instances.front(),
                                 local, label, placeOnBed, false);

    // The engine hands each object back with its own instance transform, and those
    // instances are what the build list said: baking the file's placement again
    // would apply the author's rotation twice and cancel it. The build list is read
    // only for what the engine does not model -- which plate an object was on.
    const next_layer::ThreeMfProject project = next_layer::readThreeMfProject(local);

    pushUndo(QStringLiteral("Import %1").arg(QFileInfo(local).fileName()));
    QList<int> added;
    for (const auto& objectPtr : scene->objects) {
        for (const auto& instance : objectPtr->instances) {
            const int id = importSceneObject(*objectPtr, instance, local, QString(), false, true,
                                             nullptr);
            if (id < 0)
                continue;
            added.push_back(id);
            // The records are matched by the order the model file lists its
            // objects in, which is the order the engine hands them back.
            const int position = added.size() - 1;
            if (position >= project.buildOrder.size())
                continue;
            const int objectId = project.buildOrder.at(position);
            const auto record = project.objectRecords.constFind(objectId);
            if (record != project.objectRecords.constEnd())
                applyProjectRecord(id, *objectPtr, *record);
            if (project.foreignSettings.contains(objectId))
                applyForeignSettings(id, project.foreignSettings.value(objectId),
                                     project.foreignNames.value(objectId));
        }
    }
    if (added.isEmpty()) {
        fail(QStringLiteral("%1: the file holds no object").arg(QFileInfo(local).fileName()));
        return -1;
    }

    // Timeline markers belong to the bed, and a bed is a plate here: the first
    // bed's markers become this plate's, in the document's own vocabulary.
    {
        const QVector<next_layer::ThreeMfLayerGcode> markers = project.layerGcodes.value(0);
        if (!markers.isEmpty()) {
            for (const next_layer::ThreeMfLayerGcode& marker : markers) {
                LayerGcode entry;
                entry.z = marker.z;
                switch (marker.type) {
                case 0: entry.type = QStringLiteral("color"); break;
                case 1: entry.type = QStringLiteral("pause"); break;
                case 2: entry.type = QStringLiteral("tool"); break;
                default: entry.type = QStringLiteral("gcode"); break;
                }
                entry.text = marker.text;
                m_layerGcodes.push_back(entry);
            }
            emit layerGcodesChanged();
        }
    }

    const bool hasPlates = project.valid && project.plateCount > 0
                           && project.buildOrder.size() == added.size();
    if (hasPlates)
        layoutProject(added, project);
    else if (added.size() > 1)
        arrangePlate();
    m_selectedIds = std::vector<int>(added.begin(), added.end());
    m_error = m_pendingNotes.join(QStringLiteral("; "));
    m_pendingNotes.clear();
    emit errorChanged();
    emit selectionChanged();
    changed();
    emit geometryChanged();
    return added.front();
}

/// Puts the objects of a project file where its author had them. Two things are/// settled here, and both come from the file rather than from the engine's
/// reader: which plate each object belongs to, and where on that plate it sits.
///
/// The reader hands back every object centred on its own bounding box, so the
/// arrangement the file was saved with is gone by the time the objects exist.
/// It is rebuilt from the build list: an object's placement matrix says where it
/// was on its plate, and the group on each plate is then moved as a whole so that
/// it lands on this plate, keeping its internal spacing.
namespace {

/// A volume's painted facets, from the file's per-triangle strings back into the
/// engine's selector data. The parser lives on a volume, so a scratch one is made
/// to run it: the state encoding is the engine's, and a second decoder here would
/// be a second thing to get wrong.
Slic3r::Domain::TriangleSelector::TriangleSplittingData facetsFrom(
    const QHash<int, QString>& painted, int triangleCount)
{
    if (painted.isEmpty())
        return {};
    Slic3r::Domain::Model scratch;
    Slic3r::Domain::ModelObject* holder = scratch.add_object();
    Slic3r::Domain::ModelVolume* volume =
        Slic3r::Biz::Algorithms::ModelVolume::construct_ptr(holder,
                                                            Slic3r::Domain::TriangleMesh{});
    volume->supported_facets.reserve(std::max(1, triangleCount));
    QList<int> indices = painted.keys();
    std::sort(indices.begin(), indices.end());
    for (const int index : indices)
        volume->supported_facets.set_triangle_from_string(index, painted.value(index).toStdString());
    volume->supported_facets.shrink_to_fit();
    return volume->supported_facets.get_data();
}

} // namespace

void PlateDocument::applyProjectRecord(int id, const Slic3r::Domain::ModelObject& source,
                                       const next_layer::ThreeMfObjectRecord& record)
{
    Object* object = find(id);
    if (object == nullptr || object->mesh == nullptr)
        return;

    QStringList refused;
    object->settings = next_layer::settingsFromText(record.settings, refused);
    if (!record.name.isEmpty())
        object->name = record.name;
    if (!refused.isEmpty()) {
        note(QStringLiteral("%1: %2 setting(s) in the file could not be read back: %3")
                 .arg(object->displayName())
                 .arg(refused.size())
                 .arg(refused.join(QStringLiteral(", "))));
    }

    if (record.layerHeights.size() >= 2) {
        Slic3r::Domain::ZHeightPairs bands;
        bands.reserve(size_t(record.layerHeights.size() / 2));
        for (int index = 0; index + 1 < record.layerHeights.size(); index += 2)
            bands.push_back({record.layerHeights.at(index), record.layerHeights.at(index + 1)});
        object->layerHeights = std::move(bands);
    }

    const int triangleCount = int(object->mesh->its.indices.size());
    if (!record.volumes.isEmpty()) {
        const next_layer::ThreeMfVolumeRecord& first = record.volumes.first();
        object->supportPaint = facetsFrom(first.supports, triangleCount);
        object->seamPaint = facetsFrom(first.seams, triangleCount);
        object->toolPaint = facetsFrom(first.tools, triangleCount);
    }

    // Volumes the object carries beyond its own body: modifiers, negative volumes,
    // support blockers. The engine's loader keeps their geometry and settings, and
    // this is where they become part of the document again instead of vanishing on
    // open.
    for (const Slic3r::Domain::ModelVolume* modelVolume : source.volumes) {
        if (modelVolume == nullptr
            || modelVolume->type() == Slic3r::Domain::ModelVolumeType::MODEL_PART)
            continue;
        Volume volume;
        volume.id = m_nextId++;
        volume.name = QStringLiteral("Volume");
        volume.type = modelVolume->type();
        volume.trafo = modelVolume->get_transformation();
        auto mesh = std::make_shared<TriangleMesh>(modelVolume->mesh());
        if (!meshBounds(*mesh, volume.localMin, volume.localMax))
            continue;
        volume.mesh = mesh;
        QStringList volumeUnreadable;
        volume.settings =
            next_layer::overriddenSettings(modelVolume->volume_settings, volumeUnreadable);
        object->volumes.push_back(std::move(volume));
    }
}

void PlateDocument::applyForeignSettings(int id, const QHash<QString, QString>& settings,
                                         const QString& name)
{
    Object* object = find(id);
    if (object == nullptr)
        return;
    if (!name.isEmpty())
        object->name = name;

    QStringList refused;
    QHash<QString, QString> translated;
    for (auto entry = settings.constBegin(); entry != settings.constEnd(); ++entry) {
        QString name;
        QString value;
        if (next_layer::translateForeignSetting(entry.key(), entry.value(), name, value))
            translated.insert(name, value);
    }

    const QVariantMap accepted = next_layer::settingsFromText(translated, refused);
    for (auto entry = accepted.constBegin(); entry != accepted.constEnd(); ++entry) {
        if (!object->settings.contains(entry.key()))
            object->settings.insert(entry.key(), entry.value());
    }

    // Every tool this app can address is one it has; a file that names a fourth
    // tool on a single-tool machine would otherwise put the object out of reach.
    const int tools = qMax(1, int(m_toolColours.size()));
    const auto tool = object->settings.find(QStringLiteral("extruder"));
    if (tool != object->settings.end()) {
        const int named = tool.value().toInt();
        if (named > tools)
            *tool = tools;
        else if (named < 1)
            object->settings.erase(tool);
    }
}

void PlateDocument::layoutProject(const QList<int>& added, const next_layer::ThreeMfProject& project)
{
    const bool knownPlates = project.valid && project.plateCount > 0
                             && project.buildOrder.size() == added.size();

    QHash<int, QVector<int>> onPlate;
    for (int index = 0; index < added.size(); ++index) {
        Object* object = find(added.at(index));
        if (object == nullptr)
            continue;
        if (knownPlates) {
            const int plate = project.plateOfObject.value(project.buildOrder.at(index), 0) - 1;
            if (plate >= 0 && plate < project.plateCount)
                object->plate = plate;
        }
        onPlate[object->plate].append(object->id);
    }

    if (onPlate.isEmpty())
        return;
    if (knownPlates)
        m_plateCount = std::max(m_plateCount, project.plateCount);

    // The plate bar and the selector read `plateCount`, and it just grew from one
    // plate to the project's fourteen: without this the file loads with every plate
    // holding its objects and the window still showing a single bed.
    emit plateChanged();

    for (auto it = onPlate.constBegin(); it != onPlate.constEnd(); ++it) {
        QVector3D low;
        QVector3D high;
        bool first = true;
        for (const int id : it.value()) {
            const Object* object = find(id);
            QVector3D objectLow;
            QVector3D objectHigh;
            if (object == nullptr
                || !boxOf(*object->mesh, object->trafo, object->localMin, object->localMax,
                          objectLow, objectHigh))
                continue;
            low = first ? objectLow : QVector3D(std::min(low.x(), objectLow.x()),
                                                std::min(low.y(), objectLow.y()), 0.0f);
            high = first ? objectHigh : QVector3D(std::max(high.x(), objectHigh.x()),
                                                  std::max(high.y(), objectHigh.y()), 0.0f);
            first = false;
        }
        if (first)
            continue;

        const float dx = m_bedCentre.x() - (low.x() + high.x()) * 0.5f;
        const float dy = m_bedCentre.y() - (low.y() + high.y()) * 0.5f;
        for (const int id : it.value()) {
            Object* object = find(id);
            if (object == nullptr)
                continue;
            const Vec3d offset = object->trafo.get_offset();
            object->trafo.set_offset(Vec3d(offset.x() + dx, offset.y() + dy, offset.z()));
        }
    }
}

int PlateDocument::importSceneObject(
    const Slic3r::Domain::ModelObject& source, const Slic3r::Domain::ModelInstance* instance,
    const QString& local, const QString& label, bool placeOnBed, bool deferPush,
    const next_layer::ThreeMfProject::Placement* placement)
{
    TriangleMesh mesh = source.raw_mesh();
    const QString name = source.name.empty() ? QFileInfo(local).fileName()
                                             : QString::fromStdString(source.name);

    // A placement the file recorded is baked into the geometry, rotation and all,
    // before the usual normalisation. Baking it keeps the object in whatever
    // attitude its author printed it in, and leaves the transform applied at run
    // time -- the one the panels read back -- free to mean what the user set.
    if (placement != nullptr) {
        Transform3d matrix = Transform3d::Identity();
        matrix.linear() << (*placement)[0], (*placement)[1], (*placement)[2],
                            (*placement)[3], (*placement)[4], (*placement)[5],
                            (*placement)[6], (*placement)[7], (*placement)[8];
        matrix.translation() << (*placement)[9], (*placement)[10], (*placement)[11];
        mesh.transform(matrix);
    }

    QVector3D fileMin;
    QVector3D fileMax;
    if (!meshBounds(mesh, fileMin, fileMax)) {
        fail(QStringLiteral("%1: %2 has no vertices").arg(QFileInfo(local).fileName(), name));
        return -1;
    }

    Object object;
    object.id = m_nextId++;
    object.plate = m_activePlate;
    object.path = local;
    object.name = name;
    object.label = label;

    if (instance != nullptr && !placeOnBed && placement == nullptr)
        object.trafo = instance->get_transformation();
    if (placeOnBed || placement != nullptr) {
        if (placement != nullptr) {
            // Height is the author's: a print-in-place assembly is arranged so its
            // parts sit above one another, and flattening each onto the bed would
            // stack them into each other. Only the horizontal centre moves, so the
            // transform this object carries is the position the panels show.
            const float centreX = (fileMin.x() + fileMax.x()) * 0.5f;
            const float centreY = (fileMin.y() + fileMax.y()) * 0.5f;
            mesh.translate(toVec3f(QVector3D(-centreX, -centreY, 0.0f)));
            object.trafo.set_offset(Vec3d(centreX, centreY, 0.0));
        } else {
            normalise(mesh, fileMin, fileMax);
        }
        object.mesh = std::make_shared<TriangleMesh>(mesh);
        meshBounds(*object.mesh, object.localMin, object.localMax);
        if (placeOnBed)
            placeNext(object);
    } else {
        object.mesh = std::make_shared<TriangleMesh>(mesh);
        meshBounds(*object.mesh, object.localMin, object.localMax);
        if (instance == nullptr)
            object.trafo.set_offset(Vec3d((fileMin.x() + fileMax.x()) * 0.5,
                                          (fileMin.y() + fileMax.y()) * 0.5, fileMin.z()));
    }

    // What the file also carried: the object's own overrides, its variable layer
    // height and its painted facets. A project that opens with the geometry but
    // without these is a different print with the same shape, so they come across
    // here rather than being left to the exporter's reputation.
    QStringList unreadable;
    object.settings = next_layer::overriddenSettings(source.object_settings, unreadable);
    if (!unreadable.isEmpty()) {
        fail(QStringLiteral("%1: %2 setting(s) in the file are of a type this build cannot "
                            "read back")
                 .arg(name)
                 .arg(unreadable.size()));
    }
    object.layerHeights = source.layer_height_profile.get();

    for (const Slic3r::Domain::ModelVolume* modelVolume : source.volumes) {
        if (modelVolume == nullptr)
            continue;
        if (!modelVolume->supported_facets.empty()
            || !modelVolume->seam_facets.empty()
            || !modelVolume->mm_segmentation_facets.empty()) {
            object.supportPaint = modelVolume->supported_facets.get_data();
            object.seamPaint = modelVolume->seam_facets.get_data();
            object.toolPaint = modelVolume->mm_segmentation_facets.get_data();
            break;
        }
    }

    if (!deferPush)
        pushUndo(QStringLiteral("Add %1").arg(object.displayName()));
    m_objects.push_back(std::move(object));
    if (!deferPush) {
        m_selectedIds = {m_objects.back().id};
        m_error.clear();
        emit errorChanged();
        emit selectionChanged();
        changed();
        emit geometryChanged();
    }
    return m_objects.back().id;
}

TriangleMesh PlateDocument::primitiveMesh(const QString& kind, double width, double depth,
                                            double height)
{
    namespace Algorithms = Slic3r::Biz::Algorithms::TriangleMesh;
    const double w = std::max(0.1, width);
    const double d = std::max(0.1, depth);
    const double h = std::max(0.1, height);

    if (kind == QStringLiteral("cylinder"))
        return Algorithms::make_cylinder(std::min(w, d) * 0.5, h);
    if (kind == QStringLiteral("sphere"))
        return Algorithms::make_sphere(std::min({w, d, h}) * 0.5);
    return Algorithms::make_cube(w, d, h);
}

QString PlateDocument::volumeTypeName(VolumeType type)
{
    switch (type) {
    case VolumeType::NEGATIVE_VOLUME: return QStringLiteral("negative");
    case VolumeType::PARAMETER_MODIFIER: return QStringLiteral("modifier");
    case VolumeType::SUPPORT_BLOCKER: return QStringLiteral("blocker");
    case VolumeType::SUPPORT_ENFORCER: return QStringLiteral("enforcer");
    default: return QStringLiteral("part");
    }
}

PlateDocument::VolumeType PlateDocument::volumeTypeFromName(const QString& name)
{
    if (name == QStringLiteral("negative"))
        return VolumeType::NEGATIVE_VOLUME;
    if (name == QStringLiteral("modifier"))
        return VolumeType::PARAMETER_MODIFIER;
    if (name == QStringLiteral("blocker"))
        return VolumeType::SUPPORT_BLOCKER;
    if (name == QStringLiteral("enforcer"))
        return VolumeType::SUPPORT_ENFORCER;
    return VolumeType::MODEL_PART;
}

int PlateDocument::addShape(const QString& kind, double width, double depth, double height)
{
    if (refuseOnLockedPlate())
        return -1;
    TriangleMesh mesh = primitiveMesh(kind, width, depth, height);
    QVector3D low;
    QVector3D high;
    if (!meshBounds(mesh, low, high)) {
        fail(QStringLiteral("could not build a %1").arg(kind));
        return -1;
    }
    normalise(mesh, low, high);
    meshBounds(mesh, low, high);

    Object object;
    object.id = m_nextId++;
    object.plate = m_activePlate;
    object.name = kind == QStringLiteral("cylinder") ? QStringLiteral("Cylinder")
                : kind == QStringLiteral("sphere")   ? QStringLiteral("Sphere")
                                                     : QStringLiteral("Box");
    object.mesh = std::make_shared<const TriangleMesh>(std::move(mesh));
    object.localMin = low;
    object.localMax = high;
    placeNext(object);

    pushUndo(QStringLiteral("Add %1").arg(object.name));
    m_objects.push_back(std::move(object));
    m_selectedIds = {m_objects.back().id};
    changed();
    emit selectionChanged();
    emit geometryChanged();
    return m_objects.back().id;
}

int PlateDocument::addVolume(int objectId, const QString& kind, const QString& type,
                             double width, double depth, double height)
{
    Object* object = find(objectId);
    if (object == nullptr)
        return -1;

    TriangleMesh mesh = primitiveMesh(kind, width, depth, height);
    QVector3D low;
    QVector3D high;
    if (!meshBounds(mesh, low, high)) {
        fail(QStringLiteral("could not build a %1 volume").arg(kind));
        return -1;
    }
    normalise(mesh, low, high);
    meshBounds(mesh, low, high);

    Volume volume;
    volume.id = m_nextId++;
    volume.type = volumeTypeFromName(type);
    volume.name = kind == QStringLiteral("cylinder") ? QStringLiteral("Cylinder")
                : kind == QStringLiteral("sphere")   ? QStringLiteral("Sphere")
                                                     : QStringLiteral("Box");
    volume.mesh = std::make_shared<const TriangleMesh>(std::move(mesh));
    volume.localMin = low;
    volume.localMax = high;
    // The volume sits in the middle of the object's footprint, in the object's
    // own frame, so it stays with the object when that object is transformed.
    volume.trafo.set_offset(Vec3d((object->localMin.x() + object->localMax.x()) * 0.5,
                                  (object->localMin.y() + object->localMax.y()) * 0.5,
                                  object->localMin.z()));

    const QString label = volumeTypeName(volume.type);
    pushUndo(QStringLiteral("Add %1 to %2").arg(label, displayNameOf(objectId)));
    object = find(objectId);
    if (object == nullptr)
        return -1;
    const int volumeId = volume.id;
    object->volumes.push_back(std::move(volume));
    changed();
    emit geometryChanged();
    return volumeId;
}

bool PlateDocument::removeVolume(int objectId, int volumeId)
{
    Object* object = find(objectId);
    if (object == nullptr)
        return false;
    const auto it = std::find_if(object->volumes.begin(), object->volumes.end(),
                                 [volumeId](const Volume& volume) {
                                     return volume.id == volumeId;
                                 });
    if (it == object->volumes.end())
        return false;

    pushUndo(QStringLiteral("Delete %1 volume").arg(volumeTypeName(it->type)));
    object = find(objectId);
    if (object == nullptr)
        return false;
    object->volumes.erase(std::remove_if(object->volumes.begin(), object->volumes.end(),
                                         [volumeId](const Volume& volume) {
                                             return volume.id == volumeId;
                                         }),
                          object->volumes.end());
    changed();
    emit geometryChanged();
    return true;
}

bool PlateDocument::setVolumeVisible(int objectId, int volumeId, bool visible)
{
    Object* object = find(objectId);
    if (object == nullptr)
        return false;
    const bool found = std::any_of(object->volumes.begin(), object->volumes.end(),
                                   [volumeId, visible](const Volume& volume) {
                                       return volume.id == volumeId && volume.visible != visible;
                                   });
    if (!found)
        return false;

    pushUndo(QStringLiteral("Toggle a volume"));
    object = find(objectId);
    if (object == nullptr)
        return false;
    for (Volume& volume : object->volumes) {
        if (volume.id == volumeId)
            volume.visible = visible;
    }
    changed();
    emit geometryChanged();
    return true;
}

bool PlateDocument::setVolumePosition(int objectId, int volumeId, const QVector3D& position)
{
    Object* object = find(objectId);
    if (object == nullptr)
        return false;
    const auto it = std::find_if(object->volumes.begin(), object->volumes.end(),
                                 [volumeId](const Volume& volume) {
                                     return volume.id == volumeId;
                                 });
    if (it == object->volumes.end())
        return false;

    pushUndo(QStringLiteral("Move %1 volume").arg(volumeTypeName(it->type)), volumeId * 16 + 3);
    object = find(objectId);
    if (object == nullptr)
        return false;
    for (Volume& volume : object->volumes) {
        if (volume.id == volumeId)
            volume.trafo.set_offset(Vec3d(position.x(), position.y(), position.z()));
    }
    changed();
    emit geometryChanged();
    return true;
}

bool PlateDocument::setVolumeScale(int objectId, int volumeId, double factor)
{
    Object* object = find(objectId);
    if (object == nullptr || factor <= 0.0)
        return false;
    const auto it = std::find_if(object->volumes.begin(), object->volumes.end(),
                                 [volumeId](const Volume& volume) {
                                     return volume.id == volumeId;
                                 });
    if (it == object->volumes.end())
        return false;

    pushUndo(QStringLiteral("Resize %1 volume").arg(volumeTypeName(it->type)),
             volumeId * 16 + 4);
    object = find(objectId);
    if (object == nullptr)
        return false;
    for (Volume& volume : object->volumes) {
        if (volume.id == volumeId)
            volume.trafo.set_scaling_factor(Vec3d(factor, factor, factor));
    }
    changed();
    emit geometryChanged();
    return true;
}

QVariantList PlateDocument::volumes(int objectId) const
{
    QVariantList list;
    const Object* object = find(objectId);
    if (object == nullptr)
        return list;
    for (const Volume& volume : object->volumes) {
        QVariantMap info;
        info.insert(QStringLiteral("id"), volume.id);
        info.insert(QStringLiteral("name"), volume.name);
        info.insert(QStringLiteral("type"), volumeTypeName(volume.type));
        info.insert(QStringLiteral("visible"), volume.visible);
        info.insert(QStringLiteral("width"), volume.localMax.x() - volume.localMin.x());
        info.insert(QStringLiteral("depth"), volume.localMax.y() - volume.localMin.y());
        info.insert(QStringLiteral("height"), volume.localMax.z() - volume.localMin.z());
        const Vec3d offset = volume.trafo.get_offset();
        info.insert(QStringLiteral("x"), offset.x());
        info.insert(QStringLiteral("y"), offset.y());
        info.insert(QStringLiteral("z"), offset.z());
        info.insert(QStringLiteral("scale"), volume.trafo.get_scaling_factor().x());
        list.append(info);
    }
    return list;
}

bool PlateDocument::renameObject(int id, const QString& name)
{
    Object* object = find(id);
    if (object == nullptr || name.trimmed().isEmpty() || name == object->displayName())
        return false;
    pushUndo(QStringLiteral("Rename %1").arg(object->displayName()));
    object = find(id);
    if (object == nullptr)
        return false;
    object->label = name.trimmed();
    changed();
    return true;
}

void PlateDocument::placeNext(Object& object) const
{
    const double halfWidth = (object.localMax.x() - object.localMin.x()) * 0.5;
    const double halfDepth = (object.localMax.y() - object.localMin.y()) * 0.5;
    const double halfBedWidth = m_bedSize.x() * 0.5;
    const double bedLeft = m_bedCentre.x() - halfBedWidth;
    const double bedRight = m_bedCentre.x() + halfBedWidth;

    Vec3d offset(m_bedCentre.x(), m_bedCentre.y(), 0.0);
    const bool plateEmpty = std::all_of(m_objects.begin(), m_objects.end(), [this](const Object& other) {
        return other.plate != m_activePlate;
    });
    if (!plateEmpty) {
        const QVector3D low = boundsMin();
        const QVector3D high = boundsMax();
        const double candidate = high.x() + ObjectGap + halfWidth;
        if (candidate <= bedRight)
            offset = Vec3d(candidate, (low.y() + high.y()) * 0.5, 0.0);
        else
            offset = Vec3d(bedLeft + halfWidth, high.y() + ObjectGap + halfDepth, 0.0);
    }
    object.trafo.set_offset(offset);
}

bool PlateDocument::removeObject(int id)
{
    const auto it = std::find_if(m_objects.begin(), m_objects.end(),
                                 [id](const Object& object) { return object.id == id; });
    if (it == m_objects.end())
        return false;

    pushUndo(QStringLiteral("Delete %1").arg(it->displayName()));
    m_objects.erase(it);
    m_selectedIds.erase(std::remove(m_selectedIds.begin(), m_selectedIds.end(), id),
                        m_selectedIds.end());
    if (m_selectedIds.empty() && !m_objects.empty())
        m_selectedIds = {m_objects.back().id};
    emit selectionChanged();
    changed();
    emit geometryChanged();
    return true;
}

int PlateDocument::duplicateObject(int id)
{
    const Object* source = find(id);
    if (source == nullptr)
        return -1;

    Object copy = *source;
    copy.id = m_nextId++;
    for (Volume& volume : copy.volumes)
        volume.id = m_nextId++;
    const QVector3D low = objectBoundsMin(id);
    const QVector3D high = objectBoundsMax(id);
    const Vec3d offset = source->trafo.get_offset();
    copy.trafo.set_offset(Vec3d(high.x() + ObjectGap + (high.x() - low.x()) * 0.5,
                                offset.y(), offset.z()));

    pushUndo(QStringLiteral("Duplicate %1").arg(source->displayName()));
    m_objects.push_back(std::move(copy));
    m_selectedIds = {m_objects.back().id};
    emit selectionChanged();
    changed();
    emit geometryChanged();
    return selectedId();
}

void PlateDocument::clear()
{
    const bool empty = std::all_of(m_objects.begin(), m_objects.end(), [this](const Object& object) {
        return object.plate != m_activePlate;
    });
    if (empty)
        return;
    pushUndo(QStringLiteral("Clear plate"));
    m_objects.erase(std::remove_if(m_objects.begin(), m_objects.end(),
                                   [this](const Object& object) {
                                       return object.plate == m_activePlate;
                                   }),
                    m_objects.end());
    m_selectedIds.clear();
    m_stagedRevision = 0;
    changed();
    emit selectionChanged();
    emit geometryChanged();
}

void PlateDocument::selectObject(int id, bool additive)
{
    const int next = has(id) ? id : 0;
    std::vector<int> selection = additive ? m_selectedIds : std::vector<int>();
    if (additive) {
        const auto it = std::find(selection.begin(), selection.end(), next);
        if (it != selection.end())
            selection.erase(it);
        else if (next != 0)
            selection.push_back(next);
    } else if (next != 0) {
        selection.push_back(next);
    }

    if (selection == m_selectedIds)
        return;
    m_selectedIds = std::move(selection);
    emit selectionChanged();
    emit objectsChanged();
}

void PlateDocument::selectRange(int id)
{
    if (!has(id))
        return;
    int anchor = selectedId();
    if (anchor == 0 || !has(anchor))
        anchor = id;

    int from = 0;
    int to = 0;
    for (int index = 0; index < count(); ++index) {
        if (m_objects[static_cast<size_t>(index)].id == anchor)
            from = index;
        if (m_objects[static_cast<size_t>(index)].id == id)
            to = index;
    }
    if (from > to)
        std::swap(from, to);

    std::vector<int> selection;
    selection.reserve(static_cast<size_t>(to - from + 1));
    for (int index = from; index <= to; ++index)
        selection.push_back(m_objects[static_cast<size_t>(index)].id);
    if (selection == m_selectedIds)
        return;
    m_selectedIds = std::move(selection);
    emit selectionChanged();
    emit objectsChanged();
}

void PlateDocument::selectAll()
{
    std::vector<int> selection;
    selection.reserve(m_objects.size());
    for (const Object& object : m_objects)
        selection.push_back(object.id);
    if (selection == m_selectedIds)
        return;
    m_selectedIds = std::move(selection);
    emit selectionChanged();
    emit objectsChanged();
}

void PlateDocument::selectObjects(const QVariantList& ids)
{
    std::vector<int> selection;
    selection.reserve(ids.size());
    for (const QVariant& entry : ids) {
        const int id = entry.toInt();
        const Object* object = find(id);
        if (object == nullptr || object->plate != m_activePlate)
            continue;
        if (std::find(selection.begin(), selection.end(), id) == selection.end())
            selection.push_back(id);
    }
    if (selection == m_selectedIds)
        return;
    m_selectedIds = std::move(selection);
    emit selectionChanged();
    emit objectsChanged();
}

void PlateDocument::clearSelection()
{
    if (m_selectedIds.empty())
        return;
    m_selectedIds.clear();
    emit selectionChanged();
    emit objectsChanged();
}

bool PlateDocument::isSelected(int id) const
{
    return std::find(m_selectedIds.begin(), m_selectedIds.end(), id) != m_selectedIds.end();
}

QVariantList PlateDocument::selectedIds() const
{
    QVariantList list;
    list.reserve(static_cast<int>(m_selectedIds.size()));
    for (int id : m_selectedIds)
        list.append(id);
    return list;
}

bool PlateDocument::moveSelection(double deltaX, double deltaY, double deltaZ)
{
    if (refuseOnLockedPlate())
        return false;
    if (m_selectedIds.empty())
        return false;
    if (std::abs(deltaX) + std::abs(deltaY) + std::abs(deltaZ) < 1e-9)
        return true;

    pushUndo(m_selectedIds.size() > 1
                 ? QStringLiteral("Move %1 objects").arg(m_selectedIds.size())
                 : QStringLiteral("Move %1").arg(displayNameOf(m_selectedIds.front())),
             m_selectedIds.size() > 1 ? 70001 : m_selectedIds.front() * 16 + 1);

    for (int id : m_selectedIds) {
        Object* object = find(id);
        if (object == nullptr)
            continue;
        const Vec3d offset = object->trafo.get_offset();
        object->trafo.set_offset(Vec3d(offset.x() + deltaX, offset.y() + deltaY, offset.z() + deltaZ));
    }
    changed();
    return true;
}

bool PlateDocument::rotateSelection(double degrees)
{
    if (m_selectedIds.empty())
        return false;
    pushUndo(m_selectedIds.size() > 1
                 ? QStringLiteral("Rotate %1 objects").arg(m_selectedIds.size())
                 : QStringLiteral("Rotate %1").arg(displayNameOf(m_selectedIds.front())),
             m_selectedIds.size() > 1 ? 70002 : m_selectedIds.front() * 16 + 2);
    for (int id : m_selectedIds) {
        Object* object = find(id);
        if (object == nullptr)
            continue;
        const Vec3d rotation = object->trafo.get_rotation();
        object->trafo.set_rotation(Vec3d(rotation.x(), rotation.y(),
                                         rotation.z() + qDegreesToRadians(degrees)));
    }
    changed();
    return true;
}

bool PlateDocument::scaleSelection(double factor)
{
    if (m_selectedIds.empty() || factor <= 0.0)
        return false;
    pushUndo(m_selectedIds.size() > 1
                 ? QStringLiteral("Scale %1 objects").arg(m_selectedIds.size())
                 : QStringLiteral("Scale %1").arg(displayNameOf(m_selectedIds.front())),
             m_selectedIds.size() > 1 ? 70003 : m_selectedIds.front() * 16 + 3);
    for (int id : m_selectedIds) {
        Object* object = find(id);
        if (object == nullptr)
            continue;
        const Vec3d scale = object->trafo.get_scaling_factor();
        object->trafo.set_scaling_factor(Vec3d(scale.x() * factor, scale.y() * factor,
                                               scale.z() * factor));
    }
    changed();
    return true;
}

void PlateDocument::removeSelection()
{
    if (refuseOnLockedPlate())
        return;
    const std::vector<int> selection = m_selectedIds;
    if (selection.empty())
        return;
    if (selection.size() == 1) {
        removeObject(selection.front());
        return;
    }
    pushUndo(QStringLiteral("Delete %1 objects").arg(selection.size()));
    for (int id : selection) {
        const auto it = std::find_if(m_objects.begin(), m_objects.end(),
                                     [id](const Object& object) { return object.id == id; });
        if (it != m_objects.end())
            m_objects.erase(it);
    }
    m_selectedIds.clear();
    if (!m_objects.empty())
        m_selectedIds = {m_objects.back().id};
    emit selectionChanged();
    changed();
    emit geometryChanged();
}

void PlateDocument::duplicateSelection()
{
    const std::vector<int> selection = m_selectedIds;
    if (selection.empty())
        return;
    std::vector<int> copies;
    pushUndo(selection.size() > 1 ? QStringLiteral("Duplicate %1 objects").arg(selection.size())
                                  : QStringLiteral("Duplicate %1").arg(displayNameOf(selection.front())));
    for (int id : selection) {
        const Object* source = find(id);
        if (source == nullptr)
            continue;
        Object copy = *source;
        copy.id = m_nextId++;
        const Vec3d offset = source->trafo.get_offset();
        copy.trafo.set_offset(Vec3d(offset.x() + ObjectGap * 2, offset.y() + ObjectGap * 2,
                                    offset.z()));
        copies.push_back(copy.id);
        m_objects.push_back(std::move(copy));
    }
    if (copies.empty())
        return;
    m_selectedIds = std::move(copies);
    emit selectionChanged();
    changed();
    emit geometryChanged();
}

void PlateDocument::centreSelection()
{
    if (refuseOnLockedPlate())
        return;
    const std::vector<int> selection = m_selectedIds;
    if (selection.empty())
        return;
    QVector3D low;
    QVector3D high;
    bool first = true;
    for (int id : selection) {
        const QVector3D objectLow = objectBoundsMin(id);
        const QVector3D objectHigh = objectBoundsMax(id);
        low = first ? objectLow : QVector3D(std::min(low.x(), objectLow.x()),
                                            std::min(low.y(), objectLow.y()),
                                            std::min(low.z(), objectLow.z()));
        high = first ? objectHigh : QVector3D(std::max(high.x(), objectHigh.x()),
                                              std::max(high.y(), objectHigh.y()),
                                              std::max(high.z(), objectHigh.z()));
        first = false;
    }
    moveSelection(m_bedCentre.x() - (low.x() + high.x()) * 0.5,
                  m_bedCentre.y() - (low.y() + high.y()) * 0.5, 0.0);
}

void PlateDocument::dropSelection()
{
    if (refuseOnLockedPlate())
        return;
    const std::vector<int> selection = m_selectedIds;
    if (selection.empty())
        return;
    double lowest = 0.0;
    bool first = true;
    for (int id : selection) {
        const QVector3D low = objectBoundsMin(id);
        lowest = first ? low.z() : std::min(lowest, static_cast<double>(low.z()));
        first = false;
    }
    if (std::abs(lowest) < 1e-4)
        return;
    moveSelection(0.0, 0.0, -lowest);
}

void PlateDocument::isolateSelection()
{
    if (m_selectedIds.empty())
        return;
    pushUndo(QStringLiteral("Isolate selection"));
    for (Object& object : m_objects)
        object.visible = isSelected(object.id);
    changed();
    emit geometryChanged();
}

void PlateDocument::showAll()
{
    bool hidden = false;
    for (const Object& object : m_objects)
        hidden = hidden || !object.visible;
    if (!hidden)
        return;
    pushUndo(QStringLiteral("Show all"));
    for (Object& object : m_objects)
        object.visible = true;
    changed();
    emit geometryChanged();
}

bool PlateDocument::setPosition(int id, const QVector3D& position)
{
    Object* object = find(id);
    if (object == nullptr)
        return false;
    pushUndo(QStringLiteral("Move %1").arg(object->displayName()), id * 16 + 1);
    object->trafo.set_offset(Vec3d(position.x(), position.y(), position.z()));
    changed();
    return true;
}

bool PlateDocument::setRotation(int id, const QVector3D& degrees)
{
    Object* object = find(id);
    if (object == nullptr)
        return false;
    pushUndo(QStringLiteral("Rotate %1").arg(object->displayName()), id * 16 + 2);
    object->trafo.set_rotation(Vec3d(qDegreesToRadians(degrees.x()),
                                     qDegreesToRadians(degrees.y()),
                                     qDegreesToRadians(degrees.z())));
    changed();
    return true;
}

bool PlateDocument::setScale(int id, const QVector3D& factor)
{
    Object* object = find(id);
    if (object == nullptr)
        return false;
    pushUndo(QStringLiteral("Scale %1").arg(object->displayName()), id * 16 + 3);
    object->trafo.set_scaling_factor(Vec3d(factor.x(), factor.y(), factor.z()));
    changed();
    return true;
}

bool PlateDocument::setVisible(int id, bool visible)
{
    Object* object = find(id);
    if (object == nullptr)
        return false;
    if (object->visible == visible)
        return true;
    pushUndo(visible ? QStringLiteral("Show %1").arg(object->displayName())
                     : QStringLiteral("Hide %1").arg(object->displayName()));
    object->visible = visible;
    changed();
    return true;
}

bool PlateDocument::centreObject(int id)
{
    const Object* object = find(id);
    if (object == nullptr)
        return false;
    const QVector3D low = objectBoundsMin(id);
    const QVector3D high = objectBoundsMax(id);
    const QVector3D position = object->position();
    return setPosition(id, QVector3D(m_bedCentre.x() - (low.x() + high.x()) * 0.5 + position.x(),
                                     m_bedCentre.y() - (low.y() + high.y()) * 0.5 + position.y(),
                                     position.z()));
}

bool PlateDocument::dropToBed(int id)
{
    const Object* object = find(id);
    if (object == nullptr)
        return false;
    const QVector3D low = objectBoundsMin(id);
    if (std::abs(low.z()) < 1e-4)
        return true;
    const QVector3D position = object->position();
    return setPosition(id, QVector3D(position.x(), position.y(), position.z() - low.z()));
}

bool PlateDocument::placeOnFace(int id, int facet)
{
    Object* object = find(id);
    if (object == nullptr || object->mesh == nullptr)
        return false;

    const Transform3d rotated = next_layer::layFacetFlat(*object->mesh, object->trafo.get_matrix(),
                                                         facet);
    pushUndo(QStringLiteral("Place %1 on a face").arg(object->displayName()));
    object->trafo.set_matrix(rotated);
    changed();
    dropToBed(id);
    return true;
}

bool PlateDocument::autoOrient(int id)
{
    Object* object = find(id);
    if (object == nullptr || object->mesh == nullptr)
        return false;

    const std::optional<Transform3d> rotation =
        next_layer::bestRestingRotation(*object->mesh, object->trafo.get_matrix());
    if (!rotation.has_value())
        return false;

    pushUndo(QStringLiteral("Auto-orient %1").arg(object->displayName()));
    object->trafo.set_matrix(*rotation);
    changed();
    dropToBed(id);
    return true;
}

bool PlateDocument::autoOrientSelection()
{
    if (m_selectedIds.empty())
        return false;

    const std::vector<int> selection = m_selectedIds;
    pushUndo(selection.size() > 1 ? QStringLiteral("Auto-orient %1 objects").arg(selection.size())
                                  : QStringLiteral("Auto-orient %1").arg(displayNameOf(selection.front())));
    for (int id : selection) {
        Object* object = find(id);
        if (object == nullptr || object->mesh == nullptr)
            continue;
        const std::optional<Transform3d> rotation =
            next_layer::bestRestingRotation(*object->mesh, object->trafo.get_matrix());
        if (!rotation.has_value())
            continue;
        object->trafo.set_matrix(*rotation);
        const QVector3D low = objectBoundsMin(id);
        if (std::abs(low.z()) > 1e-4) {
            const Vec3d offset = object->trafo.get_offset();
            object->trafo.set_offset(Vec3d(offset.x(), offset.y(), offset.z() - low.z()));
        }
    }
    changed();
    return true;
}

bool PlateDocument::arrangePlate()
{
    std::vector<next_layer::PlaceableObject> placeables;
    for (const Object& object : m_objects) {
        if (object.plate != m_activePlate || !object.visible || object.mesh == nullptr)
            continue;
        placeables.push_back(next_layer::PlaceableObject{
            object.id, object.mesh.get(), object.trafo.get_matrix()});
    }
    if (placeables.empty())
        return false;

    pushUndo(QStringLiteral("Arrange the plate"));
    const int applied = applyPlacements(placeables);
    changed();
    return applied > 0;
}

bool PlateDocument::arrangeSelection()
{
    std::vector<next_layer::PlaceableObject> placeables;
    for (int id : m_selectedIds) {
        const Object* object = find(id);
        if (object == nullptr || object->mesh == nullptr)
            continue;
        placeables.push_back(next_layer::PlaceableObject{
            object->id, object->mesh.get(), object->trafo.get_matrix()});
    }
    if (placeables.empty())
        return false;

    pushUndo(placeables.size() > 1
                 ? QStringLiteral("Arrange %1 objects").arg(placeables.size())
                 : QStringLiteral("Arrange %1").arg(displayNameOf(placeables.front().id)));
    const int applied = applyPlacements(placeables);
    changed();
    return applied > 0;
}

int PlateDocument::applyPlacements(const std::vector<next_layer::PlaceableObject>& placeables)
{
    const auto placements = next_layer::arrangeOnBed(placeables, m_bedCentre, m_bedSize, true);
    int applied = 0;
    for (const next_layer::Placement& placement : placements) {
        Object* object = find(placement.id);
        if (object == nullptr)
            continue;
        const Vec3d rotation = object->trafo.get_rotation();
        object->trafo.set_rotation(Vec3d(rotation.x(), rotation.y(),
                                         rotation.z() + placement.rotationZ));
        object->trafo.set_offset(placement.offset);
        const QVector3D low = objectBoundsMin(placement.id);
        if (std::abs(low.z()) > 1e-4) {
            const Vec3d offset = object->trafo.get_offset();
            object->trafo.set_offset(Vec3d(offset.x(), offset.y(), offset.z() - low.z()));
        }
        ++applied;
    }
    return applied;
}


QVariantMap PlateDocument::volumeSettings(int objectId, int volumeId) const
{
    const Object* object = find(objectId);
    if (object == nullptr)
        return {};
    for (const Volume& volume : object->volumes) {
        if (volume.id == volumeId)
            return volume.settings;
    }
    return {};
}

bool PlateDocument::setVolumeSetting(int objectId, int volumeId, const QString& name,
                                     const QVariant& value)
{
    Object* object = find(objectId);
    if (object == nullptr || name.isEmpty())
        return false;
    for (Volume& volume : object->volumes) {
        if (volume.id != volumeId)
            continue;
        if (volume.settings.value(name) == value)
            return true;
        pushUndo(QStringLiteral("%1: %2").arg(volume.name, name), volume.id * 32 + qHash(name) % 16);
        volume.settings.insert(name, value);
        changed();
        return true;
    }
    return false;
}

bool PlateDocument::clearVolumeSetting(int objectId, int volumeId, const QString& name)
{
    Object* object = find(objectId);
    if (object == nullptr)
        return false;
    for (Volume& volume : object->volumes) {
        if (volume.id != volumeId || !volume.settings.contains(name))
            continue;
        pushUndo(QStringLiteral("%1: drop %2").arg(volume.name, name));
        volume.settings.remove(name);
        changed();
        return true;
    }
    return false;
}

QVariantMap PlateDocument::objectSettings(int id) const
{
    const Object* object = find(id);
    return object == nullptr ? QVariantMap() : object->settings;
}

bool PlateDocument::setObjectSetting(int id, const QString& name, const QVariant& value)
{
    Object* object = find(id);
    if (object == nullptr || name.isEmpty())
        return false;
    if (object->settings.value(name) == value)
        return true;

    pushUndo(QStringLiteral("%1: %2").arg(object->displayName(), name),
             object->id * 32 + qHash(name) % 16);
    object->settings.insert(name, value);
    changed();
    return true;
}

bool PlateDocument::clearObjectSetting(int id, const QString& name)
{
    Object* object = find(id);
    if (object == nullptr || !object->settings.contains(name))
        return false;
    pushUndo(QStringLiteral("%1: drop %2").arg(object->displayName(), name));
    object->settings.remove(name);
    changed();
    return true;
}

void PlateDocument::clearObjectSettings(int id)
{
    Object* object = find(id);
    if (object == nullptr || object->settings.isEmpty())
        return;
    pushUndo(QStringLiteral("%1: drop every override").arg(object->displayName()));
    object->settings.clear();
    changed();
}

namespace {
const char* kToolSettings[] = {"perimeter_extruder", "infill_extruder", "solid_infill_extruder",
                               "support_material_extruder", "support_material_interface_extruder"};
}

int PlateDocument::objectTool(int id) const
{
    const Object* object = find(id);
    if (object == nullptr)
        return 1;
    for (const char* name : kToolSettings) {
        const QVariant value = object->settings.value(QString::fromLatin1(name));
        if (value.isValid())
            return std::max(1, value.toInt());
    }
    return 1;
}

void PlateDocument::setObjectTool(int id, int tool)
{
    Object* object = find(id);
    if (object == nullptr)
        return;
    const int clamped = std::clamp(tool, 1, std::max(1, int(m_toolColours.size())));
    bool touched = false;
    for (const char* name : kToolSettings) {
        const QString key = QString::fromLatin1(name);
        if (clamped == 1) {
            if (object->settings.remove(key) > 0)
                touched = true;
            continue;
        }
        if (object->settings.value(key).toInt() != clamped) {
            object->settings.insert(key, clamped);
            touched = true;
        }
    }
    if (!touched)
        return;
    pushUndo(QStringLiteral("%1: tool %2").arg(object->displayName()).arg(clamped));
    changed();
}

QVariantList PlateDocument::layerHeightProfile(int id) const
{
    QVariantList result;
    const Object* object = find(id);
    if (object == nullptr)
        return result;
    for (const Slic3r::Domain::ZHeightPair& pair : object->layerHeights) {
        QVariantMap entry;
        entry.insert(QStringLiteral("z"), pair.z);
        entry.insert(QStringLiteral("height"), pair.layer_height);
        result.append(entry);
    }
    return result;
}

bool PlateDocument::setLayerHeightRange(int id, double bottomZ, double topZ, double height)
{
    Object* object = find(id);
    if (object == nullptr || height <= 0.0 || topZ <= bottomZ)
        return false;

    const QVector3D low = objectBoundsMin(id);
    const QVector3D high = objectBoundsMax(id);
    const double objectBottom = low.z();
    const double objectTop = high.z();
    const double from = std::clamp(bottomZ, objectBottom, objectTop);
    const double to = std::clamp(topZ, from, objectTop);
    if (to - from < 1e-4)
        return false;

    // A layer height profile is a list of (z, height) bands that must cover the
    // whole object: outside the edited band it restates the printer's own layer
    // height, inside it steps to the requested one. Zero is not a height -- a
    // profile full of zeros is what "no profile" looks like to the engine.
    const double base = m_baseLayerHeight;
    const double epsilon = 1e-3;
    Slic3r::Domain::ZHeightPairs profile;
    auto addBand = [&profile](double z, double layerHeight) {
        if (!profile.empty()) {
            const Slic3r::Domain::ZHeightPair& last = profile.back();
            if (std::abs(last.z - z) < 1e-6 && std::abs(last.layer_height - layerHeight) < 1e-6)
                return;
        }
        Slic3r::Domain::ZHeightPair pair;
        pair.z = z;
        pair.layer_height = layerHeight;
        profile.push_back(pair);
    };

    addBand(objectBottom, base);
    addBand(from, base);
    addBand(from + epsilon, height);
    addBand(to - epsilon, height);
    addBand(to, base);
    addBand(objectTop, base);

    pushUndo(QStringLiteral("%1: variable layer height").arg(object->displayName()));
    object->layerHeights = profile;
    changed();
    return true;
}

void PlateDocument::clearLayerHeights(int id)
{
    Object* object = find(id);
    if (object == nullptr || object->layerHeights.empty())
        return;
    pushUndo(QStringLiteral("%1: uniform layers").arg(object->displayName()));
    object->layerHeights.clear();
    changed();
}

int PlateDocument::paintObject(int id, const QString& kind, const QVector3D& worldPoint,
                               double radius, bool erase, int extruder, int facetHint)
{
    Object* object = find(id);
    if (object == nullptr || object->mesh == nullptr)
        return 0;

    const next_layer::PaintKind paintKind =
        kind == QLatin1String("seam") ? next_layer::PaintKind::Seam
      : kind == QLatin1String("tool") ? next_layer::PaintKind::Tool
                                     : next_layer::PaintKind::Supports;

    // The stroke arrives in printer coordinates; the paint lives in mesh space,
    // so it has to be pulled back through the object's own transform.
    const Transform3d inverse = object->trafo.get_matrix().inverse();
    const Vec3d local = inverse * Vec3d(worldPoint.x(), worldPoint.y(), worldPoint.z());
    const Vec3f centre(local.x(), local.y(), local.z());

    Slic3r::Domain::TriangleSelector::TriangleSplittingData& data =
        paintKind == next_layer::PaintKind::Seam ? object->seamPaint
      : paintKind == next_layer::PaintKind::Tool ? object->toolPaint
                                                          : object->supportPaint;

    const int changedFacets = next_layer::paintSphere(*object->mesh, data, centre,
                                                      float(radius), paintKind, erase, extruder,
                                                      facetHint);
    if (changedFacets == 0)
        return 0;

    pushUndo(QStringLiteral("%1: paint").arg(object->displayName()), object->id * 64 + 1);
    changed();
    emit geometryChanged();
    return changedFacets;
}

void PlateDocument::clearPaint(int id, const QString& kind)
{
    Object* object = find(id);
    if (object == nullptr)
        return;

    auto& data = kind == QLatin1String("seam") ? object->seamPaint
               : kind == QLatin1String("tool") ? object->toolPaint
                                              : object->supportPaint;
    if (next_layer::paintIsEmpty(data))
        return;
    pushUndo(QStringLiteral("%1: clear %2 paint").arg(object->displayName(), kind));
    data = {};
    changed();
    emit geometryChanged();
}

int PlateDocument::paintedFacetCount(int id, const QString& kind) const
{
    const Object* object = find(id);
    if (object == nullptr)
        return 0;
    const auto& data = kind == QLatin1String("seam") ? object->seamPaint
                     : kind == QLatin1String("tool") ? object->toolPaint
                                                    : object->supportPaint;
    return static_cast<int>(data.triangles_to_split.size());
}

QByteArray PlateDocument::settingsToJson(const QVariantMap& settings)
{
    if (settings.isEmpty())
        return {};

    nlohmann::ordered_json json = nlohmann::ordered_json::object();
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        const QVariant& value = it.value();
        if (!value.isValid())
            continue;

        if (value.typeId() == QMetaType::QVariantMap) {
            const QVariantMap object = value.toMap();
            nlohmann::ordered_json entry = nlohmann::ordered_json::object();
            entry["value"] = object.value(QStringLiteral("value")).toDouble();
            entry["is_percent"] = object.value(QStringLiteral("is_percent"), false).toBool();
            json[it.key().toStdString()] = entry;
        } else if (value.typeId() == QMetaType::Bool) {
            json[it.key().toStdString()] = value.toBool();
        } else if (value.typeId() == QMetaType::Int || value.typeId() == QMetaType::LongLong) {
            json[it.key().toStdString()] = value.toInt();
        } else if (value.typeId() == QMetaType::Double || value.typeId() == QMetaType::Float) {
            json[it.key().toStdString()] = value.toDouble();
        } else {
            json[it.key().toStdString()] = value.toString().toStdString();
        }
    }
    return QByteArray::fromStdString(json.dump());
}

QVariantList PlateDocument::layerGcodes() const
{
    QVariantList list;
    for (const LayerGcode& entry : m_layerGcodes) {
        QVariantMap info;
        info.insert(QStringLiteral("z"), entry.z);
        info.insert(QStringLiteral("type"), entry.type);
        info.insert(QStringLiteral("text"), entry.text);
        list.append(info);
    }
    return list;
}

bool PlateDocument::addLayerGcode(double z, const QString& type, const QString& text)
{
    if (z <= 0.0)
        return false;
    // One marker per height: adding at a height that already carries one
    // replaces it, which is what a right-click menu implies.
    const auto existing = std::find_if(m_layerGcodes.begin(), m_layerGcodes.end(),
                                       [z](const LayerGcode& entry) {
                                           return std::abs(entry.z - z) < 0.001;
                                       });
    pushUndo(QStringLiteral("Add custom G-code"));
    if (existing != m_layerGcodes.end())
        m_layerGcodes.erase(existing);
    LayerGcode entry;
    entry.z = z;
    entry.type = type;
    entry.text = text;
    m_layerGcodes.push_back(entry);
    std::sort(m_layerGcodes.begin(), m_layerGcodes.end(),
              [](const LayerGcode& a, const LayerGcode& b) { return a.z < b.z; });
    changed();
    emit layerGcodesChanged();
    return true;
}

bool PlateDocument::removeLayerGcode(double z)
{
    const auto existing = std::find_if(m_layerGcodes.begin(), m_layerGcodes.end(),
                                       [z](const LayerGcode& entry) {
                                           return std::abs(entry.z - z) < 0.001;
                                       });
    if (existing == m_layerGcodes.end())
        return false;
    pushUndo(QStringLiteral("Remove custom G-code"));
    m_layerGcodes.erase(existing);
    changed();
    emit layerGcodesChanged();
    return true;
}

void PlateDocument::clearLayerGcodes()
{
    if (m_layerGcodes.empty())
        return;
    pushUndo(QStringLiteral("Clear custom G-code"));
    m_layerGcodes.clear();
    changed();
    emit layerGcodesChanged();
}

QVector<next_layer::PlateLayerGcode> PlateDocument::layerGcodeList() const
{
    QVector<next_layer::PlateLayerGcode> list;
    list.reserve(int(m_layerGcodes.size()));
    for (const LayerGcode& entry : m_layerGcodes) {
        next_layer::PlateLayerGcode out;
        out.z = entry.z;
        out.type = entry.type;
        out.text = entry.text;
        list.push_back(out);
    }
    return list;
}

QVector<next_layer::PlateSliceObject> PlateDocument::sliceObjects() const
{
    return sliceObjectsFor(m_activePlate);
}

QVector<next_layer::PlateSliceObject> PlateDocument::sliceObjectsFor(int plate) const
{
    QVector<next_layer::PlateSliceObject> objects;
    objects.reserve(m_objects.size());
    for (const Object& object : m_objects) {
        if (object.plate != plate || !object.visible || object.mesh == nullptr)
            continue;
        next_layer::PlateSliceObject slice;
        slice.name = object.displayName();
        slice.mesh = worldMesh(object.id);
        slice.settingsJson = settingsToJson(object.settings);
        slice.layerHeights = object.layerHeights;
        slice.supportPaint = object.supportPaint;
        slice.seamPaint = object.seamPaint;
        slice.toolPaint = object.toolPaint;
        for (const Volume& volume : object.volumes) {
            if (!volume.visible || volume.mesh == nullptr)
                continue;
            next_layer::PlateSliceVolume extra;
            extra.mesh = *volume.mesh;
            extra.mesh.transform(volume.worldMatrix(object.trafo));
            extra.type = volume.type;
            extra.settingsJson = settingsToJson(volume.settings);
            slice.volumes.push_back(std::move(extra));
        }
        objects.push_back(std::move(slice));
    }
    return objects;
}

QVector<next_layer::PlateSliceObject> PlateDocument::projectObjects() const
{
    QVector<next_layer::PlateSliceObject> objects;
    objects.reserve(m_objects.size());
    for (const Object& object : m_objects) {
        if (!object.visible || object.mesh == nullptr)
            continue;
        next_layer::PlateSliceObject slice;
        slice.name = object.displayName();
        slice.plate = object.plate;
        slice.mesh = *object.mesh;
        slice.transform = object.trafo;
        slice.settingsJson = settingsToJson(object.settings);
        slice.layerHeights = object.layerHeights;
        slice.supportPaint = object.supportPaint;
        slice.seamPaint = object.seamPaint;
        slice.toolPaint = object.toolPaint;
        for (const Volume& volume : object.volumes) {
            if (!volume.visible || volume.mesh == nullptr)
                continue;
            next_layer::PlateSliceVolume extra;
            extra.mesh = *volume.mesh;
            extra.mesh.transform(volume.trafo.get_matrix());
            extra.type = volume.type;
            extra.settingsJson = settingsToJson(volume.settings);
            slice.volumes.push_back(std::move(extra));
        }
        objects.push_back(std::move(slice));
    }
    return objects;
}

void PlateDocument::setPrintSetup(const QString& printer, const QString& process,
                                  const QString& filament)
{
    if (printer == m_printer && process == m_process && filament == m_filament)
        return;
    m_printer = printer;
    m_process = process;
    m_filament = filament;
    emit printSetupChanged();
}

QString PlateDocument::saveProject(const QString& path)
{
    if (path.isEmpty())
        return QStringLiteral("No file to write");

    std::vector<next_layer::ProjectObject> objects;
    objects.reserve(m_objects.size());
    for (const Object& object : m_objects) {
        if (object.mesh == nullptr)
            continue;
        next_layer::ProjectObject entry;
        entry.name = object.displayName();
        entry.sourcePath = object.path;
        entry.plate = object.plate;
        entry.visible = object.visible;
        entry.mesh = *object.mesh;
        entry.transform = object.trafo;
        entry.settings = object.settings;
        entry.layerHeights = object.layerHeights;
        entry.volumes.reserve(object.volumes.size());
        for (const Volume& volume : object.volumes) {
            if (volume.mesh == nullptr)
                continue;
            next_layer::ProjectVolume stored;
            stored.name = volume.name;
            stored.type = volumeTypeName(volume.type);
            stored.visible = volume.visible;
            stored.mesh = *volume.mesh;
            stored.transform = volume.trafo;
            entry.volumes.push_back(std::move(stored));
        }
        entry.supportPaint = object.supportPaint;
        entry.seamPaint = object.seamPaint;
        entry.toolPaint = object.toolPaint;
        objects.push_back(std::move(entry));
    }
    if (objects.empty())
        return QStringLiteral("There is nothing on the plate to save");

    next_layer::ProjectMetadata metadata;
    metadata.applicationVersion = next_layer::applicationVersion();
    metadata.printer = m_printer;
    metadata.process = m_process;
    metadata.filament = m_filament;
    metadata.activePlate = m_activePlate;
    metadata.plateCount = plateCount();
    for (int i = 0; i < plateCount(); ++i) {
        metadata.plateNames.append(i < m_plateNames.size() ? m_plateNames[i] : QString());
        metadata.plateLocked.append(plateLocked(i));
    }
    for (const QColor& colour : m_toolColours)
        metadata.toolColours.append(colour.name(QColor::HexRgb));
    for (const LayerGcode& entry : m_layerGcodes) {
        next_layer::ProjectLayerGcode marker;
        marker.z = entry.z;
        marker.type = entry.type;
        marker.text = entry.text;
        metadata.layerGcodes.push_back(std::move(marker));
    }

    const QByteArray body = next_layer::serialiseProject(objects, metadata);
    if (body.isEmpty())
        return QStringLiteral("Could not serialise the plate");

    QFile file(path);
    // eslint-disable-next-line
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("Could not write %1").arg(path);
    const bool written = file.write(body) == body.size();
    file.close();
    if (!written)
        return QStringLiteral("Could not write all of %1").arg(path);

    m_layerGcodes.clear();
    for (const next_layer::ProjectLayerGcode& marker : metadata.layerGcodes) {
        LayerGcode entry;
        entry.z = marker.z;
        entry.type = marker.type;
        entry.text = marker.text;
        m_layerGcodes.push_back(entry);
    }
    emit layerGcodesChanged();

    m_projectPath = path;
    m_modified = false;
    emit modifiedChanged();
    emit projectPathChanged();
    return {};
}


QString PlateDocument::loadProject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("Could not read %1").arg(path);
    const QByteArray body = file.readAll();
    file.close();

    std::vector<next_layer::ProjectObject> objects;
    next_layer::ProjectMetadata metadata;
    QString error;
    if (!next_layer::parseProject(body, objects, metadata, error))
        return error;

    pushUndo(QStringLiteral("Open project"));

    m_objects.clear();
    m_selectedIds.clear();
    m_nextId = 1;
    for (next_layer::ProjectObject& entry : objects) {
        Object object;
        object.id = m_nextId++;
        object.plate = entry.plate;
        object.visible = entry.visible;
        object.path = entry.sourcePath;
        object.name = entry.name;
        auto mesh = std::make_shared<TriangleMesh>(std::move(entry.mesh));
        if (!meshBounds(*mesh, object.localMin, object.localMax)) {
            m_objects.clear();
            m_selectedIds.clear();
            return QStringLiteral("\"%1\" has an empty mesh").arg(entry.name);
        }
        object.mesh = mesh;
        object.trafo = entry.transform;
        object.settings = entry.settings;
        object.layerHeights = entry.layerHeights;
        object.supportPaint = entry.supportPaint;
        object.seamPaint = entry.seamPaint;
        object.toolPaint = entry.toolPaint;
        object.volumes.reserve(entry.volumes.size());
        for (next_layer::ProjectVolume& stored : entry.volumes) {
            Volume volume;
            volume.id = m_nextId++;
            volume.name = stored.name.isEmpty() ? QStringLiteral("Volume") : stored.name;
            volume.type = volumeTypeFromName(stored.type);
            volume.visible = stored.visible;
            volume.trafo = stored.transform;
            auto volumeMesh = std::make_shared<TriangleMesh>(std::move(stored.mesh));
            if (!meshBounds(*volumeMesh, volume.localMin, volume.localMax)) {
                m_objects.clear();
                m_selectedIds.clear();
                return QStringLiteral("a volume of \"%1\" has an empty mesh").arg(entry.name);
            }
            volume.mesh = volumeMesh;
            object.volumes.push_back(std::move(volume));
        }
        m_objects.push_back(std::move(object));
    }

    m_plateCount = std::max(1, metadata.plateCount);
    m_plateNames = metadata.plateNames;
    m_plateLocked = metadata.plateLocked;
    while (m_plateNames.size() < m_plateCount)
        m_plateNames.append(QString());
    while (m_plateLocked.size() < m_plateCount)
        m_plateLocked.append(false);

    m_activePlate = std::clamp(metadata.activePlate, 0, std::max(0, plateCount() - 1));
    if (!m_objects.empty() && m_objects.front().plate == m_activePlate)
        m_selectedIds = {m_objects.front().id};

    if (!metadata.toolColours.isEmpty()) {
        QVariantList colours;
        for (const QString& colour : metadata.toolColours)
            colours.append(colour);
        setToolColours(colours);
    }

    m_projectPath = path;
    m_error.clear();
    emit errorChanged();
    emit plateChanged();
    emit projectPathChanged();
    changed();
    m_modified = false;
    emit modifiedChanged();
    emit selectionChanged();
    emit geometryChanged();
    return {};
}

QVector3D PlateDocument::objectBoundsMin(int id) const
{
    const Object* object = find(id);
    QVector3D low;
    QVector3D high;
    if (object == nullptr || object->mesh == nullptr
        || !boxOf(*object->mesh, object->trafo, object->localMin, object->localMax, low, high))
        return QVector3D();
    return low;
}

QVector3D PlateDocument::objectBoundsMax(int id) const
{
    const Object* object = find(id);
    QVector3D low;
    QVector3D high;
    if (object == nullptr || object->mesh == nullptr
        || !boxOf(*object->mesh, object->trafo, object->localMin, object->localMax, low, high))
        return QVector3D();
    return high;
}

QVector3D PlateDocument::boundsMin() const
{
    QVector3D result;
    bool first = true;
    for (const Object& object : m_objects) {
        if (object.plate != m_activePlate)
            continue;
        const QVector3D low = objectBoundsMin(object.id);
        result = first ? low : QVector3D(std::min(result.x(), low.x()),
                                         std::min(result.y(), low.y()),
                                         std::min(result.z(), low.z()));
        first = false;
    }
    return result;
}

QVector3D PlateDocument::boundsMax() const
{
    QVector3D result;
    bool first = true;
    for (const Object& object : m_objects) {
        if (object.plate != m_activePlate)
            continue;
        const QVector3D high = objectBoundsMax(object.id);
        result = first ? high : QVector3D(std::max(result.x(), high.x()),
                                          std::max(result.y(), high.y()),
                                          std::max(result.z(), high.z()));
        first = false;
    }
    return result;
}

QByteArray PlateDocument::fingerprint() const
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const Object& object : m_objects) {
        hash.addData(object.path.toUtf8());
        const Vec3d offset = object.trafo.get_offset();
        const Vec3d rotation = object.trafo.get_rotation();
        const Vec3d scale = object.trafo.get_scaling_factor();
        const double values[9] = {offset.x(), offset.y(), offset.z(),
                                  rotation.x(), rotation.y(), rotation.z(),
                                  scale.x(), scale.y(), scale.z()};
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(values), sizeof(values)));
        hash.addData(object.visible ? "v" : "h");
        for (const Volume& volume : object.volumes) {
            const QByteArray type = volumeTypeName(volume.type).toUtf8();
            hash.addData(type);
            hash.addData(volume.visible ? "v" : "h");
            const Vec3d volumeOffset = volume.trafo.get_offset();
            const double values[3] = {volumeOffset.x(), volumeOffset.y(), volumeOffset.z()};
            hash.addData(QByteArrayView(reinterpret_cast<const char*>(values), sizeof(values)));
        }
    }
    return hash.result();
}

Slic3r::Domain::TriangleMesh PlateDocument::worldMesh(int id) const
{
    const Object* object = find(id);
    if (object == nullptr || object->mesh == nullptr)
        return Slic3r::Domain::TriangleMesh();
    Slic3r::Domain::TriangleMesh mesh = *object->mesh;
    mesh.transform(object->trafo.get_matrix());
    return mesh;
}

QString PlateDocument::stagedPlatePath()
{
    // The engine's file slice path takes one project per input file, so the whole
    // plate goes in as one STL with every object baked to its world position:
    // separate shells on one bed, which slices to the same toolpaths as separate
    // objects while per-object settings do not exist yet.
    TriangleMesh plate;
    for (const Object& object : m_objects) {
        if (!object.visible || object.mesh == nullptr)
            continue;
        TriangleMesh world = *object.mesh;
        world.transform(object.trafo.get_matrix());
        appendMesh(plate, world);
    }

    if (plate.its.indices.empty())
        return QString();

    if (!m_scratch)
        m_scratch = next_layer::makeScratchDir();
    if (!m_scratch->isValid()) {
        fail(QStringLiteral("could not create a scratch directory for slicing"));
        return QString();
    }

    const QString path = m_scratch->filePath(
        QStringLiteral("plate-%1.stl").arg(++m_stagedRevision));
    if (!Slic3r::Biz::store_stl(path.toStdString(), plate, true, "next-layer")) {
        fail(QStringLiteral("could not stage the plate for slicing"));
        return QString();
    }
    m_stagedPlates.append(path);
    // A staged plate is megabytes, this runs again on every edit, and a long
    // session was filling the scratch directory with every revision it ever
    // made. The previous one is kept because a slice in flight may still hold
    // it; anything older has no reader left.
    while (m_stagedPlates.size() > 2)
        QFile::remove(m_stagedPlates.takeFirst());
    return path;
}
