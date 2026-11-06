#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace next_layer {
class Host;
}

class MeshGeometry;
class PlateDocument;
class SliceController;

// The open document, as the rest of the app sees it: a view over PlateDocument
// that keeps the viewport's geometry, the camera framing and the slice
// controller's input in step with the plate. It owns no model state of its own --
// opening a file adds to the document, clearing empties it.
class Session : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasModel READ hasModel NOTIFY modelChanged)
    Q_PROPERTY(QString path READ path NOTIFY modelChanged)
    Q_PROPERTY(QString error READ error NOTIFY modelChanged)

public:
    Session(MeshGeometry* geometry, next_layer::Host* host, SliceController* slicer,
            PlateDocument* document, QObject* parent = nullptr);

    bool hasModel() const;
    QString path() const;
    QString error() const;

    Q_INVOKABLE bool open(const QString& path);
    Q_INVOKABLE int addModels(const QStringList& paths);
    Q_INVOKABLE bool openTransformed(const QString& path, const QString& label,
                                     bool placeOnBed = true);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString displayName() const;

signals:
    void modelChanged();

private:
    void refresh();

    MeshGeometry* m_geometry;
    next_layer::Host* m_host;
    SliceController* m_slicer;
    PlateDocument* m_document;
};
