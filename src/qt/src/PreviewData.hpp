#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QVariantMap>

namespace Slic3r::App::libvgcode {
struct FdmViewerInputData;
}

// Preview data for one sliced G-code file.
//
// Read through the engine's own G-code processor (Slic3r::Biz::libpgcode),
// which is what the engine's preview and its statistics panel are built from.
// That is where the real extrusion width, height, per-move time and per-role
// filament usage come from — the previous hand-rolled reader in GcodeParser.cpp
// had none of them and faked the width with a constant.

// One extruded segment, in the form the engine's own preview consumes it: the
// two endpoints with their own extrusion width and height, and the extrusion
// role at each end. The renderer builds a box per segment (cross section at each
// endpoint) with a mitred cap, exactly as libvgcode's segment shader does, so it
// needs both ends' dimensions rather than one width for the whole path.
struct PreviewMove
{
    float x0 = 0.0f;
    float y0 = 0.0f;
    float z0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float z1 = 0.0f;
    float width0 = 0.0f;
    float height0 = 0.0f;
    float width1 = 0.0f;
    float height1 = 0.0f;
    int role0 = 0;
    int role1 = 0;
    int layer = 0;
    bool continuous = false;
};

struct PreviewLayer
{
    float z = 0.0f;
    int start = 0;
    int end = 0;
};

struct PreviewRole
{
    int role = 0;
    QString name;
    QColor color;
    int moves = 0;
    double seconds = 0.0;
    double lengthMm = 0.0;
    double massG = 0.0;
};

struct PreviewData
{
    QVector<PreviewMove> moves;
    QVector<PreviewLayer> layers;
    QVector<PreviewRole> roles;
    QVector3D boundsMin;
    QVector3D boundsMax;
    double totalSeconds = 0.0;
    double totalFilamentMm = 0.0;
    double totalFilamentG = 0.0;
    double totalCost = 0.0;

    bool valid() const { return !moves.isEmpty() && !layers.isEmpty(); }
};

// Headless reference render: loads a G-code file into libvgcode and writes one
// frame to OUT. The live preview drives the same renderer through GcodeView; this
// flag exists to verify the render off-screen against the engine's own viewer.
bool renderPreviewWithVgcode(const QString& gcodePath, const QString& outPath,
                             float yaw, float pitch, float zoom);

class PreviewReader
{
public:
    static PreviewData read(const QString& gcodePath);

    // Runs the G-code processor once and fills both the QML-facing data and the
    // libvgcode renderer input, so the legend and the toolpath view never parse
    // the same file twice.
    static bool readForRender(const QString& gcodePath, PreviewData& data,
                              Slic3r::App::libvgcode::FdmViewerInputData& input);

    static QString roleName(int role);
    static int roleIndex(const QString& name);
    static QColor roleColor(int role, const QVariantMap& palette);
};
