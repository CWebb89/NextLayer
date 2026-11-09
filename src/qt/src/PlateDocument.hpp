// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#pragma once

#include "Arranger.hpp"
#include "Paint.hpp"
#include "SliceRuntime.hpp"
#include "ThreeMfProject.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QVector2D>
#include <QVector3D>

#include <Slic3r/Domain/ModelVolume.hpp>
#include <Slic3r/Domain/TriangleMesh.hpp>
#include <Slic3r/Domain/Transformation.hpp>

#include <algorithm>
#include <memory>
#include <vector>

class QTemporaryDir;

namespace Slic3r::Domain {
class ModelInstance;
class ModelObject;
} // namespace Slic3r::Domain

// The build plate: an ordered list of objects, each with its own position,
// rotation and scale, and one of them selected. Geometry is normalised on import
// the way the engine's own loader normalises it -- XY centred on the origin,
// minimum Z at zero -- so an object's position is the world position of its
// centre and every transform pivots about that same point.
//
// The document is the single source of truth for what is on the plate: the
// viewport renders it, the object list edits it, and the slicer is handed the
// baked geometry. Geometry is immutable after import and shared between undo
// snapshots, so an undo step costs the object list and nothing more.
class PlateDocument : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList objects READ objects NOTIFY objectsChanged)
    Q_PROPERTY(int count READ count NOTIFY objectsChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY objectsChanged)
    Q_PROPERTY(int selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedIds READ selectedIds NOTIFY selectionChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(QVector3D boundsMin READ boundsMin NOTIFY objectsChanged)
    Q_PROPERTY(QVector3D boundsMax READ boundsMax NOTIFY objectsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    Q_PROPERTY(QVariantList layerGcodeMarkers READ layerGcodes NOTIFY layerGcodesChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectPathChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY undoChanged)
    Q_PROPERTY(QString redoLabel READ redoLabel NOTIFY undoChanged)
    Q_PROPERTY(int version READ version NOTIFY objectsChanged)
    Q_PROPERTY(int activePlate READ activePlate NOTIFY plateChanged)
    Q_PROPERTY(int plateCount READ plateCount NOTIFY plateChanged)
    /// Every plate as the viewport and the plate bar need it. A property, not a
    /// call: a binding that calls a method does not re-evaluate when the document
    /// changes, and the plate bar would stay on one plate forever.
    Q_PROPERTY(QVariantList plates READ plateList NOTIFY plateChanged)
    Q_PROPERTY(QVariantList toolColours READ toolColours NOTIFY toolsChanged)
    Q_PROPERTY(int toolCount READ toolCount NOTIFY toolsChanged)

public:
    using MeshPtr = std::shared_ptr<const Slic3r::Domain::TriangleMesh>;
    using VolumeType = Slic3r::Domain::ModelVolumeType;

    /// A volume attached to an object: a part the engine unions, a negative
    /// volume it subtracts, a modifier it reads settings from, or a support
    /// blocker/enforcer. Geometry is in the object's own frame, and the volume's
    /// transform is relative to that frame, so a volume follows its object when
    /// the object is moved, rotated or scaled.
    struct Volume
    {
        int id = 0;
        QString name;
        MeshPtr mesh;
        VolumeType type = VolumeType::MODEL_PART;
        Slic3r::Domain::Transformation trafo;
        QVector3D localMin;
        QVector3D localMax;
        bool visible = true;
        /// A modifier's own overrides. The engine keeps volume settings in a box
        /// of their own, so a modifier can change infill or walls without
        /// touching the object it sits in.
        QVariantMap settings;

        Slic3r::Domain::Transform3d worldMatrix(const Slic3r::Domain::Transformation& object) const
        {
            return object.get_matrix() * trafo.get_matrix();
        }
    };

    struct Object
    {
        int id = 0;
        QString path;
        QString name;
        QString label;
        MeshPtr mesh;
        Slic3r::Domain::Transformation trafo;
        QVector3D localMin;
        QVector3D localMax;
        bool visible = true;
        int plate = 0;

        // What this object alone overrides, its own layer height profile (empty
        // for a uniform object) and its painted facets. All three travel with the
        // object: they are what makes a plate more than a pile of meshes.
        std::vector<Volume> volumes;

        QVariantMap settings;
        // Settings the object's file carried that this engine cannot act on, kept
        // verbatim in the dialect they were written in. They ride along with the
        // document and are written back out on export, so opening a project from
        // another slicer and saving it again loses nothing -- the alternative, which
        // is what that other slicer does, is to drop them.
        QHash<QString, QString> foreignSettings;
        Slic3r::Domain::ZHeightPairs layerHeights;
        Slic3r::Domain::TriangleSelector::TriangleSplittingData supportPaint;
        Slic3r::Domain::TriangleSelector::TriangleSplittingData seamPaint;
        Slic3r::Domain::TriangleSelector::TriangleSplittingData toolPaint;

        QString displayName() const { return label.isEmpty() ? name : label; }
        QVector3D position() const;
        QVector3D rotation() const;
        QVector3D scale() const;
        QVector3D size() const;
    };

    explicit PlateDocument(QVector2D bedCentre, QVector2D bedSize, QObject* parent = nullptr);
    ~PlateDocument() override;

    int count() const;
    bool empty() const { return m_objects.empty(); }
    int version() const { return m_version; }
    QVariantList objects() const;
    /// The printer's tools, as colour swatches. A tool changer assigns one colour
    /// per tool, and painting a facet with tool N prints it in that colour.
    QVariantList toolColours() const;
    int toolCount() const { return int(m_toolColours.size()); }
    Q_INVOKABLE void setToolColours(const QVariantList& colours);
    Q_INVOKABLE QColor toolColour(int tool) const;

    int activePlate() const { return m_activePlate; }
    /// Plates are a property of the document, not a by-product of where objects
    /// happen to be: an empty plate you added is still a plate, and it is the one
    /// the viewport has to draw a bed for.
    int plateCount() const { return std::max(1, m_plateCount); }
    Q_INVOKABLE int objectsOnPlate(int plate) const;
    QVector<int> idsOnPlate(int plate) const;
    Q_INVOKABLE void setActivePlate(int plate);
    /// Where a plate's own coordinate frame sits in the scene. Plates are laid
    /// out in a row with one bed's width plus a gap between them, which is how a
    /// multi-plate project is shown: every plate visible, the
    /// active one where the camera is.
    Q_INVOKABLE QVector3D plateOffset(int plate) const;
    Q_INVOKABLE QVector3D plateSize() const { return QVector3D(m_bedSize.x(), m_bedSize.y(), 0.0f); }
    /// The bed's centre in printer coordinates, which is where a built plate -- a
    /// calibration tower, a set of test patches -- has to sit.
    Q_INVOKABLE QVector3D bedCentre() const { return QVector3D(m_bedCentre.x(), m_bedCentre.y(), 0.0f); }
    Q_INVOKABLE int addPlate();
    bool refuseOnLockedPlate();
    /// A copy of a plate: every object on it, with its overrides, profiles and
    /// paint, moved onto a new plate at the end.
    Q_INVOKABLE int duplicatePlate(int plate);
    Q_INVOKABLE void clearPlate(int plate);
    Q_INVOKABLE void removePlate(int plate);
    /// A locked plate is read-only: nothing can be dropped on it,
    /// moved on it, painted or removed until it is unlocked.
    Q_INVOKABLE bool plateLocked(int plate) const;
    Q_INVOKABLE void setPlateLocked(int plate, bool locked);
    Q_INVOKABLE bool activePlateLocked() const { return plateLocked(m_activePlate); }
    Q_INVOKABLE QString plateName(int plate) const;
    /// Every plate as the viewport and the plate bar need it: where its bed sits,
    /// what it is called, whether it is locked, how much is on it.
    QVariantList plateList() const;
    Q_INVOKABLE void setPlateName(int plate, const QString& name);

    int selectedId() const { return m_selectedIds.empty() ? 0 : m_selectedIds.back(); }
    int selectedCount() const { return static_cast<int>(m_selectedIds.size()); }
    QVariantList selectedIds() const;
    std::vector<int> selection() const { return m_selectedIds; }
    Q_INVOKABLE bool isSelected(int id) const;
    QString error() const { return m_error; }

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    QString undoLabel() const;
    QString redoLabel() const;

    bool has(int id) const { return find(id) != nullptr; }
    const Object* object(int id) const { return find(id); }
    QVector<int> ids() const;

    Q_INVOKABLE int addObject(const QString& path, const QString& label = QString());
    Q_INVOKABLE int addObjectAtFilePosition(const QString& path, const QString& label = QString());
    Q_INVOKABLE bool removeObject(int id);
    Q_INVOKABLE int duplicateObject(int id);

    /// A primitive added straight onto the plate -- a box, cylinder or sphere --
    /// as a new object with its own transform, exactly as an imported model is.
    Q_INVOKABLE int addShape(const QString& kind, double width, double depth,
                             double height);
    /// A volume attached to an existing object. a type is one of "modifier",
    /// "negative", "blocker" or "enforcer"; the shape is the same set of
    /// primitives, sized to sit inside the object it will affect.
    Q_INVOKABLE int addVolume(int objectId, const QString& kind, const QString& type,
                              double width, double depth, double height);
    Q_INVOKABLE bool removeVolume(int objectId, int volumeId);
    Q_INVOKABLE bool setVolumeVisible(int objectId, int volumeId, bool visible);
    Q_INVOKABLE QVariantMap volumeSettings(int objectId, int volumeId) const;
    Q_INVOKABLE bool setVolumeSetting(int objectId, int volumeId, const QString& name,
                                      const QVariant& value);
    Q_INVOKABLE bool clearVolumeSetting(int objectId, int volumeId, const QString& name);
    /// Where the volume sits inside its object, and how big it is: the two
    /// controls a modifier needs to be useful. Position is in the object's own
    /// frame; size is the volume's own scale factor.
    Q_INVOKABLE bool setVolumePosition(int objectId, int volumeId, const QVector3D& position);
    Q_INVOKABLE bool setVolumeScale(int objectId, int volumeId, double factor);
    Q_INVOKABLE QVariantList volumes(int objectId) const;
    Q_INVOKABLE bool renameObject(int id, const QString& name);
    Q_INVOKABLE void clear();

    /// The file-open filter for every format the engine's reader accepts. It is
    /// derived from the engine's own list so a format cannot be offered that the
    /// reader would refuse, or silently missing from the dialog once it can.
    Q_INVOKABLE QString importWildcards() const;
    Q_INVOKABLE void selectObject(int id, bool additive = false);
    Q_INVOKABLE void selectRange(int id);
    Q_INVOKABLE void selectAll();
    /// Replace the selection with these objects, ignoring ids that are not on the
    /// active plate. What a rubber-band selection needs: the viewport knows the
    /// rectangle, the document owns the selection.
    Q_INVOKABLE void selectObjects(const QVariantList& ids);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool moveSelection(double deltaX, double deltaY, double deltaZ = 0.0);
    Q_INVOKABLE bool rotateSelection(double degrees);
    Q_INVOKABLE bool scaleSelection(double factor);
    Q_INVOKABLE void removeSelection();
    Q_INVOKABLE void duplicateSelection();
    Q_INVOKABLE void centreSelection();
    Q_INVOKABLE void dropSelection();
    Q_INVOKABLE void isolateSelection();
    Q_INVOKABLE void showAll();

    /// The printer/process/filament triple the document was built against, so a
    /// project reopens on the machine it was sliced for.
    void setPrintSetup(const QString& printer, const QString& process, const QString& filament);
    Q_INVOKABLE QString printSetupPrinter() const { return m_printer; }
    Q_INVOKABLE QString printSetupProcess() const { return m_process; }
    Q_INVOKABLE QString printSetupFilament() const { return m_filament; }

    /// One file for the whole document: every object with its transform, its
    /// overrides, its layer height profile and its paint, plus the plates and the
    /// print setup. Both return an empty string on success or the reason it
    /// failed, and loading replaces everything, undoably.
    Q_INVOKABLE QString saveProject(const QString& path);
    Q_INVOKABLE QString loadProject(const QString& path);

    /// True once the plate has changed since it was last saved or opened, which
    /// is what the close guard asks before throwing the work away.
    bool modified() const { return m_modified; }
    QString projectPath() const { return m_projectPath; }
    Q_INVOKABLE bool setPosition(int id, const QVector3D& position);
    Q_INVOKABLE bool setRotation(int id, const QVector3D& degrees);
    Q_INVOKABLE bool setScale(int id, const QVector3D& factor);
    Q_INVOKABLE bool setVisible(int id, bool visible);
    Q_INVOKABLE bool centreObject(int id);
    Q_INVOKABLE bool placeOnFace(int id, int facet);
    Q_INVOKABLE bool autoOrient(int id);
    Q_INVOKABLE bool autoOrientSelection();
    Q_INVOKABLE bool arrangePlate();
    Q_INVOKABLE QVariantMap objectSettings(int id) const;
    /// Settings the object's file carried that this engine cannot act on. They are
    /// kept, written back out on export and shown read-only: nothing is lost by
    /// opening a project from another slicer here.
    Q_INVOKABLE QVariantMap objectForeignSettings(int id) const;
    Q_INVOKABLE bool setObjectSetting(int id, const QString& name, const QVariant& value);
    Q_INVOKABLE bool clearObjectSetting(int id, const QString& name);
    Q_INVOKABLE void clearObjectSettings(int id);
    Q_INVOKABLE QVariantList layerHeightProfile(int id) const;
    Q_INVOKABLE bool setLayerHeightRange(int id, double bottomZ, double topZ, double height);
    Q_INVOKABLE void clearLayerHeights(int id);

    /// The layer height the printer would use with no profile, needed because a
    /// layer height profile is a list of absolute heights: outside the edited band
    /// the profile has to restate it, and zero is not a height.
    void setBaseLayerHeight(double height) { m_baseLayerHeight = height > 0.0 ? height : 0.2; }
    Q_INVOKABLE int paintObject(int id, const QString& kind, const QVector3D& worldPoint,
                                double radius, bool erase, int extruder = 2,
                                int facetHint = -1);
    Q_INVOKABLE void clearPaint(int id, const QString& kind);
    /// The tool an object prints with. A tool changer keeps one setting per
    /// region -- perimeter, infill, solid infill, support -- and the object as a
    /// whole is at tool 1 until one of them says otherwise.
    Q_INVOKABLE int objectTool(int id) const;
    Q_INVOKABLE void setObjectTool(int id, int tool);
    Q_INVOKABLE int paintedFacetCount(int id, const QString& kind) const;

    /// A marker on the plate's timeline: a custom G-code snippet, a pause or a
    /// colour/tool change at a Z height. It belongs to the plate, not to an
    /// object, the way the engine's own custom G-codes do.
    struct LayerGcode
    {
        double z = 0.0;
        QString type = QStringLiteral("gcode");
        QString text;
    };

    Q_INVOKABLE QVariantList layerGcodes() const;
    Q_INVOKABLE bool addLayerGcode(double z, const QString& type, const QString& text);
    Q_INVOKABLE bool removeLayerGcode(double z);
    Q_INVOKABLE void clearLayerGcodes();
    QVector<next_layer::PlateLayerGcode> layerGcodeList() const;

    /// Every visible object as the slicer needs it: geometry in printer
    /// coordinates plus its overrides, layer height profile and paint.
    QVector<next_layer::PlateSliceObject> sliceObjects() const;
    /// The same objects for a plate that is not the active one, without changing
    /// which plate is active: slicing every plate must not move the camera or the
    /// selection out from under the person who asked for it.
    QVector<next_layer::PlateSliceObject> sliceObjectsFor(int plate) const;
    /// The active plate's objects as a project file stores them: geometry in each
    /// object's own frame with the transform that places it, and each volume in the
    /// object's frame. A 3MF has to carry the placement as a transform, so baking it
    /// into the vertices -- which is what the slicer path wants -- would lose it.
    /// Every visible object on every plate. Objects sit in their own plate's
    /// coordinates, so the plates are written side by side in one model and the
    /// plate records say which is which -- the slicer's own plates are a grid in
    /// the scene, not a coordinate space in the file.
    QVector<next_layer::PlateSliceObject> projectObjects() const;
    /// Which plate lies under a point on the bed plane, or -1 for the gap between
    /// plates. Plates are drawn in a grid, so a double click anywhere on one is
    /// enough to say which plate was meant.
    Q_INVOKABLE int plateAt(const QVector3D& point) const;
    Q_INVOKABLE bool arrangeSelection();
    Q_INVOKABLE bool dropToBed(int id);
    Q_INVOKABLE QVariantMap objectInfo(int id) const;

    /// What the mesh actually is: volume, surface area, facet count and the
    /// bounding box, in printer millimetres. Density and cost are the caller's
    /// business -- they belong to the filament, not to the geometry.
    Q_INVOKABLE QVariantMap objectStatistics(int id) const;
    Q_INVOKABLE QVariantMap plateStatistics(int plate) const;

    /// Bake several objects into one mesh in world coordinates and keep the
    /// first one as their container -- Merge. The result sits where the
    /// parts did, so nothing moves on the plate.
    Q_INVOKABLE bool mergeObjects(const QVector<int>& ids);

    /// Merge exactly what is selected, which is what the object menu calls.
    Q_INVOKABLE bool mergeSelection();

    /// Connected-component split: a mesh that is really several shells becomes
    /// that many objects, each in place. Returns how many objects were added.
    Q_INVOKABLE int splitToParts(int id);

    /// Cut at a Z height. The object becomes its lower half and the upper half
    /// becomes a new object beside it in the list, both closed with a cap. Returns
    /// the part count that resulted (1 when the plane missed, leaving the object
    /// whole).
    Q_INVOKABLE int cutAtZ(int id, double z, bool keepUpper);

    /// Keep only the slab between two heights and drop it onto the bed, so a band
    /// of a calibration tower becomes an object a printer can print. Returns false
    /// when the range misses the mesh.
    Q_INVOKABLE bool trimToBand(int id, double bottom, double top);

    /// A line of text as an object. The glyph outlines are baked into the mesh, so
    /// a text object is geometry like any other: it arranges, paints, cuts and
    /// slices with everything else.
    Q_INVOKABLE int addText(const QString& text, double height, double depth,
                            const QString& fontPath);
    Q_INVOKABLE QVariantList fonts() const;

    /// Hollow a solid: an inner wall `thickness` millimetres
    /// inside the surface, through the engine's own voxel hollowing.
    Q_INVOKABLE bool hollowObject(int id, double thickness);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    void setBed(QVector2D centre, QVector2D size);

    QVector3D boundsMin() const;
    QVector3D boundsMax() const;
    QVector3D objectBoundsMin(int id) const;
    QVector3D objectBoundsMax(int id) const;

    QByteArray fingerprint() const;
    QString stagedPlatePath();

    Slic3r::Domain::TriangleMesh worldMesh(int id) const;

signals:
    void objectsChanged();
    void selectionChanged();
    void errorChanged();
    void undoChanged();
    void geometryChanged();
    void plateChanged();
    void printSetupChanged();
    void projectPathChanged();
    void modifiedChanged();
    void toolsChanged();
    void layerGcodesChanged();

private:
    struct Snapshot
    {
        std::vector<Object> objects;
        std::vector<int> selection;
        int activePlate = 0;
        QString label;
        int coalesceKey = 0;
    };

    Object* find(int id);
    const Object* find(int id) const;
    QString displayNameOf(int id) const;
    int importObject(const QString& path, const QString& label, bool placeOnBed);
    int importSceneObject(const Slic3r::Domain::ModelObject& source,
                          const Slic3r::Domain::ModelInstance* instance, const QString& local,
                          const QString& label, bool placeOnBed, bool deferPush,
                          const next_layer::ThreeMfProject::Placement* placement = nullptr);
    void layoutProject(const QList<int>& added, const next_layer::ThreeMfProject& project);
    /// Applies what a project file recorded about one object beyond its geometry:
    /// its overrides, its variable layer height bands, the facets painted on it and
    /// the modifier volumes attached to it. The engine's loader keeps none of these
    /// for the formats this app writes and reads, so they arrive here from the
    /// file's own records.
    void applyProjectRecord(int id, const Slic3r::Domain::ModelObject& source,
                            const next_layer::ThreeMfObjectRecord& record);
    /// Settings another slicer wrote under this object, in that slicer's own
    /// names. What this engine's definitions accept becomes an override; the rest
    /// is reported by name, because a foreign project must still open.
    void applyForeignSettings(int id, const QHash<QString, QString>& settings,
                                const QString& name);
    void placeNext(Object& object) const;
    void fail(const QString& message);
    /// Something that did not fit but did not stop the file opening -- a setting the
    /// engine has no definition for, written by another slicer. Collected while the
    /// file loads and reported in the status bar once it has, instead of aborting an
    /// import over one unknown name.
    void note(const QString& message);
    int applyPlacements(const std::vector<next_layer::PlaceableObject>& placeables);
    static QByteArray settingsToJson(const QVariantMap& settings);
    static Slic3r::Domain::TriangleMesh primitiveMesh(const QString& kind, double width,
                                                      double depth, double height);
    static QString volumeTypeName(VolumeType type);
    static VolumeType volumeTypeFromName(const QString& name);
    void pushUndo(const QString& label, int coalesceKey = 0);
    void restore(const Snapshot& snapshot);
    void changed();

    QVector2D m_bedCentre;
    QVector2D m_bedSize;
    std::vector<Object> m_objects;
    std::vector<Snapshot> m_undo;
    std::vector<Snapshot> m_redo;
    int m_nextId = 1;
    int m_activePlate = 0;
    int m_plateCount = 1;
    QVector<bool> m_plateLocked;
    QStringList m_plateNames;
    QString m_printer;
    QString m_process;
    QString m_filament;
    QString m_projectPath;
    bool m_modified = false;
    QVector<QColor> m_toolColours;
    std::vector<LayerGcode> m_layerGcodes;
    double m_baseLayerHeight = 0.2;
    std::vector<int> m_selectedIds;
    int m_version = 0;
    QString m_error;
    QStringList m_pendingNotes;
    std::unique_ptr<QTemporaryDir> m_scratch;
    QStringList m_stagedPlates;
    int m_stagedRevision = 0;
};
