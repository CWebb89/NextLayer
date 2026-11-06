#include "Session.hpp"

#include "Host.hpp"
#include "MeshGeometry.hpp"
#include "PlateDocument.hpp"
#include "SliceController.hpp"

#include <QVector3D>
#include <QVariantList>

#include <Slic3r/Domain/TriangleMesh.hpp>

#include <cmath>
#include <string>

Session::Session(MeshGeometry* geometry, next_layer::Host* host, SliceController* slicer,
                 PlateDocument* document, QObject* parent)
    : QObject(parent)
    , m_geometry(geometry)
    , m_host(host)
    , m_slicer(slicer)
    , m_document(document)
{
    QObject::connect(m_document, &PlateDocument::objectsChanged, this, &Session::refresh);
}

bool Session::hasModel() const
{
    return !m_document->empty();
}

QString Session::path() const
{
    if (m_document->empty())
        return QString();
    const PlateDocument::Object* object = m_document->object(m_document->ids().first());
    return object == nullptr ? QString() : object->path;
}

QString Session::error() const
{
    return m_document->error();
}

QString Session::displayName() const
{
    if (m_document->empty())
        return QStringLiteral("no model");
    if (m_document->count() > 1)
        return QStringLiteral("%1 objects").arg(m_document->count());
    const PlateDocument::Object* object = m_document->object(m_document->ids().first());
    return object == nullptr ? QStringLiteral("no model") : object->displayName();
}

bool Session::open(const QString& path)
{
    m_document->clear();
    const bool added = m_document->addObject(path) > 0;
    refresh();
    return added;
}

bool Session::openTransformed(const QString& path, const QString& label, bool placeOnBed)
{
    m_document->clear();
    const int id = placeOnBed ? m_document->addObject(path, label)
                              : m_document->addObjectAtFilePosition(path, label);
    refresh();
    return id > 0;
}

int Session::addModels(const QStringList& paths)
{
    int added = 0;
    for (const QString& path : paths) {
        if (m_document->addObject(path) > 0)
            ++added;
    }
    if (added > 0)
        refresh();
    return added;
}

void Session::clear()
{
    m_document->clear();
    refresh();
}

void Session::refresh()
{
    // The active plate is what this session shows, not the whole document: a
    // document whose other plates hold objects is still empty here, and asking
    // for `ids().first()` on an empty plate was a segfault the first time anyone
    // added a plate.
    const QVector<int> ids = m_document->ids();
    if (ids.isEmpty()) {
        m_geometry->setMesh(Slic3r::Domain::TriangleMesh{});
        m_slicer->setModelPath(std::string());
        // The same three-element shape the non-empty branch passes: a short list
        // used to leave the camera reading a missing component.
        m_host->setModel(QString(),
                         QVariantList{m_document->boundsMin().x(), 0.0,
                                      -m_document->boundsMin().y()},
                         0.0);
        emit modelChanged();
        return;
    }

    const int selected = m_document->selectedId();
    const int id = ids.contains(selected) ? selected : ids.first();
    const PlateDocument::Object* object = m_document->object(id);

    m_geometry->setMesh(*object->mesh);

    const QVector3D plateLow = m_document->boundsMin();
    const QVector3D plateHigh = m_document->boundsMax();
    const QVector3D extent = plateHigh - plateLow;
    const double radius = 0.5 * std::sqrt(double(QVector3D::dotProduct(extent, extent)));

    m_slicer->setModelPath(m_document->stagedPlatePath().toStdString());

    m_host->setModel(object == nullptr ? QString() : object->displayName(),
                     QVariantList{(plateLow.x() + plateHigh.x()) * 0.5,
                                  (plateLow.z() + plateHigh.z()) * 0.5,
                                  -(plateLow.y() + plateHigh.y()) * 0.5},
                     radius);
    emit modelChanged();
}
