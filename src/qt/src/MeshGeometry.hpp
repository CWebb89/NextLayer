#pragma once

#include <QQuick3DGeometry>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

namespace Slic3r::Domain {
class TriangleMesh;
}

// Loaded STL as an interleaved position+normal mesh in printer coordinates:
// millimetres, Z-up, minimum Z at the bed plane, and the model's XY centre moved
// onto the bed centre. No scaling -- the bed grid, the toolpath and the model all
// share one space, and the camera fits itself to boundsMin/boundsMax.
class MeshGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    Q_PROPERTY(QVector3D boundsMin READ boundsMin NOTIFY boundsChanged)
    Q_PROPERTY(QVector3D boundsMax READ boundsMax NOTIFY boundsChanged)

public:
    explicit MeshGeometry(QQuick3DObject* parent = nullptr);

    void setMesh(const Slic3r::Domain::TriangleMesh& mesh);

    /// The same mesh with one colour per triangle, for the painted-facet overlay.
    /// Colours are straight RGBA in 0..1; a triangle with no colour is skipped, so
    /// an empty colour vector draws nothing rather than an unpainted copy of the
    /// object on top of itself.
    void setColoredMesh(const Slic3r::Domain::TriangleMesh& mesh,
                        const QVector<QVector4D>& colors);

    QVector3D boundsMin() const { return m_min; }
    QVector3D boundsMax() const { return m_max; }

    // Nearest triangle hit for a ray in printer coordinates. The returned w is
    // the ray parameter; a negative w means the ray missed the mesh.
    Q_INVOKABLE QVector4D raycast(const QVector3D& origin, const QVector3D& direction) const;

signals:
    void boundsChanged();

private:
    QVector<float> m_positions;
    QVector3D m_min;
    QVector3D m_max;
};
