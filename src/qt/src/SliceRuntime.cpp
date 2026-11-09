// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "SliceRuntime.hpp"

#include "QtPlatform.hpp"

#include <Slic3r/App/AppConfig.hpp>
#include <Slic3r/App/AppServices.hpp>
#include <Slic3r/App/CLI/CLIRuntime.hpp>
#include <Slic3r/App/CLI/LoadPrintData.hpp>
#include <Slic3r/App/CLI/ProfilesSharingUtils.hpp>
#include <Slic3r/App/Init.hpp>
#include <Slic3r/App/PopNotification/PopNotificationCenter.hpp>
#include <Slic3r/App/PresetUpdater/PresetUpdaterController.hpp>
#include <Slic3r/App/Theme.hpp>
#include <Slic3r/App/Undo/Store.hpp>
#include <Slic3r/Biz/Config/ConfigSerialize.hpp>
#include <Slic3r/Biz/Config/SelectedPresetJson.hpp>
#include <Slic3r/Biz/Format/ProjectFileConstants.hpp>
#include <Slic3r/Biz/Platform/JobManager/JobManager.hpp>
#include <Slic3r/Biz/Platform/PlatformServices.hpp>
#include <Slic3r/Biz/Preset/IO/BundlePaths.hpp>
#include <Slic3r/Biz/ProjectInteractor.hpp>
#include <Slic3r/Biz/Slicing/SlicingInteractor.hpp>
#include <Slic3r/Biz/StatusCache.hpp>
#include <Slic3r/Directories.hpp>
#include <Slic3r/Domain/BedInstance.hpp>
#include <Slic3r/Domain/ConfigContainer.hpp>
#include <Slic3r/Domain/ConfigPack.hpp>
#include <Slic3r/Domain/CustomGCode.hpp>

#include <libslic3r/CustomGCode.hpp>
#include <Slic3r/Domain/FullConfigFDM.hpp>
#include <Slic3r/Domain/FullConfigSLA.hpp>
#include <Slic3r/Domain/Project.hpp>
#include <Slic3r/Domain/SelectionId.hpp>

#include <Slic3r/Biz/Algorithms/ModelVolume.hpp>
#include <Slic3r/Biz/Config/3mf_legacy.hpp>
#include <Slic3r/Biz/Config/ConfigLoad.hpp>
#include <Slic3r/Biz/Format/STL.hpp>
#include <Slic3r/Domain/FacetsAnnotation.hpp>
#include <Slic3r/Domain/LayerHeightProfile.hpp>
#include <Slic3r/Domain/Model.hpp>
#include <Slic3r/Domain/ModelInstance.hpp>
#include <Slic3r/Domain/ModelObject.hpp>
#include <Slic3r/Domain/ModelVolume.hpp>
#include <Slic3r/Domain/Preset/SelectedPreset.hpp>

#include <libslic3r/SlicingStatus.hpp>

#include <nlohmann/json.hpp>

#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <miniz.h>

#include "Scratch.hpp"

#include <boost/filesystem/path.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

using Slic3r::App::CLI::CLIRuntime;
using Slic3r::Biz::ProjectInteractor;
using Slic3r::Biz::Slicing::Status;
using Slic3r::Biz::Slicing::StatusCode;
using Slic3r::Domain::ConfigContainer;
using Slic3r::Domain::ConfigPack;
using Slic3r::Domain::ConfigPackFDM;
using Slic3r::Domain::ConfigPackSLA;
using Slic3r::Domain::Project;
using Slic3r::Domain::SelectionId;
using Slic3r::Domain::SlicingId;

namespace next_layer {

namespace {

/// The services the shared app layer expects to exist, installed once over this
/// process's engine runtime. They are all the app layer needs from a host; the
/// plater's own render module is deliberately not among them — next-layer draws
/// its own viewport and UI, and nothing of the engine's GUI is instantiated.
struct AppServicesSetup
{
    AppServicesSetup(const Slic3r::Domain::Workbench&, ProjectInteractor& interactor)
    {
        Slic3r::App::AppServices& services = Slic3r::App::AppServices::instance();
        services.set_app_config(Slic3r::App::AppConfig::create_app_config());
        services.set_theme(std::make_unique<Slic3r::App::Theme>(
            services.app_config().get<Slic3r::App::Theme::Style>("theme")));
        services.set_pop_notification_center(
            std::make_unique<Slic3r::App::PopNotification::PopNotificationCenter>(interactor));

        Slic3r::Biz::Platform::PlatformServices& platform =
            Slic3r::Biz::Platform::PlatformServices::instance();
        Slic3r::App::PresetUpdater::ControllerEnvironment updater_environment;
        updater_environment.timer_queue      = &platform.timer_queue();
        updater_environment.online_allowed   = []() {
            return Slic3r::App::AppServices::instance().app_config().get<bool>(
                "enable_preset_update");
        };
        updater_environment.request_render = []() {
            Slic3r::Biz::Platform::PlatformServices::instance()
                .render_request_handler()
                .request_render();
        };
        services.set_preset_updater_controller(
            std::make_unique<Slic3r::App::PresetUpdater::PresetUpdaterController>(
                interactor.preset_updater_interactor(), std::move(updater_environment)));

        interactor.set_undo_provider(std::make_unique<Slic3r::App::Undo::Store>(interactor));
    }

    ~AppServicesSetup()
    {
        Slic3r::App::AppServices& services = Slic3r::App::AppServices::instance();
        services.set_preset_updater_controller(nullptr);
        services.set_pop_notification_center(nullptr);
    }
};

std::string dataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        .toStdString()
        + "/next-layer";
}

bool terminalStatus(const Slic3r::Biz::StatusCache& statusCache, const SlicingId slicingId)
{
    const std::optional<Status> status = statusCache.get_status(slicingId);
    if (!status.has_value())
        return false;
    return status->code == StatusCode::Empty || status->code == StatusCode::Removed
        || status->code == StatusCode::Finished || status->code == StatusCode::InvalidData;
}

// CLIRuntime::wait_until blocks without a limit. A slice that wedges must fail in
// the window rather than hang it, so the same loop gets a deadline.
bool waitFor(CLIRuntime& cli, const int deciseconds,
             const std::function<bool()>& predicate)
{
    for (int tick = 0; tick < deciseconds * 10; ++tick) {
        cli.dispatcher().dispatch_enqueued();
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

} // namespace

struct SliceRuntime::Runtime
{
    explicit Runtime(const Slic3r::App::InitParams& params)
        : cli(params)
    {
        Slic3r::Biz::Platform::PlatformServices::instance().set_app_config_provider(
            std::make_unique<AppConfigProvider>(Slic3r::data_dir()));
        Slic3r::App::AppServices::instance().set_file_explorer_handler(
            std::make_unique<FileExplorerHandler>());
        host = std::make_unique<AppServicesSetup>(cli.project_interactor().workbench(),
                                                  cli.project_interactor());
    }

    ~Runtime()
    {
        // Every interactor asserts the dispatcher closed before it dies, and the
        // host's interactors are destroyed with the host. `CLIRuntime` closes the
        // dispatcher in its own destructor, which runs after this body, so close it
        // here while the host can still be dropped. `close()` is idempotent.
        cli.dispatcher().close();
        host.reset();
    }

    static Slic3r::App::InitParams params()
    {
        Slic3r::App::InitParams params;
        params.misc.datadir = dataDir();
        return params;
    }

    CLIRuntime cli;
    std::unique_ptr<AppServicesSetup> host;
};

std::unique_ptr<SliceRuntime::Runtime>& runtimeInstance()
{
    static std::unique_ptr<SliceRuntime::Runtime> runtime;
    return runtime;
}

namespace {

/// Anything the engine queued for the main thread refers to the project that
/// queued it. Removing a project without draining first leaves a callback
/// pointing at a destroyed listener, and it crashes whenever something finally
/// runs the queue -- which, for a headless run, is process exit.
void drainMainThread()
{
    Slic3r::Biz::Platform::PlatformServices& services =
        Slic3r::Biz::Platform::PlatformServices::instance();
    if (services.main_thread_dispatcher().is_closed())
        return;
    services.main_thread_dispatcher().dispatch_enqueued();
}

} // namespace

SliceRuntime::Runtime& runtimeLocked()
{
    std::unique_ptr<SliceRuntime::Runtime>& runtime = runtimeInstance();
    if (!runtime) {
        runtime = std::make_unique<SliceRuntime::Runtime>(SliceRuntime::Runtime::params());
        // The platform's static services are destroyed after every static that
        // was constructed after them, and the runtime is always constructed
        // after them, so this handler is the one that closes the dispatcher
        // first. Without it the engine asserts at exit on every code path that
        // returns from main without calling shutdown().
        std::atexit([] { SliceRuntime::shutdown(); });
    }
    return *runtime;
}

SliceRuntime& SliceRuntime::instance()
{
    static SliceRuntime runtime;
    return runtime;
}

std::string SliceRuntime::dataDirForParams()
{
    return dataDir();
}

std::mutex& SliceRuntime::engineMutex()
{
    static std::mutex mutex;
    return mutex;
}

void SliceRuntime::ensurePlatformServices()
{
    const std::scoped_lock lock(engineMutex());
    runtimeLocked();
}

void SliceRuntime::shutdown()
{
    const std::scoped_lock lock(engineMutex());

    // Anything the engine queued for the main thread has to run while the scene
    // it refers to still exists. Draining here is what stops the callbacks from
    // running during static destruction, against a half-torn-down project -- a
    // segfault in SceneInteractor::on_extruder_candidates_changed on the way out.
    if (runtimeInstance()) {
        auto& dispatcher = Slic3r::Biz::Platform::PlatformServices::instance()
                               .main_thread_dispatcher();
        dispatcher.close();
        dispatcher.dispatch_enqueued();
    }
    runtimeInstance().reset();
}

bool SliceRuntime::slice(const QString& modelPath, const QString& configPath,
                         const QString& gcodePath, QString& error)
{
    const std::scoped_lock lock(engineMutex());

    Runtime& runtime = runtimeLocked();

    Slic3r::App::InitParams params;
    params.misc.datadir = dataDir();
    params.misc.output = gcodePath.toStdString();
    params.input.input_files = {modelPath.toStdString()};
    params.input.config_file = configPath.toStdString();
    params.transform.dont_arrange = true;

    std::vector<SelectionId> projectIds;
    if (!Slic3r::App::CLI::load_print_data(runtime.cli, projectIds, params)) {
        error = QStringLiteral("the engine could not load %1").arg(modelPath);
        return false;
    }
    if (projectIds.size() != 1) {
        error = QStringLiteral("%1 loaded %2 projects, expected one")
                    .arg(modelPath)
                    .arg(projectIds.size());
        return false;
    }

    ProjectInteractor& project = runtime.cli.project_interactor();
    project.select_project(projectIds.front());

    Project& selected = project.selected_project();
    Slic3r::Biz::Slicing::SlicingInteractor& slicing = project.slicing_interactor();
    Slic3r::Biz::StatusCache& statusCache = project.status_cache();
    const Slic3r::Domain::ConfigContainer& configContainer = project.selected_config_container();

    const SlicingId slicingId = project.selected_bed_slicing_id();
    const Slic3r::Domain::BedInstance* bed =
        selected.find_bed_instance_by_id(slicingId.bed_instance_id);
    if (bed == nullptr) {
        error = QStringLiteral("the selected project has no bed to slice");
        return false;
    }

    slicing.update_process(selected.model(), selected.metadata(),
                           configContainer.selected_preset().metadata(),
                           configContainer.build_print_config(), *bed);
    slicing.slice_bed(slicingId);

    if (!waitFor(runtime.cli, 600, [&statusCache, slicingId]() {
            return terminalStatus(statusCache, slicingId);
        })) {
        error = QStringLiteral("slicing never reached a terminal status");
        return false;
    }

    const Status status = statusCache.get_status(slicingId).value();
    if (status.code != StatusCode::Finished) {
        QString message = QStringLiteral("slicing did not finish (status %1)")
                              .arg(static_cast<int>(status.code));
        for (const auto& sliceError : status.errors) {
            message += QStringLiteral("; error %1").arg(static_cast<int>(sliceError.code));
            for (const std::string& key : sliceError.item_keys)
                message += QLatin1Char(' ') + QString::fromStdString(key);
        }
        error = message;
        return false;
    }

    Slic3r::App::CLI::ExportFinishedJobManagerStatusListener exportListener;
    Slic3r::Biz::Platform::PlatformServices& services =
        Slic3r::Biz::Platform::PlatformServices::instance();
    services.job_manager().add_listener<
        Slic3r::Biz::Platform::JobManager::IJobManagerStatusChangedListener>(&exportListener);

    project.do_result_export(slicingId, boost::filesystem::path{gcodePath.toStdString()});
    const bool exported = waitFor(runtime.cli, 600, [&exportListener]() {
        return exportListener.export_finished;
    });

    services.job_manager().remove_listener<
        Slic3r::Biz::Platform::JobManager::IJobManagerStatusChangedListener>(&exportListener);

    if (!exported) {
        error = QStringLiteral("the export never finished");
        return false;
    }
    if (exportListener.export_error.has_value()) {
        error = QStringLiteral("export failed: %1")
                    .arg(QString::fromStdString(exportListener.export_error.value()));
        return false;
    }

    drainMainThread();
    project.remove_project(projectIds.front());
    return true;
}


namespace {

/// Loads one file into the engine and returns the id of the project it created,
/// or nothing when the engine refused it.
std::optional<Slic3r::Domain::SelectionId> loadOne(CLIRuntime& cli, const std::string& file,
                                                   const std::string& configPath,
                                                   const std::string& outputPath)
{
    Slic3r::App::InitParams params;
    params.misc.datadir = SliceRuntime::dataDirForParams();
    params.misc.output = outputPath;
    params.input.input_files = {file};
    params.input.config_file = configPath;
    params.transform.dont_arrange = true;

    std::vector<SelectionId> projectIds;
    if (!Slic3r::App::CLI::load_print_data(cli, projectIds, params))
        return std::nullopt;
    if (projectIds.size() != 1)
        return std::nullopt;
    return projectIds.front();
}

}

std::optional<SelectionId> SliceRuntime::preparePlate(Runtime& runtime,
                                                     const QVector<PlateSliceObject>& objects,
                                                     const QString& configPath,
                                                     const QString& gcodePath,
                                                     const QVector<PlateLayerGcode>& layerGcodes,
                                                     QString& error,
                                                     QHash<int, QHash<QString, QString>>* carried)
{
    if (!m_plateDir.has_value() || !m_plateDir->isValid()) {
        m_plateDir.emplace(next_layer::scratchTemplate());
        if (!m_plateDir->isValid()) {
            error = QStringLiteral("could not create a scratch directory for the plate");
            return std::nullopt;
        }
    }

    // The engine resolves a config only while loading a file, so the first object
    // goes in as a file. Its project is then rewritten in place: the engine has no
    // empty-project constructor that also resolves the preset triple, and loading
    // the plate a second time leaves the scene's listeners pointing at the project
    // that was just removed.
    const std::string seed = m_plateDir->filePath(QStringLiteral("seed.stl")).toStdString();
    if (!Slic3r::Biz::store_stl(seed, objects.first().mesh, true, "next-layer")) {
        error = QStringLiteral("could not stage the plate's first object");
        return std::nullopt;
    }

    ProjectInteractor& project = runtime.cli.project_interactor();
    const std::optional<SelectionId> projectId =
        loadOne(runtime.cli, seed, configPath.toStdString(), gcodePath.toStdString());
    if (!projectId.has_value()) {
        error = QStringLiteral("the engine could not load the plate");
        return std::nullopt;
    }
    project.select_project(*projectId);

    Project& selected = project.selected_project();
    Slic3r::Domain::Model& model = selected.model();
    model.clear_objects();

    for (int objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
        const PlateSliceObject& source = objects.at(objectIndex);
        Slic3r::Domain::ModelObject* object = model.add_object();
        object->name = source.name.toStdString();
        Slic3r::Domain::TriangleMesh mesh = source.mesh;
        Slic3r::Domain::ModelVolume* volume =
            Slic3r::Biz::Algorithms::ModelVolume::construct_ptr(object, std::move(mesh));
        object->add_volume(*volume);
        object->add_instance(source.transform);

        // Modifier volumes join the same object: the FDM slice path slices each
        // volume by type and booleans them, so negatives subtract, modifiers
        // become regions and support blockers/enforcers are read as such.
        for (const PlateSliceVolume& extra : source.volumes) {
            Slic3r::Domain::TriangleMesh extraMesh = extra.mesh;
            Slic3r::Domain::ModelVolume* added =
                Slic3r::Biz::Algorithms::ModelVolume::construct_ptr(object, std::move(extraMesh),
                                                                    extra.type);
            if (added != nullptr) {
                if (!extra.settingsJson.isEmpty()) {
                    const nlohmann::ordered_json settings =
                        nlohmann::ordered_json::parse(extra.settingsJson.toStdString());
                    const Slic3r::Biz::Config::BoxIssues issues =
                        Slic3r::Biz::Config::load_box(settings, added->volume_settings);
                    for (auto it = settings.begin(); it != settings.end(); ++it) {
                        const auto issue = issues.find(it.key());
                        if (issue == issues.end())
                            continue;
                        error = QStringLiteral("%1 was rejected on a volume: %2")
                                    .arg(QString::fromStdString(it.key()),
                                         QString::fromStdString(issue->second.message));
                        drainMainThread();
                        project.remove_project(*projectId);
                        return std::nullopt;
                    }
                }
                object->add_volume(*added);
            }
        }

        if (!source.settingsJson.isEmpty()) {
            const nlohmann::ordered_json settings =
                nlohmann::ordered_json::parse(source.settingsJson.toStdString());
            if (qEnvironmentVariableIsSet("NEXT_LAYER_DEBUG_OBJECT_SETTINGS"))
                std::fprintf(stderr, "object settings for %s: %s\n",
                             source.name.toUtf8().constData(), settings.dump().c_str());
            const Slic3r::Biz::Config::BoxIssues issues =
                Slic3r::Biz::Config::load_box(settings, object->object_settings);
            // load_box reports every key it did not find, which is right for a whole
            // box and noise for a partial override list. Only a key we sent can be a
            // real rejection.
            for (auto it = settings.begin(); it != settings.end(); ++it) {
                const auto issue = issues.find(it.key());
                if (issue == issues.end())
                    continue;
                if (carried != nullptr) {
                    // The engine will not take this override here. Rather than fail
                    // the export over it, it goes into the file under the name it
                    // arrived with, where a reader that does understand it finds it.
                    const QString key = QString::fromStdString(it.key());
                    const nlohmann::ordered_json& value = *it;
                    (*carried)[objectIndex].insert(key, value.is_string()
                        ? QString::fromStdString(value.get<std::string>())
                        : QString::fromStdString(value.dump()));
                    continue;
                }
                error = QStringLiteral("%1 was rejected: %2")
                            .arg(QString::fromStdString(it.key()),
                                 QString::fromStdString(issue->second.message));
                drainMainThread();
    project.remove_project(*projectId);
                return std::nullopt;
            }
        }

        if (!source.layerHeights.empty())
            object->layer_height_profile.set(source.layerHeights);

        Slic3r::Domain::ModelVolume* added = object->volumes.front();
        if (!source.supportPaint.triangles_to_split.empty()) {
            Slic3r::Domain::TriangleSelector::TriangleSplittingData data = source.supportPaint;
            added->supported_facets.set_data(std::move(data));
        }
        if (!source.seamPaint.triangles_to_split.empty()) {
            Slic3r::Domain::TriangleSelector::TriangleSplittingData data = source.seamPaint;
            added->seam_facets.set_data(std::move(data));
        }
        if (!source.toolPaint.triangles_to_split.empty()) {
            Slic3r::Domain::TriangleSelector::TriangleSplittingData data = source.toolPaint;
            added->mm_segmentation_facets.set_data(std::move(data));
        }
    }

    // The bed still lists the objects that were just cleared. Every instance now on
    // the bed is attached to it, or the slice would see an empty plate.
    Slic3r::Domain::ConfigContainer& configContainer = project.selected_config_container();
    Slic3r::Domain::ConfigContainer::BedInstanceList& bedInstances = configContainer.bed_instances();
    if (bedInstances.empty()) {
        error = QStringLiteral("the plate has no bed to slice");
        drainMainThread();
    project.remove_project(*projectId);
        return std::nullopt;
    }
    for (const std::unique_ptr<Slic3r::Domain::BedInstance>& instancePtr : bedInstances) {
        Slic3r::Domain::BedInstance& instance = *instancePtr;
        instance.model_instances.clear();
        instance.colliding_instances.clear();
        instance.extruder_candidates.clear();
        for (Slic3r::Domain::ModelObject* object : model.objects) {
            for (Slic3r::Domain::ModelInstance* modelInstance : object->instances)
                instance.model_instances.push_back(modelInstance);
        }
    }


    // Per-layer custom G-code, pauses and tool changes are the engine's own
    // `CustomGCode::Info`, held on the bed instance the slice runs on. The
    // generator emits them at their print Z, so the only thing to get right is
    // the ordering and the mode.
    if (!layerGcodes.isEmpty()) {
        Slic3r::Domain::CustomGCode::Info info;
        info.mode = Slic3r::Domain::CustomGCode::Mode::Undef;
        for (const PlateLayerGcode& source : layerGcodes) {
            Slic3r::Domain::CustomGCode::Item item;
            item.print_z = source.z;
            item.extruder = 1;
            item.extra = source.text.toStdString();
            if (source.type == QStringLiteral("pause"))
                item.type = Slic3r::Domain::CustomGCode::Type::PausePrint;
            else if (source.type == QStringLiteral("color"))
                item.type = Slic3r::Domain::CustomGCode::Type::ColorChange;
            else if (source.type == QStringLiteral("tool"))
                item.type = Slic3r::Domain::CustomGCode::Type::ToolChange;
            else
                item.type = Slic3r::Domain::CustomGCode::Type::Custom;
            // The colour field is a colour and nothing else. It used to carry the
            // marker's text, which writes "check the first layer" into a field every
            // other slicer reads as #RRGGBB. Text belongs in `extra`, which is where
            // the readers look first.
            if (item.type == Slic3r::Domain::CustomGCode::Type::ColorChange
                && QRegularExpression(QStringLiteral("^#[0-9A-Fa-f]{6}$"))
                       .match(source.text)
                       .hasMatch())
                item.color = source.text.toStdString();
            info.gcodes.push_back(std::move(item));
        }
        std::sort(info.gcodes.begin(), info.gcodes.end());
        // The mode decides how the generator reads the items; the engine's own
        // helper derives it from the item types rather than from the printer.
        Slic3r::CustomGCodeUtils::check_mode_for_custom_gcode_per_print_z(info);
        for (const std::unique_ptr<Slic3r::Domain::BedInstance>& instancePtr : bedInstances)
            instancePtr->custom_gcode = info;
    }

    return projectId;
}

bool SliceRuntime::slicePlate(const QVector<PlateSliceObject>& objects, const QString& configPath,
                              const QString& gcodePath, QString& error,
                              const QVector<PlateLayerGcode>& layerGcodes)
{
    if (objects.isEmpty()) {
        error = QStringLiteral("there is nothing on the plate");
        return false;
    }

    const std::scoped_lock lock(engineMutex());
    Runtime& runtime = runtimeLocked();

    const std::optional<SelectionId> projectId =
        preparePlate(runtime, objects, configPath, gcodePath, layerGcodes, error);
    if (!projectId.has_value())
        return false;

    ProjectInteractor& project = runtime.cli.project_interactor();
    Project& selected = project.selected_project();
    Slic3r::Domain::ConfigContainer& configContainer = project.selected_config_container();
    Slic3r::Biz::Slicing::SlicingInteractor& slicing = project.slicing_interactor();
    Slic3r::Biz::StatusCache& statusCache = project.status_cache();

    const SlicingId slicingId = project.selected_bed_slicing_id();
    const Slic3r::Domain::BedInstance* bed =
        selected.find_bed_instance_by_id(slicingId.bed_instance_id);
    if (bed == nullptr) {
        error = QStringLiteral("the plate has no bed to slice");
        drainMainThread();
        project.remove_project(*projectId);
        return false;
    }

    slicing.update_process(selected.model(), selected.metadata(),
                           configContainer.selected_preset().metadata(),
                           configContainer.build_print_config(), *bed);
    slicing.slice_bed(slicingId);

    if (!waitFor(runtime.cli, 600, [&statusCache, slicingId]() {
            return terminalStatus(statusCache, slicingId);
        })) {
        error = QStringLiteral("slicing never reached a terminal status");
        drainMainThread();
        project.remove_project(*projectId);
        return false;
    }

    const Status status = statusCache.get_status(slicingId).value();
    if (status.code != StatusCode::Finished) {
        QString message = QStringLiteral("slicing did not finish (status %1)")
                              .arg(static_cast<int>(status.code));
        for (const auto& sliceError : status.errors) {
            message += QStringLiteral("; error %1").arg(static_cast<int>(sliceError.code));
            for (const std::string& key : sliceError.item_keys)
                message += QLatin1Char(' ') + QString::fromStdString(key);
        }
        error = message;
        drainMainThread();
        project.remove_project(*projectId);
        return false;
    }

    Slic3r::App::CLI::ExportFinishedJobManagerStatusListener exportListener;
    Slic3r::Biz::Platform::PlatformServices& services =
        Slic3r::Biz::Platform::PlatformServices::instance();
    services.job_manager().add_listener<
        Slic3r::Biz::Platform::JobManager::IJobManagerStatusChangedListener>(&exportListener);

    project.do_result_export(slicingId, boost::filesystem::path{gcodePath.toStdString()});
    const bool exported = waitFor(runtime.cli, 600, [&exportListener]() {
        return exportListener.export_finished;
    });

    services.job_manager().remove_listener<
        Slic3r::Biz::Platform::JobManager::IJobManagerStatusChangedListener>(&exportListener);

    if (!exported) {
        error = QStringLiteral("the export never finished");
        drainMainThread();
        project.remove_project(*projectId);
        return false;
    }
    if (exportListener.export_error.has_value()) {
        error = QStringLiteral("export failed: %1")
                    .arg(QString::fromStdString(exportListener.export_error.value()));
        drainMainThread();
        project.remove_project(*projectId);
        return false;
    }

    drainMainThread();
    project.remove_project(*projectId);
    return true;
}

namespace {

/// Adds the plate records the classic 3MF writer does not know about: every
/// object on plate one, named. A reader that understands them puts the objects
/// back where they were instead of packing the bed, and OrcaSlicer reads the same
/// file.
bool writePlateRecords(const QString& path, const QVector<PlateSliceObject>& objects,
                       const QHash<int, QHash<QString, QString>>& carried)
{
    QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<config>\n");
    int plateCount = 1;
    for (const PlateSliceObject& object : objects)
        plateCount = std::max(plateCount, object.plate + 1);

    // The object ids are the ones the writer gave the model, one per object in the
    // order they were handed over. The settings an object carries that this engine
    // cannot act on ride along here, in the dialect they were written in: this file
    // is metadata, so a name nothing here knows costs nothing and a reader that
    // does know it gets it back.
    for (int index = 0; index < objects.size(); ++index) {
        xml += QStringLiteral("  <object id=\"%1\">\n"
                              "    <metadata key=\"plater_id\" value=\"%2\"/>\n")
                   .arg(index + 1)
                   .arg(objects.at(index).plate + 1);
        const QHash<QString, QString>& kept = carried[index];
        for (int group = 0; group < 2; ++group) {
            const QHash<QString, QString>& entries =
                group == 0 ? objects.at(index).foreignSettings : kept;
            for (auto entry = entries.constBegin(); entry != entries.constEnd(); ++entry) {
                if (entry.key().isEmpty())
                    continue;
                xml += QStringLiteral("    <metadata key=\"%1\" value=\"%2\"/>\n")
                           .arg(entry.key().toHtmlEscaped(), entry.value().toHtmlEscaped());
            }
        }
        xml += QStringLiteral("  </object>\n");
    }
    for (int plate = 1; plate <= plateCount; ++plate) {
        xml += QStringLiteral("  <plate>\n    <metadata key=\"plater_id\" value=\"%1\"/>\n")
                   .arg(plate);
        for (int index = 0; index < objects.size(); ++index) {
            if (objects.at(index).plate + 1 != plate)
                continue;
            xml += QStringLiteral("    <model_instance>\n"
                                  "      <metadata key=\"object_id\" value=\"%1\"/>\n"
                                  "      <metadata key=\"instance_id\" value=\"0\"/>\n"
                                  "    </model_instance>\n")
                       .arg(index + 1);
        }
        xml += QStringLiteral("  </plate>\n");
    }
    xml += QStringLiteral("</config>\n");

    const QByteArray body = xml.toUtf8();
    const mz_bool added = mz_zip_add_mem_to_archive_file_in_place(
        path.toUtf8().constData(), "Metadata/model_settings.config", body.constData(),
        size_t(body.size()), nullptr, 0, MZ_BEST_COMPRESSION);
    return added == MZ_TRUE;
}

} // namespace

bool SliceRuntime::exportPlate3mf(const QVector<PlateSliceObject>& objects,
                                  const QString& configPath, const QString& outPath,
                                  QString& error,
                                  const QVector<PlateLayerGcode>& layerGcodes)
{
    if (objects.isEmpty()) {
        error = QStringLiteral("there is nothing on the plate");
        return false;
    }

    const std::scoped_lock lock(engineMutex());
    Runtime& runtime = runtimeLocked();

    QHash<int, QHash<QString, QString>> carried;
    const std::optional<SelectionId> projectId =
        preparePlate(runtime, objects, configPath, outPath, layerGcodes, error, &carried);
    if (!projectId.has_value())
        return false;

    ProjectInteractor& project = runtime.cli.project_interactor();
    Project& selected = project.selected_project();
    const Slic3r::Domain::ConfigContainer& configContainer =
        project.selected_config_container();

    // The plate's per-bed records -- wipe tower and per-layer markers -- are
    // written by the same call: the exporter reads them from the beds, not from
    // anything passed alongside.
    Slic3r::Domain::WipeTowersOnBeds wipeTowers;
    Slic3r::Domain::CustomGCodesOnBeds customGcodes;
    int bedIndex = 0;
    for (const std::unique_ptr<Slic3r::Domain::BedInstance>& instancePtr :
         configContainer.bed_instances()) {
        const int index = bedIndex++;
        if (instancePtr->custom_gcode.has_value()
            && !instancePtr->custom_gcode->gcodes.empty())
            customGcodes[index] = *instancePtr->custom_gcode;
        wipeTowers[index] = instancePtr->wipe_tower;
    }

    // PrusaSlicer's own project format, which OrcaSlicer reads: the classic
    // `Slic3r_PE.config` and `Slic3r_PE_model.config` pair. The engine's newer
    // `PrusaSlicer3_project.json` writer is deliberately not used here -- it
    // round-trips in this engine alone and is invisible to OrcaSlicer.
    const bool written = Slic3rLegacy::store_3mf_legacy(
        outPath.toStdString().c_str(), &selected.model(), configContainer.build_print_config(),
        configContainer.selected_preset().hw_config, /*fullpath_sources=*/true, wipeTowers,
        customGcodes);
    const bool appended = written && writePlateRecords(outPath, objects, carried);

    if (!written)
        error = QStringLiteral("the engine could not write %1").arg(outPath);
    else if (!appended)
        error = QStringLiteral("could not add the plate records to %1").arg(outPath);

    drainMainThread();
    project.remove_project(*projectId);
    return written && appended;
}

void SliceRuntime::reloadPresets()
{
    const std::scoped_lock lock(engineMutex());
    Runtime& runtime = runtimeLocked();
    runtime.cli.project_interactor().preset_interactor().load_preset_bundle(
        Slic3r::Biz::Preset::IO::BundlePaths::make_standard_runtime());
}

QByteArray SliceRuntime::printerModels()
{
    const std::scoped_lock lock(engineMutex());
    Runtime& runtime = runtimeLocked();

    const std::string json = Slic3r::App::CLI::get_json_printer_models(
        runtime.cli.project_interactor().workbench().preset_bundle());
    return QByteArray::fromStdString(json);
}

QByteArray SliceRuntime::profilesFor(const QString& printer)
{
    const std::scoped_lock lock(engineMutex());
    Runtime& runtime = runtimeLocked();

    const std::string json = Slic3r::App::CLI::get_json_print_tool_filament_profiles(
        runtime.cli.project_interactor().workbench().preset_bundle(),
        printer.toStdString());
    return QByteArray::fromStdString(json);
}

QByteArray SliceRuntime::resolvedConfig(const QString& printer, const QString& print,
                                        const QStringList& filaments)
{
    const std::scoped_lock lock(engineMutex());
    Runtime& runtime = runtimeLocked();

    Slic3r::App::InitParams params;
    params.misc.datadir = dataDir();
    params.input.printer_profile_preset = printer.toStdString();
    params.input.print_profile_preset = print.toStdString();
    for (const QString& filament : filaments) {
        if (!filament.isEmpty())
            params.input.material_profile_presets.push_back(filament.toStdString());
    }
    if (params.input.material_profile_presets.empty())
        params.input.material_profile_presets.push_back("Generic");

    std::vector<SelectionId> projectIds;
    if (!Slic3r::App::CLI::load_print_data(runtime.cli, projectIds, params))
        return {};

    ProjectInteractor& project = runtime.cli.project_interactor();
    const SelectionId projectId = project.selected_project_id();
    const ConfigContainer& container = project.selected_config_container();

    nlohmann::ordered_json json;
    json[Slic3r::Biz::Format::ProjectFileConstants::PRESET_METADATA] =
        nlohmann::ordered_json(container.selected_preset().metadata());

    const ConfigPack pack{container.build_print_config()};
    if (const auto* fdm = std::get_if<ConfigPackFDM>(&pack)) {
        json[Slic3r::Biz::Format::ProjectFileConstants::CONFIGURATION] =
            nlohmann::ordered_json(Slic3r::Domain::as_boxes(*fdm));
    } else if (const auto* sla = std::get_if<ConfigPackSLA>(&pack)) {
        json[Slic3r::Biz::Format::ProjectFileConstants::CONFIGURATION] =
            nlohmann::ordered_json(Slic3r::Domain::as_boxes(*sla));
    } else {
        drainMainThread();
    project.remove_project(projectId);
        return {};
    }

    const std::string dumped = json.dump();
    drainMainThread();
    project.remove_project(projectId);
    return QByteArray::fromStdString(dumped);
}

} // namespace next_layer
