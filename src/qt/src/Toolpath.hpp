#pragma once

#include "PreviewData.hpp"

#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <memory>

class GcodeView;

// Data facade for the toolpath preview: layers, legend and print statistics,
// parsed from the G-code through the engine's own processor. Rendering is
// delegated to GcodeView, which draws the same data with libvgcode.
class Toolpath : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int layerCount READ layerCount NOTIFY changed)
    Q_PROPERTY(int currentLayer READ currentLayer NOTIFY changed)
    Q_PROPERTY(int firstLayer READ firstLayer NOTIFY changed)
    Q_PROPERTY(int moveCount READ moveCount NOTIFY changed)
    Q_PROPERTY(int layerMoveCount READ layerMoveCount NOTIFY changed)
    Q_PROPERTY(int firstMove READ firstMove NOTIFY changed)
    Q_PROPERTY(int lastMove READ lastMove NOTIFY changed)
    Q_PROPERTY(double layerZ READ layerZ NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(QVariantList featureLegend READ featureLegend NOTIFY changed)
    Q_PROPERTY(QVariantList viewTypes READ viewTypes CONSTANT)
    Q_PROPERTY(QString viewType READ viewType NOTIFY changed)
    Q_PROPERTY(QVariantList rangeLegend READ rangeLegend NOTIFY changed)
    Q_PROPERTY(QVariantList optionLegend READ optionLegend NOTIFY changed)
    Q_PROPERTY(double totalSeconds READ totalSeconds NOTIFY changed)
    Q_PROPERTY(double totalFilamentMm READ totalFilamentMm NOTIFY changed)
    Q_PROPERTY(double totalFilamentG READ totalFilamentG NOTIFY changed)
    Q_PROPERTY(double totalCost READ totalCost NOTIFY changed)
    Q_PROPERTY(int layerMode READ layerMode NOTIFY changed)
    Q_PROPERTY(QVariantList gcodeWindow READ gcodeWindow NOTIFY changed)
    Q_PROPERTY(int gcodeLine READ gcodeLine NOTIFY changed)

public:
    explicit Toolpath(const QVariantMap& palette, QObject* parent = nullptr);
    ~Toolpath() override;

    Q_INVOKABLE void attachView(GcodeView* view);
    Q_INVOKABLE bool load(const QString& gcodePath);
    Q_INVOKABLE void setLayer(int index);
    Q_INVOKABLE void setFirstLayer(int index);
    Q_INVOKABLE void setFirstMove(int index);
    Q_INVOKABLE void setLastMove(int index);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void setFeatureVisible(const QString& name, bool visible);
    Q_INVOKABLE void setViewType(const QString& name);
    Q_INVOKABLE void setOptionVisible(const QString& name, bool visible);
    Q_INVOKABLE void setSingleLayer(bool single);
    Q_INVOKABLE void cycleLayerMode();

    // The G-code around the move the preview is currently stopped at, in the
    // form Orca's g-code window shows it: one entry per line with the line's own
    // number, and the line that produced the current move flagged.
    QVariantList gcodeWindow() const;
    int gcodeLine() const;

    int layerCount() const;
    int currentLayer() const;
    int firstLayer() const;
    int moveCount() const;
    int layerMoveCount() const;
    int firstMove() const;
    int lastMove() const;
    double layerZ() const;
    /// The print Z of any layer, and the index of the layer a Z height belongs
    /// to. Custom G-code is stored against a Z, so the slider needs the mapping
    /// rather than the other way round.
    Q_INVOKABLE double layerZAt(int layer) const;
    Q_INVOKABLE int layerAtZ(double z) const;
    bool loaded() const;
    bool singleLayer() const;
    int layerMode() const;
    QVariantList featureLegend() const;
    QVariantList viewTypes() const;
    QString viewType() const;
    QVariantList rangeLegend() const;
    QVariantList optionLegend() const;
    double totalSeconds() const;
    double totalFilamentMm() const;
    double totalFilamentG() const;
    double totalCost() const;

signals:
    void changed();

private:
    void forwardInput(Slic3r::App::libvgcode::FdmViewerInputData&& input);
    void forwardLayerRange();
    void forwardMoveRange();
    void resetMoveRange();
    int layerVertexStart(int layer) const;
    void forwardVisibility(const QString& name, bool visible);
    int currentVertex() const;

    QVariantMap m_palette;
    PreviewData m_data;
    GcodeView* m_view = nullptr;
    std::unique_ptr<Slic3r::App::libvgcode::FdmViewerInputData> m_pendingInput;
    QSet<QString> m_hidden;
    // Orca and PrusaSlicer both open the preview with travel moves off: a plate
    // of toolpaths is what the print is, and the travels between them are noise
    // until someone asks to see them.
    QSet<QString> m_hiddenOptions = {QStringLiteral("Travel")};
    QString m_viewType;
    int m_currentLayer = 0;
    int m_firstLayer = 0;
    int m_firstMove = 0;
    int m_lastMove = -1;
    struct LayerVertexRange
    {
        int start = 0;
        int count = 0;
    };
    QVector<LayerVertexRange> m_layerVertexRanges;
    QVector<uint32_t> m_vertexGcodeIds;
    QStringList m_gcodeLines;
    bool m_singleLayer = false;
    int m_layerMode = 0;
    bool m_loaded = false;
};
