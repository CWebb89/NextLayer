// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "Toolpath.hpp"

#include "GcodeView.hpp"

#include <Slic3r/App/libvgcode/FdmViewerInputData.hpp>

#include <QColor>
#include <QFile>

#include <algorithm>

Toolpath::Toolpath(const QVariantMap& palette, QObject* parent)
    : QObject(parent)
    , m_palette(palette)
{
}

Toolpath::~Toolpath() = default;

void Toolpath::attachView(GcodeView* view)
{
    m_view = view;
    if (m_view == nullptr)
        return;
    connect(m_view, &GcodeView::contentsLoaded, this, [this] {
        if (!m_viewType.isEmpty())
            m_view->setViewType(m_viewType);
        for (const QString& name : m_hiddenOptions)
            m_view->setOptionVisible(name, false);
        forwardLayerRange();
        emit changed();
    });
    connect(m_view, &GcodeView::legendChanged, this, &Toolpath::changed);
    if (m_pendingInput) {
        m_view->setInput(std::move(*m_pendingInput));
        m_pendingInput.reset();
        forwardLayerRange();
    }
}

void Toolpath::setViewType(const QString& name)
{
    if (m_viewType == name)
        return;
    m_viewType = name;
    if (m_view != nullptr)
        m_view->setViewType(name);
    emit changed();
}

void Toolpath::setOptionVisible(const QString& name, bool visible)
{
    if (visible)
        m_hiddenOptions.remove(name);
    else
        m_hiddenOptions.insert(name);
    if (m_view != nullptr)
        m_view->setOptionVisible(name, visible);
    emit changed();
}

QVariantList Toolpath::viewTypes() const
{
    return GcodeView::viewTypeChoices();
}

QString Toolpath::viewType() const
{
    return m_viewType.isEmpty() ? QStringLiteral("Feature type") : m_viewType;
}

QVariantList Toolpath::rangeLegend() const
{
    return (m_view != nullptr && viewType() != QStringLiteral("Feature type")) ? m_view->rangeLegend()
                                                                              : QVariantList();
}

QVariantList Toolpath::optionLegend() const
{
    return m_view != nullptr ? m_view->optionLegend() : QVariantList();
}

bool Toolpath::load(const QString& gcodePath)
{
    PreviewData data;
    Slic3r::App::libvgcode::FdmViewerInputData input;
    if (!PreviewReader::readForRender(gcodePath, data, input))
        return false;

    // The renderer addresses its vertices, one per G-code move, so a layer's move
    // range is the run of vertices carrying that layer id. Recorded here because
    // this is the only place where both the parsed file and the render input exist.
    m_layerVertexRanges.clear();
    m_vertexGcodeIds.clear();
    if (input.vertices != nullptr) {
        const auto& vertices = *input.vertices;
        m_vertexGcodeIds.resize(int(vertices.size()));
        for (size_t i = 0; i < vertices.size(); ++i) {
            const int layer = int(vertices[i].layer_id);
            m_vertexGcodeIds[int(i)] = vertices[i].gcode_id;
            if (layer < 0)
                continue;
            if (size_t(layer) >= m_layerVertexRanges.size())
                m_layerVertexRanges.resize(size_t(layer) + 1, LayerVertexRange{});
            LayerVertexRange& range = m_layerVertexRanges[size_t(layer)];
            if (range.count == 0)
                range.start = int(i);
            range.count = int(i) - range.start + 1;
        }
    }

    m_data = std::move(data);
    m_gcodeLines.clear();
    QFile file(gcodePath);
    if (file.open(QIODevice::ReadOnly)) {
        m_gcodeLines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        if (!m_gcodeLines.isEmpty() && m_gcodeLines.last().isEmpty())
            m_gcodeLines.removeLast();
    }
    m_loaded = true;
    m_currentLayer = 0;
    m_firstLayer = 0;
    m_firstMove = 0;
    m_lastMove = -1;
    forwardInput(std::move(input));
    emit changed();
    return true;
}

void Toolpath::setLayer(int index)
{
    if (!m_loaded)
        return;
    const int layer = std::clamp(index, 0, layerCount() - 1);
    const bool firstMoved = m_firstLayer > layer;
    if (firstMoved)
        m_firstLayer = layer;
    if (layer == m_currentLayer && !firstMoved)
        return;
    m_currentLayer = layer;
    resetMoveRange();
    forwardLayerRange();
    emit changed();
}

int Toolpath::layerMoveCount() const
{
    if (m_currentLayer < 0 || size_t(m_currentLayer) >= m_layerVertexRanges.size())
        return 0;
    return m_layerVertexRanges[size_t(m_currentLayer)].count;
}

int Toolpath::layerVertexStart(int layer) const
{
    if (layer < 0 || size_t(layer) >= m_layerVertexRanges.size())
        return 0;
    return m_layerVertexRanges[size_t(layer)].start;
}

int Toolpath::firstMove() const
{
    return m_firstMove;
}

int Toolpath::lastMove() const
{
    return m_lastMove >= 0 ? m_lastMove : std::max(0, layerMoveCount() - 1);
}

void Toolpath::setFirstMove(int index)
{
    if (!m_loaded)
        return;
    const int last = std::max(0, layerMoveCount() - 1);
    const int move = std::clamp(index, 0, last);
    if (move == m_firstMove)
        return;
    m_firstMove = move;
    if (m_lastMove >= 0 && m_lastMove < m_firstMove)
        m_lastMove = m_firstMove;
    forwardMoveRange();
    emit changed();
}

void Toolpath::setLastMove(int index)
{
    if (!m_loaded)
        return;
    const int last = std::max(0, layerMoveCount() - 1);
    const int move = std::clamp(index, m_firstMove, last);
    if (m_lastMove < 0) {
        m_lastMove = last;
        if (move == m_lastMove)
            return;
    }
    if (move == m_lastMove)
        return;
    m_lastMove = move;
    forwardMoveRange();
    emit changed();
}

void Toolpath::resetMoveRange()
{
    m_firstMove = 0;
    m_lastMove = -1;
}

void Toolpath::setFeatureVisible(const QString& name, bool visible)
{
    const bool hidden = !visible;
    if (m_hidden.contains(name) == hidden)
        return;
    if (hidden)
        m_hidden.insert(name);
    else
        m_hidden.remove(name);
    forwardVisibility(name, visible);
    emit changed();
}

// The layer view modes: everything up to the slider, every layer, or the
// slider's layer on its own. Mode 0 is the default because a slider that cuts
// the model is what a slicer's preview is expected to do.
void Toolpath::setSingleLayer(bool single)
{
    const int mode = single ? 2 : 1;
    if (m_layerMode == mode)
        return;
    m_layerMode = mode;
    forwardLayerRange();
    emit changed();
}

void Toolpath::cycleLayerMode()
{
    m_layerMode = (m_layerMode + 1) % 3;
    forwardLayerRange();
    emit changed();
}

int Toolpath::layerMode() const
{
    return m_layerMode;
}

// The move the preview is stopped at: the upper handle of the moves slider, in
// the renderer's own vertex space.
int Toolpath::currentVertex() const
{
    if (!m_loaded)
        return -1;
    return layerVertexStart(m_currentLayer) + lastMove();
}

int Toolpath::gcodeLine() const
{
    const int vertex = currentVertex();
    if (vertex < 0 || vertex >= m_vertexGcodeIds.size())
        return 0;
    return int(m_vertexGcodeIds[vertex]);
}

QVariantList Toolpath::gcodeWindow() const
{
    constexpr int kHalf = 10;
    const int centre = gcodeLine();
    if (centre <= 0 || m_gcodeLines.isEmpty())
        return {};
    const int first = std::max(1, centre - kHalf);
    const int last = std::min(int(m_gcodeLines.size()), centre + kHalf);
    QVariantList window;
    window.reserve(last - first + 1);
    for (int line = first; line <= last; ++line) {
        QVariantMap entry;
        entry.insert(QStringLiteral("number"), line);
        entry.insert(QStringLiteral("text"), m_gcodeLines.at(line - 1).trimmed());
        entry.insert(QStringLiteral("current"), line == centre);
        window.append(entry);
    }
    return window;
}

void Toolpath::clear()
{
    if (!m_loaded)
        return;
    m_data = PreviewData{};
    m_gcodeLines.clear();
    m_vertexGcodeIds.clear();
    m_layerVertexRanges.clear();
    m_currentLayer = 0;
    m_firstLayer = 0;
    m_firstMove = 0;
    m_lastMove = -1;
    m_loaded = false;
    if (m_view != nullptr)
        m_view->clearContents();
    emit changed();
}

int Toolpath::layerCount() const
{
    return m_data.layers.size();
}

int Toolpath::currentLayer() const
{
    return m_currentLayer;
}

int Toolpath::firstLayer() const
{
    return m_firstLayer;
}

void Toolpath::setFirstLayer(int index)
{
    if (!m_loaded)
        return;
    const int layer = std::clamp(index, 0, m_currentLayer);
    if (layer == m_firstLayer)
        return;
    m_firstLayer = layer;
    if (m_layerMode == 0)
        forwardLayerRange();
    emit changed();
}

int Toolpath::moveCount() const
{
    return m_data.moves.size();
}

double Toolpath::layerZAt(int layer) const
{
    if (!m_loaded || layer < 0 || layer >= m_data.layers.size())
        return 0.0;
    return m_data.layers.at(layer).z;
}

int Toolpath::layerAtZ(double z) const
{
    if (!m_loaded || m_data.layers.isEmpty() || z <= 0.0)
        return -1;
    int best = -1;
    double bestDistance = std::numeric_limits<double>::max();
    for (int index = 0; index < m_data.layers.size(); ++index) {
        const double distance = std::abs(double(m_data.layers.at(index).z) - z);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = index;
        }
    }
    return best;
}

double Toolpath::layerZ() const
{
    if (!m_loaded || m_currentLayer >= m_data.layers.size())
        return 0.0;
    return m_data.layers.at(m_currentLayer).z;
}

bool Toolpath::loaded() const
{
    return m_loaded;
}

bool Toolpath::singleLayer() const
{
    return m_layerMode == 2;
}

double Toolpath::totalSeconds() const
{
    return m_data.totalSeconds;
}

double Toolpath::totalFilamentMm() const
{
    return m_data.totalFilamentMm;
}

double Toolpath::totalFilamentG() const
{
    return m_data.totalFilamentG;
}

double Toolpath::totalCost() const
{
    return m_data.totalCost;
}

// Per line type, heaviest first, so the
// rows that explain the print time sit at the top.
QVariantList Toolpath::featureLegend() const
{
    QVector<PreviewRole> shown;
    for (const PreviewRole& role : m_data.roles)
        if (role.moves > 0)
            shown.append(role);

    std::stable_sort(shown.begin(), shown.end(), [](const PreviewRole& a, const PreviewRole& b) {
        return a.seconds > b.seconds;
    });

    const double total = m_data.totalSeconds > 0.0 ? m_data.totalSeconds : 1.0;

    QVariantList legend;
    for (const PreviewRole& role : shown) {
        QVariantMap entry;
        entry.insert(QStringLiteral("name"), role.name);
        entry.insert(QStringLiteral("color"), role.color.name(QColor::HexRgb));
        entry.insert(QStringLiteral("moves"), role.moves);
        entry.insert(QStringLiteral("seconds"), role.seconds);
        entry.insert(QStringLiteral("percent"), role.seconds / total * 100.0);
        entry.insert(QStringLiteral("lengthMm"), role.lengthMm);
        entry.insert(QStringLiteral("massG"), role.massG);
        entry.insert(QStringLiteral("visible"), !m_hidden.contains(role.name));
        legend.append(entry);
    }
    return legend;
}

void Toolpath::forwardInput(Slic3r::App::libvgcode::FdmViewerInputData&& input)
{
    if (m_view != nullptr) {
        m_view->setInput(std::move(input));
        forwardLayerRange();
    } else {
        m_pendingInput = std::make_unique<Slic3r::App::libvgcode::FdmViewerInputData>(std::move(input));
    }
}

void Toolpath::forwardLayerRange()
{
    if (m_view == nullptr || !m_loaded)
        return;
    const int last = std::max(0, layerCount() - 1);
    const int min = (m_layerMode == 0) ? std::min(m_firstLayer, m_currentLayer)
                  : (m_layerMode == 2) ? m_currentLayer : 0;
    const int max = (m_layerMode == 0) ? std::min(m_currentLayer, last)
                  : (m_layerMode == 2) ? m_currentLayer : last;
    m_view->setLayerRange(min, max);
    forwardMoveRange();
}

// The moves slider narrows the layer range down to the moves of the current
// layer. libvgcode's visible range counts its own vertices, one per G-code move,
// so the viewer supplies the per-layer start and count; the slider works in that
// space, which is the same one the move slider scrubs.
void Toolpath::forwardMoveRange()
{
    if (m_view == nullptr || !m_loaded || m_data.layers.isEmpty())
        return;
    const int last = std::max(0, layerMoveCount() - 1);
    if (last == 0)
        return;
    const int rangeStart = (m_layerMode == 0) ? std::min(m_firstLayer, m_currentLayer)
                         : (m_layerMode == 2) ? m_currentLayer : 0;
    const int layerStart = layerVertexStart(m_currentLayer);
    const int min = (rangeStart == m_currentLayer)
                    ? layerStart + std::clamp(m_firstMove, 0, last)
                    : layerVertexStart(rangeStart);
    const int max = layerStart + std::clamp(lastMove(), 0, last);
    m_view->setVisibleMoveRange(min, max);
}

void Toolpath::forwardVisibility(const QString& name, bool visible)
{
    if (m_view == nullptr)
        return;
    const int role = PreviewReader::roleIndex(name);
    if (role >= 0)
        m_view->setRoleVisibility(role, visible);
}
