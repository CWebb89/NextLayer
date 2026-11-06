#pragma once

#include <Slic3r/Domain/LayerHeightProfile.hpp>
#include <Slic3r/Domain/Transformation.hpp>
#include <Slic3r/Domain/TriangleMesh.hpp>
#include <Slic3r/Domain/TriangleSelector.hpp>
#include <Slic3r/Domain/Types.hpp>

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <vector>

namespace next_layer {

/// One object as a project file stores it: geometry, its own transform, and the
/// three things this app can attach to an object beyond geometry -- overrides, a
/// layer height profile and paint.
/// A volume attached to a project object: geometry in the object's own frame,
/// its own transform, and the role it plays in the slice.
struct ProjectVolume
{
    QString name;
    QString type;
    bool visible = true;
    Slic3r::Domain::TriangleMesh mesh;
    Slic3r::Domain::Transformation transform;
};

struct ProjectObject
{
    QString name;
    QString sourcePath;
    int plate = 0;
    bool visible = true;
    Slic3r::Domain::TriangleMesh mesh;
    Slic3r::Domain::Transformation transform;
    std::vector<ProjectVolume> volumes;
    QVariantMap settings;
    Slic3r::Domain::ZHeightPairs layerHeights;
    Slic3r::Domain::TriangleSelector::TriangleSplittingData supportPaint;
    Slic3r::Domain::TriangleSelector::TriangleSplittingData seamPaint;
    Slic3r::Domain::TriangleSelector::TriangleSplittingData toolPaint;
};

/// A marker on the plate's timeline, stored with the plate because the engine
/// applies it to the bed instance rather than to any one object.
struct ProjectLayerGcode
{
    double z = 0.0;
    QString type;
    QString text;
};

struct ProjectMetadata
{
    QString applicationVersion;
    QString printer;
    QString process;
    QString filament;
    int activePlate = 0;
    int plateCount = 1;
    QStringList plateNames;
    QVector<bool> plateLocked;
    QStringList toolColours;
    std::vector<ProjectLayerGcode> layerGcodes;
};

/// Writes the document as one JSON file. Meshes are embedded as base64 binary
/// STL, so a project is one file that moves between machines without the models
/// it was built from.
QByteArray serialiseProject(const std::vector<ProjectObject>& objects,
                            const ProjectMetadata& metadata);

/// Reads a project file back. Returns false and fills \a error when the file is
/// not a project, is newer than this build, or names a mesh that cannot be read.
bool parseProject(const QByteArray& body, std::vector<ProjectObject>& objects,
                  ProjectMetadata& metadata, QString& error);

}
