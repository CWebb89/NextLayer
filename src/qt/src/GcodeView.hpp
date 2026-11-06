#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickPaintedItem>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector3D>

#include <memory>

namespace Slic3r::App::libvgcode {
struct FdmViewerInputData;
}

// Renders the sliced toolpath with libvgcode, the renderer PrusaSlicer's own
// preview is built on. The QML Quick 3D scene keeps the bed grid and the STL
// model; this item draws the toolpath on top with a camera synced to that scene.
//
// libvgcode is built on GLEW and needs desktop OpenGL, while the scene graph
// runs an OpenGL ES context on this machine, so the viewer lives in its own
// software desktop-GL context and the result is composited as a raster image.
class GcodeView : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QVector3D cameraEye READ cameraEye WRITE setCameraEye)
    Q_PROPERTY(QVector3D cameraCenter READ cameraCenter WRITE setCameraCenter)
    Q_PROPERTY(QVector3D cameraUp READ cameraUp WRITE setCameraUp)
    Q_PROPERTY(qreal fov READ fov WRITE setFov)
    Q_PROPERTY(bool initialized READ initialized NOTIFY initializedChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY frameCountChanged)

public:
    explicit GcodeView(QQuickItem* parent = nullptr);
    ~GcodeView() override;

    void paint(QPainter* painter) override;

    QVector3D cameraEye() const { return m_cameraEye; }
    void setCameraEye(const QVector3D& eye);
    QVector3D cameraCenter() const { return m_cameraCenter; }
    void setCameraCenter(const QVector3D& center);
    QVector3D cameraUp() const { return m_cameraUp; }
    void setCameraUp(const QVector3D& up);
    qreal fov() const { return m_fov; }
    void setFov(qreal fov);

    bool initialized() const { return m_initialized; }
    int frameCount() const;

signals:
    void initializedChanged();
    void frameCountChanged();
    void contentsLoaded();
    void legendChanged();

public:

    void setInput(Slic3r::App::libvgcode::FdmViewerInputData&& input);
    void setLayerRange(int minLayer, int maxLayer);
    void setVisibleMoveRange(int minMove, int maxMove);
    void setRoleVisibility(int role, bool visible);
    void setViewType(const QString& name);
    void setOptionVisible(const QString& name, bool visible);
    QString viewType() const { return m_viewTypeName; }
    QVariantList optionLegend() const;
    QVariantList rangeLegend() const;
    static QVariantList viewTypeChoices();
    void clearContents();

private:
    void scheduleRender();
    void scheduleSettledRender();

    class Impl;
    std::unique_ptr<Impl> m_impl;

    QVector3D m_cameraEye{0.0f, 0.0f, 400.0f};
    QVector3D m_cameraCenter{0.0f, 0.0f, 0.0f};
    QVector3D m_cameraUp{0.0f, 1.0f, 0.0f};
    qreal m_fov = 60.0;

    std::unique_ptr<Slic3r::App::libvgcode::FdmViewerInputData> m_pendingInput;
    int m_layerMin = 0;
    int m_layerMax = 0;
    bool m_layerDirty = false;
    int m_moveMin = 0;
    int m_moveMax = -1;
    bool m_moveDirty = false;
    QSet<int> m_hiddenRoles;
    QSet<QString> m_hiddenOptions;
    QString m_viewTypeName;
    bool m_viewTypeDirty = false;
    bool m_legendDirty = false;
    bool m_clearPending = false;

    QImage m_frame;
    mutable QMutex m_frameMutex;
    QTimer m_renderTimer;
    QTimer m_qualityTimer;
    bool m_qualityPending = false;
    bool m_initialized = false;
    int m_frameCount = 0;
    bool m_renderScheduled = false;
};
