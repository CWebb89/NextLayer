// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include "Host.hpp"
#include "MeshGeometry.hpp"
#include "PlateDocument.hpp"
#include "SliceController.hpp"

#include "next-layer-app/src/bridge.rs.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QVector3D>

#include <Slic3r/Domain/TriangleMesh.hpp>

#include <cstdint>
#include <string>

namespace next_layer {
class Host;
}

class Session : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasModel READ hasModel NOTIFY modelChanged)
    Q_PROPERTY(QString path READ path NOTIFY modelChanged)
    Q_PROPERTY(QString error READ error NOTIFY modelChanged)

public:
    Session(MeshGeometry* geometry, next_layer::Host* host, SliceController* slicer,
            PlateDocument* document, QObject* parent = nullptr)
        : QObject(parent)
        , m_geometry(geometry)
        , m_host(host)
        , m_slicer(slicer)
        , m_document(document)
    {
        QObject::connect(m_document, &PlateDocument::objectsChanged, this, &Session::refresh);
        refresh();
    }

    bool hasModel() const { return m_view.has_model; }
    QString path() const { return textOf(m_view.path); }
    QString error() const { return textOf(m_view.error); }

    Q_INVOKABLE bool open(const QString& path)
    {
        m_document->clear();
        const bool added = m_document->addObject(path) > 0;
        refresh();
        return added;
    }

    Q_INVOKABLE int addModels(const QStringList& paths)
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

    Q_INVOKABLE bool openTransformed(const QString& path, const QString& label,
                                     bool placeOnBed = true)
    {
        m_document->clear();
        const int id = placeOnBed ? m_document->addObject(path, label)
                                  : m_document->addObjectAtFilePosition(path, label);
        refresh();
        return id > 0;
    }

    Q_INVOKABLE void clear()
    {
        m_document->clear();
        refresh();
    }

    Q_INVOKABLE QString displayName() const { return textOf(m_view.display_name); }

signals:
    void modelChanged();

private:
    /// Rebuilds the view from the document: one call into Rust for the object, the
    /// name, the path and the framing, then the three Qt pushes it decides.
    void refresh()
    {
        const QVector<int> plateIds = m_document->ids();
        rust::Vec<int64_t> ids;
        rust::Vec<rust::String> paths;
        rust::Vec<rust::String> names;
        collect(plateIds, ids, paths, names);

        const QVector3D plateLow = m_document->boundsMin();
        const QVector3D plateHigh = m_document->boundsMax();
        m_view = next_layer::session_view(
            m_document->empty(), rust::Slice<const int64_t>(ids.data(), ids.size()),
            m_document->count(), m_document->selectedId(),
            rust::Slice<const rust::String>(paths.data(), paths.size()),
            rust::Slice<const rust::String>(names.data(), names.size()),
            {plateLow.x(), plateLow.y(), plateLow.z()},
            {plateHigh.x(), plateHigh.y(), plateHigh.z()},
            m_document->error().toStdString());

        const PlateDocument::Object* object = m_document->object(m_view.object);
        if (plateIds.isEmpty()) {
            m_geometry->setMesh(Slic3r::Domain::TriangleMesh{});
            m_slicer->setModelPath(std::string());
        } else {
            m_geometry->setMesh(*object->mesh);
            m_slicer->setModelPath(m_document->stagedPlatePath().toStdString());
        }

        m_host->setModel(object == nullptr ? QString() : object->displayName(),
                         QVariantList{m_view.focus[0], m_view.focus[1], m_view.focus[2]},
                         m_view.radius);
        emit modelChanged();
    }

    /// The plate's objects as their ids, display names and source paths, in the
    /// order the document lists them.
    void collect(const QVector<int>& plateIds, rust::Vec<int64_t>& ids,
                 rust::Vec<rust::String>& paths, rust::Vec<rust::String>& names) const
    {
        const size_t count = size_t(plateIds.size());
        ids.reserve(count);
        paths.reserve(count);
        names.reserve(count);
        for (const int id : plateIds) {
            ids.push_back(int64_t(id));
            const PlateDocument::Object* object = m_document->object(id);
            paths.push_back((object == nullptr ? QString() : object->path).toStdString());
            names.push_back(
                (object == nullptr ? QString() : object->displayName()).toStdString());
        }
    }

    static QString textOf(const rust::String& value)
    {
        return QString::fromStdString(static_cast<std::string>(value));
    }

    MeshGeometry* m_geometry;
    next_layer::Host* m_host;
    SliceController* m_slicer;
    PlateDocument* m_document;
    next_layer::SessionView m_view;
};
