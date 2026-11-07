// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include <QObject>
#include <QVector3D>
#include <QVector4D>

class MeshGeometry;
class PlateDocument;
class QQuick3DGeometry;

// The plate as the viewport draws it: one mesh for every unselected object and
// one for the selected object, so the accent tint is a property of the draw and
// never a second copy of the same triangles on top of itself. Ray picking runs
// against the document's own triangles, so it does not depend on what has been
// uploaded to the GPU.
class PlateScene : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQuick3DGeometry* plateGeometry READ plateGeometry NOTIFY geometryChanged)
    /// Every plate that is not the active one, drawn dimmed: the plate you are
    /// working on should be the one that reads as active, and only that one is
    /// sliced.
    Q_PROPERTY(QQuick3DGeometry* otherPlateGeometry READ otherPlateGeometry NOTIFY geometryChanged)
    Q_PROPERTY(bool hasOtherPlates READ hasOtherPlates NOTIFY geometryChanged)
    Q_PROPERTY(QQuick3DGeometry* selectionGeometry READ selectionGeometry NOTIFY geometryChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY geometryChanged)
    Q_PROPERTY(QQuick3DGeometry* paintGeometry READ paintGeometry NOTIFY geometryChanged)
    Q_PROPERTY(QQuick3DGeometry* volumeGeometry READ volumeGeometry NOTIFY geometryChanged)
    Q_PROPERTY(bool hasVolumes READ hasVolumes NOTIFY geometryChanged)
    Q_PROPERTY(QQuick3DGeometry* bedMarkerGeometry READ bedMarkerGeometry NOTIFY geometryChanged)
    Q_PROPERTY(QQuick3DGeometry* gizmoGeometry READ gizmoGeometry NOTIFY geometryChanged)
    Q_PROPERTY(QVector3D gizmoOrigin READ gizmoOrigin NOTIFY geometryChanged)
    Q_PROPERTY(bool hasGizmo READ hasGizmo NOTIFY geometryChanged)
    Q_PROPERTY(int pickedId READ pickedId NOTIFY pickedIdChanged)
    Q_PROPERTY(int pickedFacet READ pickedFacet NOTIFY pickedIdChanged)

public:
    explicit PlateScene(PlateDocument* document, QObject* parent = nullptr);

    QQuick3DGeometry* plateGeometry() const;
    QQuick3DGeometry* otherPlateGeometry() const;
    bool hasOtherPlates() const { return m_hasOtherPlates; }
    QQuick3DGeometry* selectionGeometry() const;
    QQuick3DGeometry* paintGeometry() const;
    /// Modifier, negative and support volumes of every visible object, each
    /// triangle coloured by the role its volume plays.
    QQuick3DGeometry* volumeGeometry() const;
    /// Whether the selected object carries modifier volumes, which is what makes
    /// the object draw as a ghost so they can be seen inside it.
    bool hasVolumes() const { return m_hasVolumes; }
    /// A thin slab the size of every plate that is not the active one, so a
    /// multi-plate document reads as plates rather than as objects floating in
    /// space. Empty for a single-plate document.
    QQuick3DGeometry* bedMarkerGeometry() const;
    bool hasSelection() const;
    int pickedId() const { return m_pickedId; }
    int pickedFacet() const { return m_pickedFacet; }

    Q_INVOKABLE QVector4D pick(const QVector3D& origin, const QVector3D& direction);

    /// The move handles: three arrows at the selection's centre, in printer
    /// coordinates. They exist only when a single object is selected, because a
    /// handle whose drag means "move all of them" is a trap.
    QQuick3DGeometry* gizmoGeometry() const;
    QVector3D gizmoOrigin() const { return m_gizmoOrigin; }
    bool hasGizmo() const { return m_hasGizmo; }
    /// Which axis the ray hits: 0 for X, 1 for Y, 2 for Z, -1 for none.
    Q_INVOKABLE int pickGizmo(const QVector3D& origin, const QVector3D& direction);
    /// The point on that axis the ray passes closest to, which is what a drag
    /// measures its travel against.
    Q_INVOKABLE QVector3D gizmoAxisPoint(int axis, const QVector3D& origin,
                                         const QVector3D& direction);

signals:
    void geometryChanged();
    void pickedIdChanged();

private:
    void rebuild();
    void rebuildGizmo();
    float gizmoReach() const;
    QVector3D gizmoAxisStart(int axis) const;

    PlateDocument* m_document;
    MeshGeometry* m_plate;
    MeshGeometry* m_otherPlate;
    MeshGeometry* m_selection;
    MeshGeometry* m_paint;
    MeshGeometry* m_volumes;
    MeshGeometry* m_plateMarkers;
    MeshGeometry* m_gizmo;
    QVector3D m_gizmoOrigin;
    bool m_hasGizmo = false;
    bool m_hasSelection = false;
    bool m_hasOtherPlates = false;
    bool m_hasVolumes = false;
    int m_pickedId = 0;
    int m_pickedFacet = -1;
};
