// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "SceneSelftest.hpp"

#include "Calibration.hpp"

#include "PlateDocument.hpp"
#include "SettingsModel.hpp"

#include "SliceRuntime.hpp"

#include <QFileInfo>
#include <QTemporaryDir>

#include "Scratch.hpp"

#include <Slic3r/App/CLI/CLIRuntime.hpp>
#include <Slic3r/App/Init.hpp>
#include <Slic3r/Biz/Config/ConfigLoad.hpp>
#include <Slic3r/Biz/Algorithms/ModelVolume.hpp>
#include <Slic3r/Biz/Platform/JobManager/JobManager.hpp>
#include <Slic3r/Biz/Platform/PlatformServices.hpp>
#include <Slic3r/Biz/Scene/SceneInteractor.hpp>
#include <Slic3r/Biz/Slicing/SlicingInteractor.hpp>
#include <Slic3r/Biz/StatusCache.hpp>
#include <Slic3r/Domain/Bed.hpp>
#include <Slic3r/Domain/BedInstance.hpp>
#include <Slic3r/Domain/ConfigContainer.hpp>
#include <Slic3r/Domain/Model.hpp>
#include <Slic3r/Domain/ModelInstance.hpp>
#include <Slic3r/Domain/ModelObject.hpp>
#include <Slic3r/Domain/ModelVolume.hpp>
#include <Slic3r/Domain/Project.hpp>

#include <libslic3r/SlicingStatus.hpp>

#include <nlohmann/json.hpp>

#include <QFile>
#include <QVariantMap>
#include <QVector2D>
#include <QVector3D>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>

using Slic3r::Biz::ProjectInteractor;
using Slic3r::Biz::Slicing::Status;
using Slic3r::Biz::Slicing::StatusCode;
using Slic3r::Domain::Project;
using Slic3r::Domain::SlicingId;
using Slic3r::Domain::Vec2d;
using Slic3r::Domain::Vec3d;

namespace {

bool countToolpath(const QString& path, int& layers, int& moves, double& minX, double& maxX)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    layers = 0;
    moves = 0;
    minX = 1e9;
    maxX = -1e9;

    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        if (line.startsWith(";LAYER_CHANGE")) {
            ++layers;
            continue;
        }
        if (!line.startsWith("G1 "))
            continue;

        double x = 0.0;
        bool hasX = false;
        bool hasE = false;
        for (const QByteArray& word : line.split(' ')) {
            if (word.size() < 2)
                continue;
            if (word.at(0) == 'X') {
                x = word.mid(1).toDouble();
                hasX = true;
            } else if (word.at(0) == 'E') {
                hasE = true;
            }
        }
        if (!hasE || !hasX)
            continue;
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        ++moves;
    }
    return layers > 0 && moves > 0;
}

bool sliceAndExport(Slic3r::App::CLI::CLIRuntime& runtime,
                    const QString& gcodePath,
                    std::string& error)
{
    ProjectInteractor& project = runtime.project_interactor();
    Slic3r::Biz::Slicing::SlicingInteractor& slicing = project.slicing_interactor();
    Slic3r::Biz::StatusCache& statusCache = project.status_cache();

    const SlicingId slicingId = project.selected_bed_slicing_id();
    Project& selected = project.selected_project();
    const Slic3r::Domain::ConfigContainer& configContainer = project.selected_config_container();

    const Slic3r::Domain::BedInstance* bed =
        selected.find_bed_instance_by_id(slicingId.bed_instance_id);
    if (bed == nullptr) {
        error = "the selected project has no bed to slice";
        return false;
    }

    slicing.update_process(selected.model(), selected.metadata(),
                           configContainer.selected_preset().metadata(),
                           configContainer.build_print_config(), *bed);

    const auto terminal = [&statusCache, slicingId]()
    {
        const std::optional<Status> status = statusCache.get_status(slicingId);
        if (!status.has_value())
            return false;
        return status->code == StatusCode::Empty || status->code == StatusCode::Removed
            || status->code == StatusCode::Finished || status->code == StatusCode::InvalidData;
    };

    slicing.slice_bed(slicingId);

    int lastCode = -1;
    for (int tick = 0; tick < 400; ++tick) {
        runtime.dispatcher().dispatch_enqueued();
        const std::optional<Status> status = statusCache.get_status(slicingId);
        const int code = status.has_value() ? static_cast<int>(status->code) : -1;
        if (code != lastCode) {
            lastCode = code;
            std::printf("    slicing status -> %d\n", code);
            std::fflush(stdout);
        }
        if (terminal())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!terminal()) {
        error = "slicing never reached a terminal status";
        return false;
    }

    const Status status = statusCache.get_status(slicingId).value();
    if (status.code != StatusCode::Finished) {
        std::ostringstream message;
        message << "slicing did not finish (status " << status.code << ")";
        for (const auto& sliceError : status.errors) {
            message << "; error " << static_cast<int>(sliceError.code);
            for (const std::string& key : sliceError.item_keys)
                message << ' ' << key;
        }
        error = message.str();
        return false;
    }

    Slic3r::App::CLI::ExportFinishedJobManagerStatusListener exportListener;
    Slic3r::Biz::Platform::PlatformServices& services =
        Slic3r::Biz::Platform::PlatformServices::instance();
    services.job_manager().add_listener<Slic3r::Biz::Platform::JobManager::IJobManagerStatusChangedListener>(
        &exportListener);

    project.do_result_export(slicingId, boost::filesystem::path{gcodePath.toStdString()});
    runtime.wait_until([&exportListener]() { return exportListener.export_finished; });

    services.job_manager().remove_listener<Slic3r::Biz::Platform::JobManager::IJobManagerStatusChangedListener>(
        &exportListener);

    if (exportListener.export_error.has_value()) {
        error = "export failed: " + exportListener.export_error.value();
        return false;
    }
    return true;
}

}

namespace next_layer {

int runSceneSelftest(const QStringList& models,
                     const QString& configPath,
                     const QString& gcodePath,
                     const QString& dataDir)
{
    if (models.size() < 2) {
        std::fprintf(stderr, "scene-selftest needs at least two models\n");
        return 2;
    }

    std::ifstream configFile(configPath.toStdString());
    if (!configFile.is_open()) {
        std::fprintf(stderr, "cannot read config: %s\n", qPrintable(configPath));
        return 2;
    }
    nlohmann::ordered_json configJson;
    configFile >> configJson;

    const auto presetAndConfig = Slic3r::Biz::Config::load_preset_and_config(configJson);
    if (!presetAndConfig.has_value()) {
        std::fprintf(stderr, "config rejected: %s\n", presetAndConfig.error().c_str());
        return 1;
    }

    Slic3r::App::InitParams params;
    params.misc.datadir = dataDir.toStdString();
    Slic3r::App::CLI::CLIRuntime runtime{params};

    ProjectInteractor& project = runtime.project_interactor();

    // The engine's scene refuses to finish a queued arrange without an undo provider, so one has
    // to be installed even when nothing is undoable. The plate document replaces it with a real one.
    project.set_undo_provider(std::make_unique<Slic3r::Biz::NoopUndoProvider>());

    const auto projectId = project.new_project_with_preset(presetAndConfig->preset_metadata,
                                                          presetAndConfig->config_pack);
    if (!projectId.has_value()) {
        std::fprintf(stderr, "could not create the project: %s\n", projectId.error().c_str());
        return 1;
    }
    project.select_project(projectId.value());

    const Slic3r::Domain::Bed& bed = project.selected_config_container().bed();
    const Vec2d bedCentre = bed.center();
    const Slic3r::Domain::Vec2d bedExtent = bed.contour_aabb_extent();

    PlateDocument document(QVector2D(bedCentre.x(), bedCentre.y()),
                           QVector2D(bedExtent.x(), bedExtent.y()));

    for (const QString& model : models) {
        if (document.addObject(model) < 0) {
            std::fprintf(stderr, "%s\n", qPrintable(document.error()));
            return 1;
        }
    }

    if (document.count() != static_cast<int>(models.size())) {
        std::fprintf(stderr, "the document holds %d object(s) for %d model(s)\n",
                     document.count(), static_cast<int>(models.size()));
        return 1;
    }

    const QVector<int> ids = document.ids();
    for (int index = 1; index < ids.size(); ++index) {
        if (ids.at(index) <= ids.at(index - 1)) {
            std::fprintf(stderr, "object ids are not strictly increasing: %d then %d\n",
                         ids.at(index - 1), ids.at(index));
            return 1;
        }
    }

    const PlateDocument::Object* first = document.object(ids.first());
    if (first == nullptr) {
        std::fprintf(stderr, "the first object vanished from the document\n");
        return 1;
    }
    const QVector3D firstPosition = first->position();
    if (std::abs(firstPosition.x() - bedCentre.x()) > 0.01
        || std::abs(firstPosition.y() - bedCentre.y()) > 0.01
        || std::abs(firstPosition.z()) > 0.01) {
        std::fprintf(stderr, "the first object is not centred on the bed: (%.2f, %.2f, %.2f)\n",
                     firstPosition.x(), firstPosition.y(), firstPosition.z());
        return 1;
    }
    const int firstId = first->id;

    std::printf("document after import:\n");
    for (const QVariant& entry : document.objects()) {
        const QVariantMap info = entry.toMap();
        std::printf("  object %d \"%s\" at (%.2f, %.2f), %.2f x %.2f x %.2f mm%s\n",
                    info.value(QStringLiteral("id")).toInt(),
                    info.value(QStringLiteral("name")).toString().toUtf8().constData(),
                    info.value(QStringLiteral("x")).toDouble(),
                    info.value(QStringLiteral("y")).toDouble(),
                    info.value(QStringLiteral("width")).toDouble(),
                    info.value(QStringLiteral("depth")).toDouble(),
                    info.value(QStringLiteral("height")).toDouble(),
                    info.value(QStringLiteral("selected")).toBool() ? " (selected)" : "");
    }

    const int secondId = ids.at(1);
    const PlateDocument::Object* second = document.object(secondId);
    document.setPosition(secondId, QVector3D(firstPosition.x() + 40.0, second->position().y(), 0.0));

    const QVector3D low = document.boundsMin();
    const QVector3D high = document.boundsMax();
    std::printf("after moving object %d 40 mm along X: plate spans X %.2f..%.2f (%.2f mm)\n",
                secondId, low.x(), high.x(), high.x() - low.x());
    if (high.x() - low.x() < 40.0) {
        std::fprintf(stderr, "the second object did not move: the plate is only %.2f mm wide\n",
                     high.x() - low.x());
        return 1;
    }

    document.setRotation(secondId, QVector3D(0.0, 0.0, 45.0));
    const double rotatedWidth = document.objectBoundsMax(secondId).x()
                              - document.objectBoundsMin(secondId).x();
    std::printf("after rotating object %d 45 degrees about Z: footprint %.2f mm wide\n",
                secondId, rotatedWidth);
    if (std::abs(rotatedWidth - 20.0 * std::sqrt(2.0)) > 0.1) {
        std::fprintf(stderr, "a 20 mm square rotated 45 degrees should span %.2f mm, measured %.2f\n",
                     20.0 * std::sqrt(2.0), rotatedWidth);
        return 1;
    }

    document.setRotation(firstId, QVector3D(0.0, 0.0, 30.0));

    document.selectObject(secondId);
    if (document.selectedId() != secondId) {
        std::fprintf(stderr, "selection did not follow selectObject(%d)\n", secondId);
        return 1;
    }

    document.selectObject(firstId, true);
    if (document.selectedCount() != 2 || !document.isSelected(firstId)
        || !document.isSelected(secondId)) {
        std::fprintf(stderr, "ctrl-click did not extend the selection (count %d)\n",
                     document.selectedCount());
        return 1;
    }

    const QVector3D firstBefore = document.object(firstId)->position();
    const QVector3D secondBefore = document.object(secondId)->position();
    document.moveSelection(5.0, -3.0);
    const QVector3D firstAfter = document.object(firstId)->position();
    const QVector3D secondAfter = document.object(secondId)->position();
    std::printf("multi-select move: %d objects, first (%.2f, %.2f), second (%.2f, %.2f)\n",
                document.selectedCount(), firstAfter.x(), firstAfter.y(),
                secondAfter.x(), secondAfter.y());
    if (std::abs(firstAfter.x() - firstBefore.x() - 5.0) > 1e-3
        || std::abs(firstAfter.y() - firstBefore.y() + 3.0) > 1e-3
        || std::abs(secondAfter.x() - secondBefore.x() - 5.0) > 1e-3
        || std::abs(secondAfter.y() - secondBefore.y() + 3.0) > 1e-3) {
        std::fprintf(stderr, "moveSelection did not move every selected object by the same delta\n");
        return 1;
    }
    document.undo();
    if (std::abs(document.object(firstId)->position().x() - firstBefore.x()) > 1e-3
        || std::abs(document.object(secondId)->position().x() - secondBefore.x()) > 1e-3) {
        std::fprintf(stderr, "undo did not restore both objects after a multi-select move\n");
        return 1;
    }

    document.selectAll();
    if (document.selectedCount() != document.count()) {
        std::fprintf(stderr, "selectAll selected %d of %d objects\n", document.selectedCount(),
                     document.count());
        return 1;
    }
    document.selectObject(secondId);

    const int copyId = document.duplicateObject(firstId);
    if (copyId < 0 || document.count() != static_cast<int>(models.size()) + 1) {
        std::fprintf(stderr, "duplicate did not add an object (count %d)\n", document.count());
        return 1;
    }
    if (!document.removeObject(copyId) || document.count() != static_cast<int>(models.size())) {
        std::fprintf(stderr, "remove did not take the duplicate back out (count %d)\n",
                     document.count());
        return 1;
    }


    Slic3r::Domain::Model model;
    for (const int id : document.ids()) {
        const PlateDocument::Object* object = document.object(id);
        Slic3r::Domain::ModelObject* engineObject = model.add_object();
        engineObject->name = object->displayName().toStdString();
        const Slic3r::Domain::TriangleMesh mesh = document.worldMesh(id);
        Slic3r::Domain::ModelVolume* volume =
            Slic3r::Biz::Algorithms::ModelVolume::construct_ptr(engineObject, mesh);
        engineObject->add_volume(*volume);
        engineObject->add_instance();
    }
    project.scene_interactor().add_new_objects(model.objects);

    if (project.selected_project().model().objects.size() != static_cast<size_t>(document.count())) {
        std::fprintf(stderr, "the project holds %zu object(s) for %d document object(s)\n",
                     project.selected_project().model().objects.size(), document.count());
        return 1;
    }
    for (const Slic3r::Domain::ModelObject* engineObject :
         project.selected_project().model().objects) {
        const Vec3d offset = engineObject->instances.front()->get_offset();
        std::printf("  engine object \"%s\" instance at (%.2f, %.2f, %.2f)\n",
                    engineObject->name.c_str(), offset.x(), offset.y(), offset.z());
    }

    std::string error;
    if (!sliceAndExport(runtime, gcodePath, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    int layers = 0;
    int moves = 0;
    double minX = 0.0;
    double maxX = 0.0;
    if (!countToolpath(gcodePath, layers, moves, minX, maxX)) {
        std::fprintf(stderr, "the exported toolpath is empty: %s\n", qPrintable(gcodePath));
        return 1;
    }
    std::printf("toolpath: %d layer(s), %d extruding move(s), X spans %.2f..%.2f mm\n",
                layers, moves, minX, maxX);

    const double span = maxX - minX;
    if (span < 61.0) {
        std::fprintf(stderr,
                     "expected both objects, one rotated, in the toolpath (X span > 61 mm), measured %.2f mm\n",
                     span);
        return 1;
    }
    std::printf("scene-selftest passed\n");
    return 0;
}

int runPlateToolsSelftest(const QStringList& models,
                          const QString& configPath,
                          const QString& dataDir)
{
    if (models.isEmpty()) {
        std::fprintf(stderr, "plate-tools needs at least one model\n");
        return 2;
    }

    std::ifstream configFile(configPath.toStdString());
    if (!configFile.is_open()) {
        std::fprintf(stderr, "cannot read config: %s\n", qPrintable(configPath));
        return 2;
    }
    nlohmann::ordered_json configJson;
    configFile >> configJson;

    const auto presetAndConfig = Slic3r::Biz::Config::load_preset_and_config(configJson);
    if (!presetAndConfig.has_value()) {
        std::fprintf(stderr, "config rejected: %s\n", presetAndConfig.error().c_str());
        return 1;
    }

    Slic3r::App::InitParams params;
    params.misc.datadir = dataDir.toStdString();
    Slic3r::App::CLI::CLIRuntime runtime{params};

    ProjectInteractor& project = runtime.project_interactor();
    project.set_undo_provider(std::make_unique<Slic3r::Biz::NoopUndoProvider>());

    const auto projectId = project.new_project_with_preset(presetAndConfig->preset_metadata,
                                                          presetAndConfig->config_pack);
    if (!projectId.has_value()) {
        std::fprintf(stderr, "could not create the project: %s\n", projectId.error().c_str());
        return 1;
    }
    project.select_project(projectId.value());

    const Slic3r::Domain::Bed& bed = project.selected_config_container().bed();
    const Vec2d bedCentre = bed.center();
    const Slic3r::Domain::Vec2d bedExtent = bed.contour_aabb_extent();

    PlateDocument document(QVector2D(bedCentre.x(), bedCentre.y()),
                           QVector2D(bedExtent.x(), bedExtent.y()));

    const QString model = models.first();
    const int first = document.addObject(model, QStringLiteral("first"));
    const int second = document.addObject(model, QStringLiteral("second"));
    if (first < 0 || second < 0) {
        std::fprintf(stderr, "%s\n", qPrintable(document.error()));
        return 1;
    }

    const auto overlaps = [&document](int a, int b) {
        const QVector3D aLow = document.objectBoundsMin(a);
        const QVector3D aHigh = document.objectBoundsMax(a);
        const QVector3D bLow = document.objectBoundsMin(b);
        const QVector3D bHigh = document.objectBoundsMax(b);
        return aLow.x() < bHigh.x() && bLow.x() < aHigh.x()
            && aLow.y() < bHigh.y() && bLow.y() < aHigh.y();
    };

    document.setPosition(second, document.object(first)->position());
    if (!overlaps(first, second)) {
        std::fprintf(stderr, "the overlap precondition failed: both objects should start in the "
                             "same place\n");
        return 1;
    }

    if (!document.arrangePlate()) {
        std::fprintf(stderr, "arrange did not place anything\n");
        return 1;
    }
    if (overlaps(first, second)) {
        std::fprintf(stderr, "arrange left the two objects overlapping\n");
        return 1;
    }

    for (const int id : document.ids()) {
        const QVector3D low = document.objectBoundsMin(id);
        const QVector3D high = document.objectBoundsMax(id);
        const double left = bedCentre.x() - bedExtent.x() * 0.5;
        const double right = bedCentre.x() + bedExtent.x() * 0.5;
        const double back = bedCentre.y() - bedExtent.y() * 0.5;
        const double front = bedCentre.y() + bedExtent.y() * 0.5;
        if (low.x() < left - 0.01 || high.x() > right + 0.01 || low.y() < back - 0.01
            || high.y() > front + 0.01) {
            std::fprintf(stderr, "object %d was arranged off the bed: x %.2f..%.2f, y %.2f..%.2f\n",
                         id, low.x(), high.x(), low.y(), high.y());
            return 1;
        }
    }
    std::printf("arrange: two overlapping cubes separated, both inside the %.0f x %.0f mm bed\n",
                bedExtent.x(), bedExtent.y());

    document.setRotation(first, QVector3D(37.0, 21.0, 11.0));
    const QVector3D tiltedLow = document.objectBoundsMin(first);
    const QVector3D tiltedHigh = document.objectBoundsMax(first);
    const double tiltedHeight = tiltedHigh.z() - tiltedLow.z();
    if (!document.autoOrient(first)) {
        std::fprintf(stderr, "auto-orient did not produce a rotation\n");
        return 1;
    }
    const QVector3D orientedLow = document.objectBoundsMin(first);
    const QVector3D orientedHigh = document.objectBoundsMax(first);
    const double orientedHeight = orientedHigh.z() - orientedLow.z();
    std::printf("auto-orient: %.2f mm tall tilted, %.2f mm tall resting, base at Z %.2f\n",
                tiltedHeight, orientedHeight, orientedLow.z());
    if (std::abs(orientedLow.z()) > 0.05) {
        std::fprintf(stderr, "auto-orient left the object %.2f mm off the bed\n", orientedLow.z());
        return 1;
    }
    if (orientedHeight > tiltedHeight + 0.01) {
        std::fprintf(stderr, "auto-orient made the object taller (%.2f vs %.2f mm)\n",
                     orientedHeight, tiltedHeight);
        return 1;
    }

    const auto mesh = document.object(first)->mesh;
    int downFacet = -1;
    for (size_t facet = 0; facet < mesh->its.indices.size(); ++facet) {
        const auto& face = mesh->its.indices[facet];
        const Vec3d a = mesh->its.vertices[face[0]].cast<double>();
        const Vec3d b = mesh->its.vertices[face[1]].cast<double>();
        const Vec3d c = mesh->its.vertices[face[2]].cast<double>();
        const Vec3d normal = (b - a).cross(c - a).normalized();
        if (normal.z() < -0.99) {
            downFacet = static_cast<int>(facet);
            break;
        }
    }
    if (downFacet < 0) {
        std::fprintf(stderr, "the test mesh has no downward face to place on\n");
        return 1;
    }
    document.setRotation(first, QVector3D(0.0, 0.0, 0.0));
    if (!document.placeOnFace(first, downFacet)) {
        std::fprintf(stderr, "place-on-face refused facet %d\n", downFacet);
        return 1;
    }
    const QVector3D placedLow = document.objectBoundsMin(first);
    std::printf("place-on-face: facet %d laid flat, base at Z %.2f\n", downFacet, placedLow.z());
    if (std::abs(placedLow.z()) > 0.05) {
        std::fprintf(stderr, "place-on-face left the object %.2f mm off the bed\n", placedLow.z());
        return 1;
    }

    std::printf("plate-tools-selftest passed\n");
    return 0;
}


int runSliceFeaturesSelftest(const QString& model,
                             const QString& configPath,
                             const QString& dataDir)
{
    if (model.isEmpty()) {
        std::fprintf(stderr, "slice-features needs a model\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::ifstream configFile(configPath.toStdString());
    if (!configFile.is_open()) {
        std::fprintf(stderr, "cannot read config: %s\n", qPrintable(configPath));
        return 2;
    }
    nlohmann::ordered_json configJson;
    configFile >> configJson;

    // The bed comes out of the resolved config rather than a second engine
    // runtime: this process may only ever create one.
    double minX = 0.0;
    double maxX = 0.0;
    double minY = 0.0;
    double maxY = 0.0;
    bool firstPoint = true;
    const auto& printer = configJson["configuration"]["printer_settings"];
    for (const auto& point : printer.value("bed_shape", nlohmann::ordered_json::array())) {
        double x = 0.0;
        double y = 0.0;
        if (point.is_array() && point.size() >= 2) {
            x = point[0].get<double>();
            y = point[1].get<double>();
        } else if (point.is_string()) {
            std::istringstream stream(point.get<std::string>());
            if (!(stream >> x >> y))
                continue;
        } else {
            continue;
        }
        minX = firstPoint ? x : std::min(minX, x);
        maxX = firstPoint ? x : std::max(maxX, x);
        minY = firstPoint ? y : std::min(minY, y);
        maxY = firstPoint ? y : std::max(maxY, y);
        firstPoint = false;
    }
    if (firstPoint) {
        std::fprintf(stderr, "the config has no bed_shape\n");
        return 1;
    }

    PlateDocument document(QVector2D((minX + maxX) * 0.5, (minY + maxY) * 0.5),
                           QVector2D(maxX - minX, maxY - minY));
    const int id = document.addObject(model);
    if (id < 0) {
        std::fprintf(stderr, "%s\n", qPrintable(document.error()));
        return 1;
    }

    const auto sliceNow = [&document, &configPath](int& layers, int& moves, int& supports) {
        next_layer::SliceRuntime& slicer = next_layer::SliceRuntime::instance();
        const auto directoryScratch = next_layer::makeScratchDir();
        QTemporaryDir& directory = *directoryScratch;
        const QString gcode = directory.filePath(QStringLiteral("slice.gcode"));
        QString error;
        if (!slicer.slicePlate(document.sliceObjects(), configPath, gcode, error)) {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            return false;
        }
        QFile file(gcode);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;
        layers = 0;
        moves = 0;
        supports = 0;
        bool inSupport = false;
        while (!file.atEnd()) {
            const QByteArray line = file.readLine();
            if (line.startsWith(";LAYER_CHANGE")) {
                ++layers;
            } else if (line.startsWith(";TYPE:")) {
                inSupport = line.contains("Support material");
            } else if (line.startsWith("G1 ") && line.contains(" E")) {
                ++moves;
                if (inSupport)
                    ++supports;
            }
        }
        return layers > 0;
    };

    int baseLayers = 0;
    int baseMoves = 0;
    int baseSupports = 0;
    if (!sliceNow(baseLayers, baseMoves, baseSupports)) {
        std::fprintf(stderr, "the baseline plate did not slice\n");
        return 1;
    }
    std::printf("plate slice: %d layer(s), %d extruding move(s)\n", baseLayers, baseMoves);

    if (!document.setObjectSetting(id, QStringLiteral("layer_height"), 0.3)) {
        std::fprintf(stderr, "the object refused a layer_height override\n");
        return 1;
    }
    int overrideLayers = 0;
    int overrideMoves = 0;
    int overrideSupports = 0;
    if (!sliceNow(overrideLayers, overrideMoves, overrideSupports)) {
        std::fprintf(stderr, "the plate did not slice with a per-object layer height\n");
        return 1;
    }
    std::printf("per-object layer_height 0.30: %d layer(s) (was %d)\n", overrideLayers, baseLayers);
    if (overrideLayers >= baseLayers || overrideLayers < 2) {
        std::fprintf(stderr,
                     "a 0.30 mm per-object layer height produced %d layers, the 0.20 mm object "
                     "produced %d: the override did not reach the slicer\n",
                     overrideLayers, baseLayers);
        return 1;
    }
    document.clearObjectSettings(id);

    const QVector3D low = document.objectBoundsMin(id);
    const QVector3D high = document.objectBoundsMax(id);
    const double bottom = low.z() + (high.z() - low.z()) * 0.25;
    const double top = low.z() + (high.z() - low.z()) * 0.75;
    if (!document.setLayerHeightRange(id, bottom, top, 0.1)) {
        std::fprintf(stderr, "the object refused a layer height profile\n");
        return 1;
    }
    int profileLayers = 0;
    int profileMoves = 0;
    int profileSupports = 0;
    if (!sliceNow(profileLayers, profileMoves, profileSupports)) {
        std::fprintf(stderr, "the plate did not slice with a layer height profile\n");
        return 1;
    }
    std::printf("variable layer height 0.10 mm between %.1f and %.1f mm: %d layer(s) (was %d)\n",
                bottom, top, profileLayers, baseLayers);
    if (profileLayers <= baseLayers) {
        std::fprintf(stderr,
                     "a 0.10 mm band through half the object produced %d layers, the uniform "
                     "0.20 mm slice produced %d: the profile did not reach the slicer\n",
                     profileLayers, baseLayers);
        return 1;
    }
    document.clearLayerHeights(id);

    // Per-object supports, asserted the decisive way round. Comparing "supports
    // everywhere" with "supports only under the painted facets" proves nothing on
    // a model whose overhang is one flat span -- both support the same area and
    // the counts come out equal, which reads as a failure whichever way it went.
    // Turning them off for this object has one answer: no support moves at all.
    if (!document.setObjectSetting(id, QStringLiteral("support_material"),
                                   QStringLiteral("none"))) {
        std::fprintf(stderr, "the object refused a support_material override\n");
        return 1;
    }
    int noneLayers = 0;
    int noneMoves = 0;
    int noneSupports = 0;
    if (!sliceNow(noneLayers, noneMoves, noneSupports)) {
        std::fprintf(stderr, "the plate did not slice with per-object supports off\n");
        return 1;
    }
    std::printf("per-object support_material none: %d support move(s) (was %d)\n",
                noneSupports, baseSupports);
    if (baseSupports == 0) {
        // Nothing to prove either way: a profile that generates no supports cannot
        // show that turning them off per object did anything. The gate skips with
        // this line rather than reading the silence as a pass.
        std::printf("support overrides skipped: this profile generates no support\n");
        document.clearObjectSettings(id);
        return 0;
    }
    if (noneSupports != 0) {
        std::fprintf(stderr,
                     "per-object support_material none left %d support move(s) of %d: the "
                     "override did not reach the slicer\n",
                     noneSupports, baseSupports);
        return 1;
    }

    // Painting: the object is told to generate supports only where they are
    // painted ("Manual Only"), then every downward-facing facet above the bed is
    // marked as an enforcer. With nothing painted that mode produces no support
    // at all, so support moves can only come from the annotation reaching the
    // engine.
    if (!document.setObjectSetting(id, QStringLiteral("support_material"),
                                   QStringLiteral("enforcers_only"))) {
        std::fprintf(stderr, "the object refused a support_material override\n");
        return 1;
    }

    const auto mesh = document.object(id)->mesh;
    const Slic3r::Domain::Transform3d trafo = document.object(id)->trafo.get_matrix();
    int enforcers = 0;
    for (size_t facet = 0; facet < mesh->its.indices.size(); ++facet) {
        const auto& face = mesh->its.indices[facet];
        const Vec3d a = mesh->its.vertices[face[0]].cast<double>();
        const Vec3d b = mesh->its.vertices[face[1]].cast<double>();
        const Vec3d c = mesh->its.vertices[face[2]].cast<double>();
        const Vec3d normal = (trafo.linear() * (b - a).cross(c - a)).normalized();
        const Vec3d centre3 = trafo * ((a + b + c) / 3.0);
        if (normal.z() < -0.9 && centre3.z() > 1.0) {
            enforcers += document.paintObject(id, QStringLiteral("supports"),
                                              QVector3D(centre3.x(), centre3.y(), centre3.z()),
                                              1.5, false, 2, static_cast<int>(facet));
        }
    }
    if (enforcers == 0) {
        std::fprintf(stderr,
                     "the model has no downward-facing facet above the bed to paint: run this "
                     "with a model that overhangs (the bridge, not a cube)\n");
        return 1;
    }
    std::printf("painted %d downward facet(s) as support enforcers\n", enforcers);

    int paintedLayers = 0;
    int paintedMoves = 0;
    int paintedSupports = 0;
    if (!sliceNow(paintedLayers, paintedMoves, paintedSupports)) {
        std::fprintf(stderr, "the plate did not slice with painted facets\n");
        return 1;
    }
    std::printf("support enforcer paint: %d support move(s) (was %d)\n",
                paintedSupports, baseSupports);
    if (paintedSupports == 0) {
        std::fprintf(stderr,
                     "the painted facets produced no support moves: the paint did not reach "
                     "the slicer\n");
        return 1;
    }

    // A custom G-code marker: the layer-slider right-click writes one, and the
    // only proof that it is real is the line appearing in the toolpath at the
    // height it was attached to.
    document.clearPaint(id, QStringLiteral("supports"));
    document.clearObjectSetting(id, QStringLiteral("support_material"));
    const QString marker = QStringLiteral("M117 NEXT_LAYER_MARKER");
    if (!document.addLayerGcode(2.0, QStringLiteral("gcode"), marker)) {
        std::fprintf(stderr, "the plate refused a custom G-code marker\n");
        return 1;
    }
    int markerLayers = 0;
    int markerMoves = 0;
    int markerSupports = 0;
    if (!sliceNow(markerLayers, markerMoves, markerSupports)) {
        std::fprintf(stderr, "the plate did not slice with a custom G-code marker\n");
        return 1;
    }
    const auto markerDirScratch = next_layer::makeScratchDir();
    QTemporaryDir& markerDir = *markerDirScratch;
    const QString markerGcode = markerDir.filePath(QStringLiteral("marker.gcode"));
    {
        QString error;
        if (!next_layer::SliceRuntime::instance().slicePlate(document.sliceObjects(), configPath,
                                                             markerGcode, error,
                                                             document.layerGcodeList())) {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            return 1;
        }
    }
    int occurrences = 0;
    int markerLayerIndex = -1;
    int layerSeen = 0;
    QFile markerFile(markerGcode);
    if (!markerFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "the custom G-code slice produced no file\n");
        return 1;
    }
    while (!markerFile.atEnd()) {
        const QByteArray line = markerFile.readLine();
        if (line.startsWith(";LAYER_CHANGE"))
            ++layerSeen;
        if (line.contains(marker.toUtf8())) {
            ++occurrences;
            if (markerLayerIndex < 0)
                markerLayerIndex = layerSeen;
        }
    }
    std::printf("custom G-code at 2.00 mm: %d occurrence(s), inside layer %d\n", occurrences,
                markerLayerIndex);
    if (occurrences != 1 || markerLayerIndex < 5) {
        std::fprintf(stderr,
                     "a custom G-code marker at 2.00 mm appeared %d time(s) in layer %d: the "
                     "engine did not receive it\n",
                     occurrences, markerLayerIndex);
        return 1;
    }
    document.clearLayerGcodes();

    std::printf("slice-features-selftest passed\n");
    return 0;
}


namespace {

/// The bed of the resolved config, which is what a headless test has to build a
/// document on: the engine may only be created once per process.
bool bedOfConfig(const QString& configPath, QVector2D& centre, QVector2D& size)
{
    std::ifstream configFile(configPath.toStdString());
    if (!configFile.is_open())
        return false;
    nlohmann::ordered_json configJson;
    configFile >> configJson;

    double minX = 0.0;
    double maxX = 0.0;
    double minY = 0.0;
    double maxY = 0.0;
    bool first = true;
    for (const auto& point :
         configJson["configuration"]["printer_settings"].value("bed_shape",
                                                              nlohmann::ordered_json::array())) {
        if (!point.is_array() || point.size() < 2)
            continue;
        const double x = point[0].get<double>();
        const double y = point[1].get<double>();
        minX = first ? x : std::min(minX, x);
        maxX = first ? x : std::max(maxX, x);
        minY = first ? y : std::min(minY, y);
        maxY = first ? y : std::max(maxY, y);
        first = false;
    }
    if (first)
        return false;
    centre = QVector2D((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
    size = QVector2D(maxX - minX, maxY - minY);
    return true;
}

struct SliceResult
{
    int layers = 0;
    int moves = 0;
    double extruded = 0.0;
};

/// Filament is the measure that matters for a boolean: a hole through a box adds
/// perimeters around it, so the move count goes up while the amount of plastic
/// goes down. The engine writes relative extrusion (M83), so the E values add up
/// directly.
bool slicePlate(PlateDocument& document, const QString& configPath, SliceResult& result)
{
    const auto directoryScratch = next_layer::makeScratchDir();
    QTemporaryDir& directory = *directoryScratch;
    const QString gcode = directory.filePath(QStringLiteral("shapes.gcode"));
    QString error;
    if (!next_layer::SliceRuntime::instance().slicePlate(document.sliceObjects(), configPath,
                                                         gcode, error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return false;
    }
    QFile file(gcode);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    result = SliceResult();
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        if (line.startsWith(";LAYER_CHANGE")) {
            ++result.layers;
            continue;
        }
        if (!line.startsWith("G1 "))
            continue;
        const int e = line.indexOf(" E");
        if (e < 0)
            continue;
        ++result.moves;
        const double value = line.mid(e + 2).trimmed().toDouble();
        if (value > 0.0)
            result.extruded += value;
    }
    return result.layers > 0;
}

}

int runMultiToolSelftest(const QString& configPath, const QString& dataDir)
{
    Q_UNUSED(dataDir)
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    QVector2D centre;
    QVector2D size;
    if (!bedOfConfig(configPath, centre, size)) {
        std::fprintf(stderr, "cannot read the bed out of %s\n", qPrintable(configPath));
        return 2;
    }

    PlateDocument document(centre, size);
    const int id = document.addShape(QStringLiteral("box"), 30.0, 30.0, 20.0);
    if (id < 0) {
        std::fprintf(stderr, "%s\n", qPrintable(document.error()));
        return 1;
    }
    document.setToolColours(QVariantList{QStringLiteral("#d8452c"), QStringLiteral("#2c6fd8"),
                                         QStringLiteral("#2cd89f"), QStringLiteral("#d8cf2c")});

    const auto sliceNow = [&document, &configPath](int& toolChanges, QList<int>& tools,
                                                   QList<QByteArray>& colours) {
        const auto directoryScratch = next_layer::makeScratchDir();
        QTemporaryDir& directory = *directoryScratch;
        const QString gcode = directory.filePath(QStringLiteral("multi.gcode"));
        QString error;
        if (!next_layer::SliceRuntime::instance().slicePlate(document.sliceObjects(), configPath,
                                                             gcode, error,
                                                             document.layerGcodeList())) {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            return false;
        }
        QFile file(gcode);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;
        toolChanges = 0;
        tools.clear();
        colours.clear();
        int current = -1;
        while (!file.atEnd()) {
            const QByteArray line = file.readLine();
            // The header lists the tools; every tool change in the body is a line
            // of its own. Both are the engine's own numbers, not ours.
            if (line.startsWith("; filament_type = ")) {
                for (const QByteArray& type : line.mid(18).trimmed().split(';'))
                    tools.append(type.trimmed().isEmpty() ? 0 : 1);
            } else if (line.startsWith("; extruder_colour = ")) {
                for (const QByteArray& colour : line.mid(20).trimmed().split(';'))
                    colours.append(colour.trimmed());
            } else if (line.startsWith("T") && line.size() > 1 && line.at(1) >= '0' && line.at(1) <= '9') {
                const int tool = line.mid(1).trimmed().toInt();
                if (tool != current) {
                    ++toolChanges;
                    current = tool;
                }
            }
        }
        return true;
    };

    int baseChanges = 0;
    QList<int> baseTools;
    QList<QByteArray> baseColours;
    if (!sliceNow(baseChanges, baseTools, baseColours)) {
        std::fprintf(stderr, "the plate did not slice\n");
        return 1;
    }
    if (baseTools.size() < 2) {
        std::fprintf(stderr,
                     "the config resolves %lld tool(s): resolve a multi-tool printer with "
                     "--filaments before running this\n",
                     static_cast<long long>(baseTools.size()));
        return 1;
    }
    std::printf("two-tool plate: %lld tool(s), %d tool change(s) before painting\n",
                static_cast<long long>(baseTools.size()), baseChanges);

    // Paint the top of the box with tool 2: every upward-facing facet. A tool
    // changer prints that in the second material, so the G-code has to change
    // tools and the colour has to be the second one.
    const auto mesh = document.object(id)->mesh;
    const Slic3r::Domain::Transform3d trafo = document.object(id)->trafo.get_matrix();
    int painted = 0;
    for (size_t facet = 0; facet < mesh->its.indices.size(); ++facet) {
        const auto& face = mesh->its.indices[facet];
        const Vec3d a = mesh->its.vertices[face[0]].cast<double>();
        const Vec3d b = mesh->its.vertices[face[1]].cast<double>();
        const Vec3d c = mesh->its.vertices[face[2]].cast<double>();
        const Vec3d normal = (trafo.linear() * (b - a).cross(c - a)).normalized();
        const Vec3d centre3 = trafo * ((a + b + c) / 3.0);
        if (normal.z() > 0.9) {
            painted += document.paintObject(id, QStringLiteral("tool"),
                                            QVector3D(centre3.x(), centre3.y(), centre3.z()),
                                            30.0, false, 2, static_cast<int>(facet));
        }
    }
    if (painted == 0) {
        std::fprintf(stderr, "no upward-facing facet was painted\n");
        return 1;
    }
    std::printf("painted %d upward facet(s) with tool 2\n", painted);

    int paintedChanges = 0;
    QList<int> paintedTools;
    QList<QByteArray> paintedColours;
    if (!sliceNow(paintedChanges, paintedTools, paintedColours)) {
        std::fprintf(stderr, "the plate did not slice with a painted tool\n");
        return 1;
    }
    std::printf("painted slice: %d tool change(s), %lld tool(s) in the header\n",
                paintedChanges, static_cast<long long>(paintedTools.size()));

    if (paintedChanges <= baseChanges) {
        std::fprintf(stderr,
                     "tool 2 was painted on %d facet(s) but the G-code has %d tool change(s), "
                     "not more than the %d of the unpainted plate: the paint never reached the "
                     "engine\n",
                     painted, paintedChanges, baseChanges);
        return 1;
    }
    if (paintedColours.size() < 2 || paintedColours.at(0) == paintedColours.at(1)) {
        std::fprintf(stderr, "the two tools carry the same colour: %s\n",
                     paintedColours.isEmpty() ? "(none)" : paintedColours.at(0).constData());
        return 1;
    }

    std::printf("multi-tool: %d tool change(s) (was %d), colours %s / %s\n", paintedChanges,
                baseChanges, paintedColours.at(0).constData(), paintedColours.at(1).constData());
    return 0;
}

int runVolumeSettingsSelftest(const QString& configPath, const QString& dataDir)
{
    Q_UNUSED(dataDir)
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    QVector2D centre;
    QVector2D size;
    if (!bedOfConfig(configPath, centre, size)) {
        std::fprintf(stderr, "cannot read the bed out of %s\n", qPrintable(configPath));
        return 2;
    }

    PlateDocument document(centre, size);
    const int box = document.addShape(QStringLiteral("box"), 40.0, 40.0, 20.0);
    if (box < 0) {
        std::fprintf(stderr, "%s\n", qPrintable(document.error()));
        return 1;
    }

    SliceResult base;
    if (!slicePlate(document, configPath, base)) {
        std::fprintf(stderr, "the plain box did not slice\n");
        return 1;
    }

    const int volume = document.addVolume(box, QStringLiteral("box"), QStringLiteral("modifier"),
                                          20.0, 20.0, 10.0);
    if (volume <= 0) {
        std::fprintf(stderr, "%s\n", qPrintable(document.error()));
        return 1;
    }

    SliceResult untouched;
    if (!slicePlate(document, configPath, untouched)) {
        std::fprintf(stderr, "the box with a plain modifier did not slice\n");
        return 1;
    }

    QVariantMap density;
    density.insert(QStringLiteral("is_percent"), true);
    density.insert(QStringLiteral("value"), 60);
    if (!document.setVolumeSetting(box, volume, QStringLiteral("fill_density"), density)) {
        std::fprintf(stderr, "the modifier refused a fill_density override\n");
        return 1;
    }
    SliceResult dense;
    if (!slicePlate(document, configPath, dense)) {
        std::fprintf(stderr, "the box with a dense modifier did not slice\n");
        return 1;
    }

    std::printf("box %.1f mm, + plain modifier %.1f mm, + modifier at 60 %% infill %.1f mm\n",
                base.extruded, untouched.extruded, dense.extruded);
    if (dense.extruded <= untouched.extruded * 1.02) {
        std::fprintf(stderr,
                     "a modifier with its own 60 %% infill printed %.1f mm against %.1f mm "
                     "without it: the volume settings never reached the engine\n",
                     dense.extruded, untouched.extruded);
        return 1;
    }

    if (!document.clearVolumeSetting(box, volume, QStringLiteral("fill_density"))) {
        std::fprintf(stderr, "the modifier refused to drop its override\n");
        return 1;
    }
    SliceResult restored;
    if (!slicePlate(document, configPath, restored)) {
        std::fprintf(stderr, "the box did not slice after dropping the override\n");
        return 1;
    }
    if (std::abs(restored.extruded - untouched.extruded) > 0.5) {
        std::fprintf(stderr, "dropping the override left %.1f mm against %.1f mm\n",
                     restored.extruded, untouched.extruded);
        return 1;
    }

    std::printf("volume settings: %.1f -> %.1f -> %.1f mm of filament\n", untouched.extruded,
                dense.extruded, restored.extruded);
    return 0;
}

int runMeshToolsSelftest(const QString& configPath, const QString& dataDir)
{
    Q_UNUSED(dataDir)
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    QVector2D centre;
    QVector2D size;
    if (!bedOfConfig(configPath, centre, size)) {
        std::fprintf(stderr, "cannot read the bed out of %s\n", qPrintable(configPath));
        return 2;
    }

    PlateDocument document(centre, size);
    const int box = document.addShape(QStringLiteral("box"), 20.0, 20.0, 20.0);
    if (box < 0) {
        std::fprintf(stderr, "%s\n", qPrintable(document.error()));
        return 1;
    }

    // Statistics: a 20 mm cube is 8000 mm3 inside 2400 mm2 of surface, and the
    // numbers have to come from the mesh, not from the primitive's parameters.
    const QVariantMap stats = document.objectStatistics(box);
    const double volume = stats.value(QStringLiteral("volume")).toDouble();
    const double area = stats.value(QStringLiteral("area")).toDouble();
    const int facets = stats.value(QStringLiteral("facets")).toInt();
    std::printf("statistics: %.1f mm3, %.1f mm2, %d facets\n", volume, area, facets);
    if (std::abs(volume - 8000.0) > 1.0 || std::abs(area - 2400.0) > 1.0 || facets < 12) {
        std::fprintf(stderr, "a 20 mm cube measured %.1f mm3 and %.1f mm2 over %d facets\n",
                     volume, area, facets);
        return 1;
    }

    // Hollowing: the same cube with a 2 mm wall is a shell, so the enclosed
    // volume drops to the wall itself while the bounding box stays a cube.
    SliceResult solid;
    if (!slicePlate(document, configPath, solid)) {
        std::fprintf(stderr, "the plain box did not slice\n");
        return 1;
    }
    if (!document.hollowObject(box, 2.0)) {
        std::fprintf(stderr, "hollowing failed: %s\n", qPrintable(document.error()));
        return 1;
    }
    const QVariantMap hollowStats = document.objectStatistics(box);
    const double hollowVolume = hollowStats.value(QStringLiteral("volume")).toDouble();
    const double hollowHeight = hollowStats.value(QStringLiteral("height")).toDouble();
    std::printf("hollow: %.1f mm3 inside a %.1f mm tall shell\n", hollowVolume, hollowHeight);
    if (hollowVolume >= volume * 0.9 || hollowHeight < 19.0) {
        std::fprintf(stderr, "hollowing left %.1f mm3 of %.1f mm3 and height %.1f\n",
                     hollowVolume, volume, hollowHeight);
        return 1;
    }

    // Text: a line of glyphs has to become a printable solid of the height that was
    // asked for, and it has to slice -- a font that renders nothing looks identical
    // to a success until the slicer rejects it.
    {
        PlateDocument typesetter(centre, size);
        const QVariantList fontList = typesetter.fonts();
        if (fontList.isEmpty()) {
            std::fprintf(stderr, "no fonts to emboss with\n");
            return 1;
        }
        const QVariantMap font = fontList.first().toMap();
        const int text = typesetter.addText(QStringLiteral("next-layer"), 10.0, 1.2,
                                           font.value(QStringLiteral("path")).toString());
        if (text < 0) {
            std::fprintf(stderr, "text failed: %s\n", qPrintable(typesetter.error()));
            return 1;
        }
        // The glyphs lie in X/Y and the extrusion is Z, so the requested text
        // height is the object's depth and the extrusion is its height.
        const QVariantMap stats = typesetter.objectStatistics(text);
        const double statsHeight = stats.value(QStringLiteral("depth")).toDouble();
        const double statsWidth = stats.value(QStringLiteral("width")).toDouble();
        const double statsVolume = stats.value(QStringLiteral("volume")).toDouble();
        const double statsThickness = stats.value(QStringLiteral("height")).toDouble();
        SliceResult sliced;
        const bool slices = slicePlate(typesetter, configPath, sliced);
        std::printf("text: %d fonts, \"next-layer\" is %.1f wide by %.1f tall and %.1f mm "
                    "thick at %.1f mm3, %s\n", int(fontList.size()), statsWidth, statsHeight,
                    statsThickness, statsVolume, slices ? "slices" : "does not slice");
        if (std::abs(statsHeight - 10.0) > 0.1 || std::abs(statsThickness - 1.2) > 0.05
            || statsWidth < 20.0 || statsVolume <= 0.0 || !slices) {
            std::fprintf(stderr,
                         "text measured %.1f wide, %.1f tall, %.1f thick, %.1f mm3 and %s\n",
                         statsWidth, statsHeight, statsThickness, statsVolume,
                         slices ? "sliced" : "did not slice");
            return 1;
        }
    }

    // Cut: a 20 mm cube cut at 8 mm is two solids of 8 and 12 mm, each closed, and
    // their volumes still add up to the original. A cut that left a half open would
    // either lose volume or the slice would fill the gap.
    PlateDocument cutter(centre, size);
    const int whole = cutter.addShape(QStringLiteral("box"), 20.0, 20.0, 20.0);
    if (whole < 0) {
        std::fprintf(stderr, "%s\n", qPrintable(cutter.error()));
        return 1;
    }
    const int parts = cutter.cutAtZ(whole, 8.0, true);
    if (parts != 2 || cutter.count() != 2) {
        std::fprintf(stderr, "cutting at 8 mm gave %d object(s) from %d\n", cutter.count(), whole);
        return 1;
    }
    const QVariantList cutStats = cutter.objects();
    double cutVolume = 0.0;
    double lowest = 1e9;
    double highest = -1e9;
    for (const QVariant& entry : cutStats) {
        const QVariantMap info = entry.toMap();
        const QVariantMap stats = cutter.objectStatistics(info.value(QStringLiteral("id")).toInt());
        cutVolume += stats.value(QStringLiteral("volume")).toDouble();
        const double height = stats.value(QStringLiteral("height")).toDouble();
        lowest = std::min(lowest, height);
        highest = std::max(highest, height);
    }
    std::printf("cut: 2 parts of %.1f and %.1f mm, %.1f mm3 together\n", lowest, highest,
                cutVolume);
    if (std::abs(lowest - 8.0) > 0.01 || std::abs(highest - 12.0) > 0.01
        || std::abs(cutVolume - 8000.0) > 8.0) {
        std::fprintf(stderr,
                     "a cube cut at 8 mm gave parts of %.1f and %.1f mm holding %.1f mm3\n",
                     lowest, highest, cutVolume);
        return 1;
    }

    // Split: two cubes merged into one mesh are two shells, so the split has to
    // put one object back and add exactly one more.
    PlateDocument plate(centre, size);
    const int left = plate.addShape(QStringLiteral("box"), 10.0, 10.0, 10.0);
    const int right = plate.addShape(QStringLiteral("box"), 10.0, 10.0, 10.0);
    if (left < 0 || right < 0) {
        std::fprintf(stderr, "%s\n", qPrintable(plate.error()));
        return 1;
    }
    if (!plate.mergeObjects({left, right})) {
        std::fprintf(stderr, "merging two boxes failed: %s\n", qPrintable(plate.error()));
        return 1;
    }
    if (plate.count() != 1) {
        std::fprintf(stderr, "two boxes merged into %d objects\n", plate.count());
        return 1;
    }
    const int added = plate.splitToParts(left);
    std::printf("split: 1 object -> %d objects\n", plate.count());
    if (added != 1 || plate.count() != 2) {
        std::fprintf(stderr, "splitting a two-shell mesh added %d objects, leaving %d\n",
                     added, plate.count());
        return 1;
    }

    return 0;
}

int runCalibrationSelftest(const QString& configPath, const QString& dataDir)
{
    Q_UNUSED(dataDir)
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    QVector2D centre;
    QVector2D size;
    if (!bedOfConfig(configPath, centre, size)) {
        std::fprintf(stderr, "cannot read the bed out of %s\n", qPrintable(configPath));
        return 2;
    }

    PlateDocument document(centre, size);
    SettingsModel settings;
    {
        QFile configFile(configPath);
        if (configFile.open(QIODevice::ReadOnly | QIODevice::Text))
            settings.seedFromConfigJson(QString::fromUtf8(configFile.readAll()));
    }
    next_layer::Calibration calibration(&document, &settings);

    struct Expectation
    {
        const char* kind;
        const char* marker;
        int minimumBands;
        int minimumObjects;
        QVariantMap params;
    };
    const Expectation expectations[] = {
        {"temperature", "M104 S", 6, 1, {}},
        {"pressure_advance", "M900 K", 6, 1, {{QStringLiteral("method"), QStringLiteral("tower")}}},
        {"pressure_advance", "M900 K", 6, 1, {{QStringLiteral("method"), QStringLiteral("line")}}},
        {"pressure_advance", "M900 K", 6, 1, {{QStringLiteral("method"), QStringLiteral("pattern")}}},
        {"flow", "M221 S", 5, 6, {}},
        {"max_flow", "M220 S", 6, 6, {}},
        {"vfa", "M220 S", 6, 6, {}},
        {"cornering", "M205 J", 6, 6, {}},
        {"input_shaping_freq", "SET_INPUT_SHAPER", 6, 6, {}},
        {"input_shaping_damp", "SET_INPUT_SHAPER", 4, 6, {}},
        {"retraction", "M207 S", 6, 6, {}},
        {"first_layer", "", 6, 6, {}},
    };

    int built = 0;
    for (const Expectation& expectation : expectations) {
        const QString message = calibration.build(QString::fromLatin1(expectation.kind),
                                                  expectation.params);
        if (message.isEmpty() || message.contains(QLatin1String("not a calibration test"))) {
            std::fprintf(stderr, "%s: %s\n", expectation.kind, qPrintable(message));
            return 1;
        }
        if (document.count() < expectation.minimumObjects) {
            std::fprintf(stderr, "%s built %d object(s), expected at least %d\n", expectation.kind,
                         document.count(), expectation.minimumObjects);
            return 1;
        }
        ++built;

        const auto directoryScratch = next_layer::makeScratchDir();
        QTemporaryDir& directory = *directoryScratch;
        const QString gcode = directory.filePath(QStringLiteral("calibration.gcode"));
        QString error;
        if (!next_layer::SliceRuntime::instance().slicePlate(document.sliceObjects(), configPath,
                                                             gcode, error,
                                                             document.layerGcodeList())) {
            std::fprintf(stderr, "%s did not slice: %s\n", expectation.kind, qPrintable(error));
            return 1;
        }

        QFile file(gcode);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return 1;
        int markers = 0;
        QList<double> temperatures;
        while (!file.atEnd()) {
            const QByteArray line = file.readLine();
            if (!line.contains(expectation.marker))
                continue;
            ++markers;
            if (line.startsWith("M104 S")) {
                const int valueStart = 6;
                const int stop = line.indexOf(' ', valueStart);
                temperatures.append(line.mid(valueStart, stop - valueStart).toDouble());
            }
        }
        std::printf("%s: %d object(s), %d band command(s) in the G-code -- %s\n", expectation.kind,
                    document.count(), markers, message.toUtf8().constData());
        if (!expectation.marker[0])
            continue;
        if (markers < expectation.minimumBands) {
            std::fprintf(stderr,
                         "%s: only %d of %d band commands reached the G-code, so the tower does "
                         "not change anything between bands\n",
                         expectation.kind, markers, expectation.minimumBands);
            return 1;
        }
        if (temperatures.size() >= 2 && temperatures.first() <= temperatures.last()) {
            std::fprintf(stderr, "%s: the bands do not descend in temperature (%g then %g)\n",
                         expectation.kind, temperatures.first(), temperatures.last());
            return 1;
        }
    }

    std::printf("calibration: %d test(s) built and sliced\n", built);
    return 0;
}

int runShapeSelftest(const QString& configPath, const QString& dataDir)
{
    Q_UNUSED(dataDir)
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    QVector2D centre;
    QVector2D size;
    if (!bedOfConfig(configPath, centre, size)) {
        std::fprintf(stderr, "cannot read the bed out of %s\n", qPrintable(configPath));
        return 2;
    }

    PlateDocument document(centre, size);
    const int box = document.addShape(QStringLiteral("box"), 40.0, 40.0, 20.0);
    if (box < 0) {
        std::fprintf(stderr, "%s\n", qPrintable(document.error()));
        return 1;
    }

    SliceResult base;
    if (!slicePlate(document, configPath, base)) {
        std::fprintf(stderr, "the plate with one added box did not slice\n");
        return 1;
    }
    std::printf("added box 40x40x20: %d layer(s), %d extruding move(s), %.1f mm filament\n",
                base.layers, base.moves, base.extruded);
    if (base.layers < 5 || base.moves < 100 || base.extruded <= 0.0) {
        std::fprintf(stderr, "a 40x40x20 box sliced to %d layer(s), %d move(s), %.1f mm\n",
                     base.layers, base.moves, base.extruded);
        return 1;
    }

    const int hole = document.addVolume(box, QStringLiteral("cylinder"), QStringLiteral("negative"),
                                        24.0, 24.0, 40.0);
    if (hole < 0) {
        std::fprintf(stderr, "%s\n", qPrintable(document.error()));
        return 1;
    }
    SliceResult cut;
    if (!slicePlate(document, configPath, cut)) {
        std::fprintf(stderr, "the plate with a negative volume did not slice\n");
        return 1;
    }
    std::printf("negative cylinder 24 mm through it: %d move(s), %.1f mm filament (was %.1f mm)\n",
                cut.moves, cut.extruded, base.extruded);
    if (cut.extruded >= base.extruded * 0.95) {
        std::fprintf(stderr,
                     "a 24 mm hole through a 40 mm box used %.1f mm of filament, the solid box "
                     "%.1f mm: the negative volume did not reach the slicer's booleans\n",
                     cut.extruded, base.extruded);
        return 1;
    }

    if (!document.removeVolume(box, hole)) {
        std::fprintf(stderr, "the negative volume could not be removed\n");
        return 1;
    }
    SliceResult restored;
    if (!slicePlate(document, configPath, restored)) {
        std::fprintf(stderr, "the plate did not slice after the volume was removed\n");
        return 1;
    }
    if (restored.moves != base.moves || std::abs(restored.extruded - base.extruded) > 0.01) {
        std::fprintf(stderr,
                     "removing the negative volume left %d moves and %.1f mm against the "
                     "original %d moves and %.1f mm: the volume did not go away\n",
                     restored.moves, restored.extruded, base.moves, base.extruded);
        return 1;
    }
    std::printf("removed: %d move(s), %.1f mm -- back to the original\n", restored.moves,
                restored.extruded);

    const int cylinder = document.addShape(QStringLiteral("cylinder"), 20.0, 20.0, 30.0);
    if (cylinder < 0) {
        std::fprintf(stderr, "%s\n", qPrintable(document.error()));
        return 1;
    }
    SliceResult two;
    if (!slicePlate(document, configPath, two)) {
        std::fprintf(stderr, "the plate with a box and a cylinder did not slice\n");
        return 1;
    }
    std::printf("added cylinder 20x30 beside it: %d object(s), %d layer(s), %.1f mm filament\n",
                document.count(), two.layers, two.extruded);
    if (document.count() != 2 || two.extruded <= restored.extruded) {
        std::fprintf(stderr,
                     "a second shape on the plate gave %d object(s) and %.1f mm against %.1f mm: "
                     "the added shape is not on the plate\n",
                     document.count(), two.extruded, restored.extruded);
        return 1;
    }

    const int enforcer = document.addVolume(cylinder, QStringLiteral("box"),
                                            QStringLiteral("enforcer"), 8.0, 8.0, 4.0);
    if (enforcer < 0 || !document.setVolumeVisible(cylinder, enforcer, false)
        || !document.setVolumeVisible(cylinder, enforcer, true)) {
        std::fprintf(stderr, "a support enforcer volume could not be attached\n");
        return 1;
    }
    SliceResult support;
    if (!slicePlate(document, configPath, support)) {
        std::fprintf(stderr, "the plate with a support enforcer volume did not slice\n");
        return 1;
    }
    std::printf("attached a support enforcer: %d layer(s), %d move(s)\n", support.layers,
                support.moves);

    std::printf("shape-selftest passed\n");
    return 0;
}

int runProjectSelftest(const QString& model, const QString& configPath, const QString& dataDir)
{
    if (model.isEmpty()) {
        std::fprintf(stderr, "project-test needs a model\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::ifstream configFile(configPath.toStdString());
    if (!configFile.is_open()) {
        std::fprintf(stderr, "cannot read config: %s\n", qPrintable(configPath));
        return 2;
    }
    nlohmann::ordered_json configJson;
    configFile >> configJson;

    double minX = 0.0;
    double maxX = 0.0;
    double minY = 0.0;
    double maxY = 0.0;
    bool firstPoint = true;
    for (const auto& point :
         configJson["configuration"]["printer_settings"].value("bed_shape", nlohmann::ordered_json::array())) {
        if (!point.is_array() || point.size() < 2)
            continue;
        const double x = point[0].get<double>();
        const double y = point[1].get<double>();
        minX = firstPoint ? x : std::min(minX, x);
        maxX = firstPoint ? x : std::max(maxX, x);
        minY = firstPoint ? y : std::min(minY, y);
        maxY = firstPoint ? y : std::max(maxY, y);
        firstPoint = false;
    }
    if (firstPoint) {
        std::fprintf(stderr, "the config has no bed_shape\n");
        return 1;
    }

    PlateDocument document(QVector2D((minX + maxX) * 0.5, (minY + maxY) * 0.5),
                           QVector2D(maxX - minX, maxY - minY));
    const int first = document.addObject(model, QStringLiteral("first"));
    const int second = document.addObject(model, QStringLiteral("second"));
    if (first < 0 || second < 0) {
        std::fprintf(stderr, "%s\n", qPrintable(document.error()));
        return 1;
    }
    document.setPrintSetup(QStringLiteral("Voron 2.4 350 0.4"), QStringLiteral("Standard"),
                           QStringLiteral("Generic PETG"));
    document.setObjectSetting(first, QStringLiteral("layer_height"), 0.3);
    document.setLayerHeightRange(second, 5.0, 12.0, 0.1);
    document.setPosition(second, QVector3D(document.object(first)->position().x() + 40.0,
                                           document.object(first)->position().y(), 0.0));
    document.setRotation(second, QVector3D(0.0, 0.0, 30.0));

    const auto mesh = document.object(first)->mesh;
    const Slic3r::Domain::Transform3d trafo = document.object(first)->trafo.get_matrix();
    int painted = 0;
    for (size_t facet = 0; facet < mesh->its.indices.size(); ++facet) {
        const auto& face = mesh->its.indices[facet];
        const Vec3d a = mesh->its.vertices[face[0]].cast<double>();
        const Vec3d b = mesh->its.vertices[face[1]].cast<double>();
        const Vec3d c = mesh->its.vertices[face[2]].cast<double>();
        const Vec3d normal = (trafo.linear() * (b - a).cross(c - a)).normalized();
        const Vec3d centre = trafo * ((a + b + c) / 3.0);
        if (normal.z() < -0.9 && centre.z() > 1.0) {
            painted += document.paintObject(first, QStringLiteral("supports"),
                                            QVector3D(centre.x(), centre.y(), centre.z()), 1.5,
                                            false, 2, int(facet));
        }
    }
    // Tool paint on the object's own top face: a point in the air would paint
    // nothing, and this test has to carry real paint through the file.
    const QVector3D top = document.objectBoundsMax(first);
    const QVector3D middle = document.objectBoundsMin(first);
    for (size_t facet = 0; facet < mesh->its.indices.size(); ++facet) {
        const auto& face = mesh->its.indices[facet];
        const Vec3d a = mesh->its.vertices[face[0]].cast<double>();
        const Vec3d b = mesh->its.vertices[face[1]].cast<double>();
        const Vec3d c = mesh->its.vertices[face[2]].cast<double>();
        if ((trafo.linear() * (b - a).cross(c - a)).normalized().z() < 0.9)
            continue;
        const Vec3d hit = trafo * ((a + b + c) / 3.0);
        document.paintObject(first, QStringLiteral("tool"),
                             QVector3D(hit.x(), hit.y(), hit.z()), 6.0, false, 2, int(facet));
        break;
    }
    document.setToolColours(QVariantList{QStringLiteral("#e8583c"), QStringLiteral("#3c8ce8")});

    const QByteArray before = document.fingerprint();
    const int beforeCount = document.count();
    const int beforeSettings = document.objectSettings(first).size();
    const int beforeProfile = document.layerHeightProfile(second).size();
    const int beforePaint = document.paintedFacetCount(first, QStringLiteral("supports"));
    const int beforeToolPaint = document.paintedFacetCount(first, QStringLiteral("tool"));

    const auto directoryScratch = next_layer::makeScratchDir();
    QTemporaryDir& directory = *directoryScratch;
    const QString path = directory.filePath(QStringLiteral("plate.nextproj"));
    QString error = document.saveProject(path);
    if (!error.isEmpty()) {
        std::fprintf(stderr, "save failed: %s\n", qPrintable(error));
        return 1;
    }
    const qint64 size = QFileInfo(path).size();
    std::printf("saved %d object(s) to %lld bytes\n", beforeCount, static_cast<long long>(size));

    document.clear();
    document.addObject(model);
    if (document.count() != 1) {
        std::fprintf(stderr, "the document was not cleared before loading\n");
        return 1;
    }

    error = document.loadProject(path);
    if (!error.isEmpty()) {
        std::fprintf(stderr, "load failed: %s\n", qPrintable(error));
        return 1;
    }

    const int afterSettings = document.objectSettings(first).size();
    const int afterProfile = document.layerHeightProfile(second).size();
    const int afterPaint = document.paintedFacetCount(first, QStringLiteral("supports"));
    const int afterToolPaint = document.paintedFacetCount(first, QStringLiteral("tool"));
    std::printf("reloaded: %d object(s), %d override(s), %d profile band(s), %d support paint, "
                "%d tool paint\n",
                document.count(), afterSettings, afterProfile, afterPaint, afterToolPaint);

    if (document.count() != beforeCount) {
        std::fprintf(stderr, "the project has %d object(s), it had %d\n", document.count(),
                     beforeCount);
        return 1;
    }
    if (afterSettings != beforeSettings || afterProfile != beforeProfile
        || afterPaint != beforePaint || afterToolPaint != beforeToolPaint) {
        std::fprintf(stderr,
                     "the project lost data: overrides %d/%d, profile %d/%d, support paint %d/%d, "
                     "tool paint %d/%d\n",
                     afterSettings, beforeSettings, afterProfile, beforeProfile, afterPaint,
                     beforePaint, afterToolPaint, beforeToolPaint);
        return 1;
    }
    if (document.printSetupProcess() != QStringLiteral("Standard")) {
        std::fprintf(stderr, "the project did not keep the print setup\n");
        return 1;
    }

    const QByteArray after = document.fingerprint();
    if (before != after) {
        std::fprintf(stderr, "the reloaded plate has a different fingerprint\n");
        return 1;
    }
    std::printf("project round trip preserved the plate, %d painted facet group(s)\n", painted);

    // A project that loads but does not slice is not a project. The reloaded
    // plate goes through the same slice path the app uses.
    next_layer::SliceRuntime& slicer = next_layer::SliceRuntime::instance();
    const QString gcode = directory.filePath(QStringLiteral("plate.gcode"));
    if (!slicer.slicePlate(document.sliceObjects(), configPath, gcode, error)) {
        std::fprintf(stderr, "the reloaded plate did not slice: %s\n", qPrintable(error));
        return 1;
    }
    QFile file(gcode);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "the reloaded plate wrote no G-code\n");
        return 1;
    }
    int layers = 0;
    int moves = 0;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        if (line.startsWith(";LAYER_CHANGE"))
            ++layers;
        else if (line.startsWith("G1 ") && line.contains(" E"))
            ++moves;
    }
    std::printf("the reloaded plate slices: %d layer(s), %d extruding move(s)\n", layers, moves);
    if (layers < 2 || moves < 100) {
        std::fprintf(stderr, "the reloaded plate sliced to almost nothing (%d layers, %d moves)\n",
                     layers, moves);
        return 1;
    }

    std::printf("project-selftest passed\n");
    return 0;
}



}