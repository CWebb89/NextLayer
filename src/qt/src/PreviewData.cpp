// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "PreviewData.hpp"

#include "FeatureColors.hpp"

#include <Slic3r/Biz/libpgcode/Processor.hpp>
#include <Slic3r/Biz/libpgcode/ProcessorConfig.hpp>
#include <Slic3r/Biz/libpgcode/ProcessorResult.hpp>
#include <Slic3r/Biz/libpgcode/Types.hpp>

#include <Slic3r/App/Preview/PreviewSceneRenderCustomizer.hpp>
#include <Slic3r/App/Render/CommandBuffer.hpp>
#include <Slic3r/App/Render/Context.hpp>
#include <Slic3r/App/Render/Device.hpp>
#include <Slic3r/App/Render/Init.hpp>
#include <Slic3r/App/Render/Framebuffer.hpp>
#include <Slic3r/App/Render/FramebufferManager.hpp>
#include <Slic3r/App/Scene/Camera.hpp>
#include <Slic3r/App/Scene/CameraHelper.hpp>
#include <Slic3r/App/Scene/GeometryDataFactory.hpp>
#include <Slic3r/App/Scene/Scene.hpp>
#include <Slic3r/App/Scene/Transform.hpp>
#include <Slic3r/App/libvgcode/FdmViewer.hpp>
#include <Slic3r/Biz/Algorithms/Color.hpp>

#include <GL/glew.h>

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>

namespace pg = Slic3r::Biz::libpgcode;

namespace {

constexpr std::size_t kRoleCount = std::size_t(Slic3r::Domain::GCodeExtrusionRole::Count);

// The engine's own `;TYPE:` names, so the legend, the palette in
// FeatureColors.hpp and `--hide-feature` keep addressing the same labels.
const char* const kRoleNames[kRoleCount] = {
    "None",
    "Perimeter",
    "External perimeter",
    "Overhang perimeter",
    "Internal infill",
    "Solid infill",
    "Top solid infill",
    "Ironing",
    "Bridge infill",
    "Gap fill",
    "Skirt",
    "Support material",
    "Support material interface",
    "Wipe tower",
    "Custom",
    "Bottom surface",
    "Internal bridge infill",
};

pg::ProcessorConfig configFor(const std::string& gcode)
{
    // ProcessorConfig::extract_processor_config_from_prusaslicer_gcode() ends
    // in `PANIC("Must load hw config!")` in this engine snapshot: the hardware
    // config subsystem it wants is not wired up in 3.0.0-alpha12, so the only
    // usable source is the config block the engine writes into the G-code
    // footer. Read the keys that decide geometry (filament diameter), mass and
    // cost (density, cost) and the time estimate (machine limits, flavor).
    pg::ProcessorConfig config;
    config.producer = pg::GCodeProducer::PrusaSlicer;
    config.machine_limits.usage = pg::MachineLimitsUsageType::TimeEstimateOnly;

    const auto trim = [](std::string text) {
        const auto begin = text.find_first_not_of(" \t\r\n=");
        if (begin == std::string::npos)
            return std::string();
        const auto end = text.find_last_not_of(" \t\r\n");
        return text.substr(begin, end - begin + 1);
    };

    const auto numbers = [](const std::string& value) {
        std::vector<float> out;
        std::string current;
        for (char ch : value) {
            if (ch == ',' || ch == ' ' || ch == '\t') {
                if (!current.empty()) {
                    out.push_back(float(std::atof(current.c_str())));
                    current.clear();
                }
            } else {
                current.push_back(ch);
            }
        }
        if (!current.empty())
            out.push_back(float(std::atof(current.c_str())));
        return out;
    };

    std::size_t position = 0;
    while (position < gcode.size()) {
        std::size_t lineEnd = gcode.find('\n', position);
        if (lineEnd == std::string::npos)
            lineEnd = gcode.size();
        std::string_view line(gcode.data() + position, lineEnd - position);
        position = lineEnd + 1;

        if (line.size() < 3 || line[0] != ';' || line[1] != ' ')
            continue;

        const std::size_t equals = line.find(" = ", 2);
        if (equals == std::string_view::npos)
            continue;

        const std::string key = trim(std::string(line.substr(2, equals - 2)));
        if (key.empty() || key.find(' ') != std::string::npos)
            continue;
        const std::string value = trim(std::string(line.substr(equals + 3)));
        if (value.empty())
            continue;

        if (key == "filament_diameter")            config.filaments.diameters = numbers(value);
        else if (key == "filament_density")         config.filaments.densities = numbers(value);
        else if (key == "filament_cost")            config.filaments.costs = numbers(value);
        else if (key == "first_layer_height")       config.first_layer_height = float(std::atof(value.c_str()));
        else if (key == "max_print_height")         config.max_print_height = float(std::atof(value.c_str()));
        else if (key == "z_offset")                 config.z_offset = float(std::atof(value.c_str()));
        else if (key == "spiral_vase")              config.spiral_vase_enabled = value != "0";
        else if (key == "use_volumetric_e")         config.use_volumetric_e = value != "0";
        else if (key == "single_extruder_multi_material") config.single_extruder_multi_material = value != "0";
        else if (key == "machine_max_acceleration_x") config.machine_limits.max_acceleration_x = numbers(value);
        else if (key == "machine_max_acceleration_y") config.machine_limits.max_acceleration_y = numbers(value);
        else if (key == "machine_max_acceleration_z") config.machine_limits.max_acceleration_z = numbers(value);
        else if (key == "machine_max_acceleration_e") config.machine_limits.max_acceleration_e = numbers(value);
        else if (key == "machine_max_feedrate_x")     config.machine_limits.max_feedrate_x = numbers(value);
        else if (key == "machine_max_feedrate_y")     config.machine_limits.max_feedrate_y = numbers(value);
        else if (key == "machine_max_feedrate_z")     config.machine_limits.max_feedrate_z = numbers(value);
        else if (key == "machine_max_feedrate_e")     config.machine_limits.max_feedrate_e = numbers(value);
        else if (key == "machine_max_jerk_x")         config.machine_limits.max_jerk_x = numbers(value);
        else if (key == "machine_max_jerk_y")         config.machine_limits.max_jerk_y = numbers(value);
        else if (key == "machine_max_jerk_z")         config.machine_limits.max_jerk_z = numbers(value);
        else if (key == "machine_max_jerk_e")         config.machine_limits.max_jerk_e = numbers(value);
        else if (key == "machine_max_acceleration_extruding") config.machine_limits.max_acceleration_extruding = numbers(value);
        else if (key == "machine_max_acceleration_retracting") config.machine_limits.max_acceleration_retracting = numbers(value);
        else if (key == "machine_max_acceleration_travel") config.machine_limits.max_acceleration_travel = numbers(value);
        else if (key == "machine_min_extruding_rate") config.machine_limits.min_extruding_rate = numbers(value);
        else if (key == "machine_min_travel_rate")   config.machine_limits.min_travel_rate = numbers(value);
        else if (key == "gcode_flavor") {
            if (value == "klipper")            config.flavor = Slic3r::Domain::GCodeFlavor::gcfKlipper;
            else if (value == "marlin2")       config.flavor = Slic3r::Domain::GCodeFlavor::gcfMarlinFirmware;
            else if (value == "marlin")        config.flavor = Slic3r::Domain::GCodeFlavor::gcfMarlinLegacy;
            else if (value == "reprapfirmware") config.flavor = Slic3r::Domain::GCodeFlavor::gcfRepRapFirmware;
            else if (value == "repetier")      config.flavor = Slic3r::Domain::GCodeFlavor::gcfRepetier;
            else if (value == "smoothie")      config.flavor = Slic3r::Domain::GCodeFlavor::gcfSmoothie;
        }
    }

    if (config.filaments.diameters.empty())
        config.filaments.diameters = { pg::DEFAULT_FILAMENT_DIAMETER };
    if (config.filaments.densities.empty())
        config.filaments.densities = { pg::DEFAULT_FILAMENT_DENSITY };
    if (config.filaments.costs.empty())
        config.filaments.costs = { pg::DEFAULT_FILAMENT_COST };

    config.extruders.count = uint8_t(std::max<std::size_t>(1, config.filaments.diameters.size()));
    config.machine_limits.validate();
    return config;
}

float roleSeconds(const pg::MoveVertex& move)
{
    return move.time[std::size_t(pg::TimeMode::Normal)];
}

// The G-code processor walks lines and throws rather than tolerating a last line
// without a terminator, which aborts the process: a file that was cut mid-write,
// or one another tool wrote, then reads as a slicer that crashed on open.
void terminateLastLine(std::string& gcode)
{
    if (!gcode.empty() && gcode.back() != '\n')
        gcode.push_back('\n');
}

// The engine emits one role for every solid layer: `Solid infill` covers the
// visible bottom, internal solid layers and anything else. Orca separates the
// bottom, and the legend has a row for it, so the G-code is re-tagged here from
// the layer index and the same `bottom_solid_layers` the slicer used. The
// processor reads `;TYPE:` comments, so this is a text pass, not a second slice.
int bottomSolidLayers(const std::string& gcode)
{
    const std::string key = "bottom_solid_layers =";
    std::size_t position = gcode.find(key);
    while (position != std::string::npos) {
        std::size_t lineStart = gcode.rfind('\n', position);
        lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
        std::size_t prefix = lineStart;
        while (prefix < position && (gcode[prefix] == ' ' || gcode[prefix] == '\t'))
            ++prefix;
        if (prefix < position && gcode[prefix] == ';') {
            ++prefix;
            while (prefix < position && gcode[prefix] == ' ')
                ++prefix;
            if (prefix == position)
                return std::atoi(gcode.c_str() + position + key.size());
        }
        position = gcode.find(key, position + key.size());
    }
    return 0;
}

void classifyBottomSurface(std::string& gcode)
{
    const int bottomLayers = bottomSolidLayers(gcode);
    if (bottomLayers <= 0)
        return;

    int layer = -1;
    std::size_t position = 0;
    while (position < gcode.size()) {
        std::size_t end = gcode.find('\n', position);
        if (end == std::string::npos)
            end = gcode.size();
        const std::string_view line(gcode.data() + position, end - position);
        if (line.rfind(";LAYER_CHANGE", 0) == 0) {
            ++layer;
        } else if (layer >= 0 && layer < bottomLayers
                   && line.rfind(";TYPE:Solid infill", 0) == 0) {
            gcode.replace(position, line.size(), ";TYPE:Bottom surface");
        }
        position = end + 1;
    }
}

}

bool renderPreviewWithVgcode(const QString& gcodePath, const QString& outPath,
                             float yaw, float pitch, float zoom)
{
    QFile file(gcodePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    std::string gcode = file.readAll().toStdString();
    file.close();
    if (gcode.empty())
        return false;
    terminateLastLine(gcode);
    classifyBottomSurface(gcode);

    pg::ProcessorConfig config = configFor(gcode);
    pg::Processor processor(std::move(config));
    processor.process_buffer(std::move(gcode));
    pg::ProcessorResult result = processor.finalize();
    const std::shared_ptr<const pg::MoveVertices> vertices = result.const_moves();
    if (!vertices || vertices->empty())
        return false;

    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(3, 3);
    format.setDepthBufferSize(24);
    QOffscreenSurface surface;
    surface.setFormat(format);
    surface.create();
    if (!surface.isValid()) {
        qCritical("vgcode-shot: offscreen surface invalid on %s",
                  qPrintable(QGuiApplication::platformName()));
        return false;
    }
    QOpenGLContext gl_context;
    gl_context.setFormat(format);
    if (!gl_context.create()) {
        qCritical("vgcode-shot: QOpenGLContext::create failed");
        return false;
    }
    if (!gl_context.makeCurrent(&surface)) {
        qCritical("vgcode-shot: makeCurrent failed");
        return false;
    }

    glewExperimental = GL_TRUE;
    const GLenum glewStatus = glewInit();
    if (glewStatus != GLEW_OK) {
        qCritical("vgcode-shot: glewInit: %d %s", int(glewStatus),
                  reinterpret_cast<const char*>(glewGetErrorString(glewStatus)));
        return false;
    }
    while (glGetError() != GL_NO_ERROR) {
    }

    using namespace Slic3r::App;
    Render::initialize_render();
    Render::Context& context = Render::Context::instance();
    Render::Device& device = context.device();
    Scene::Scene scene;
    Scene::GeometryDataFactory factory(device, "vgcode-probe");
    libvgcode::FdmViewer viewer;
    viewer.init(device, scene, factory);
    viewer.set_scene(scene);

    libvgcode::FdmViewerInputData input;
    input.vertices = vertices;
    input.gcode = result.const_gcode();
    input.extruders_count = result.extruders_count;
    for (const std::string& encoded : result.extruder_str_colors) {
        Slic3r::Domain::ColorRGB colour;
        Slic3r::Biz::Algorithms::Color::decode_color(encoded, colour);
        input.tools_colors.push_back(colour);
    }
    input.color_print_colors = input.tools_colors;
    viewer.load(std::move(input), Scene::Transform::Identity());
    viewer.set_view_type(libvgcode::ViewType::FeatureType);

    const Slic3r::Domain::BoundingBox3d box = viewer.extrusion_bounding_box();
    if (!box.defined)
        return false;

    Eigen::AlignedBox3d printed;
    int custom_vertices = 0;
    for (const pg::MoveVertex& vertex : *vertices) {
        if (!vertex.is_extrusion())
            continue;
        if (vertex.extrusion_role == Slic3r::Domain::GCodeExtrusionRole::Custom) {
            ++custom_vertices;
            continue;
        }
        printed.extend(vertex.position.cast<double>());
    }
    std::fprintf(stdout,
                 "vgcode-shot: bbox min(%.1f,%.1f,%.1f) max(%.1f,%.1f,%.1f), printed",
                 box.min.x(), box.min.y(), box.min.z(), box.max.x(), box.max.y(), box.max.z());
    if (printed.isEmpty())
        std::fprintf(stdout, " none, custom vertices %d\n", custom_vertices);
    else
        std::fprintf(stdout, " min(%.1f,%.1f,%.1f) max(%.1f,%.1f,%.1f), custom vertices %d\n",
                     printed.min().x(), printed.min().y(), printed.min().z(),
                     printed.max().x(), printed.max().y(), printed.max().z(), custom_vertices);

    const Eigen::AlignedBox3d fit = printed.isEmpty()
        ? Eigen::AlignedBox3d(box.min, box.max)
        : printed;

    constexpr int width = 1200;
    constexpr int height = 800;
    const Render::Rect viewport = {0, 0, width, height};

    Scene::Camera& camera = scene.camera();
    camera.set_projection_type(Scene::CameraProjectionType::Perspective);
    camera.set_viewport(viewport);

    const Slic3r::Domain::Vec3d centre = 0.5 * (fit.min() + fit.max());
    const double radius = std::max(0.5 * (fit.max() - fit.min()).norm(), 0.5);
    const double distance = 2.2 * radius;
    const double yaw_radians = double(yaw) * M_PI / 180.0;
    const double pitch_radians = double(pitch) * M_PI / 180.0;
    const Slic3r::Domain::Vec3d eye = centre
        + distance * Slic3r::Domain::Vec3d(std::cos(pitch_radians) * std::cos(yaw_radians),
                                           std::cos(pitch_radians) * std::sin(yaw_radians),
                                           std::sin(pitch_radians));
    camera.look_at(eye, centre, Slic3r::Domain::Vec3d(0.0, 0.0, 1.0));

    Scene::zoom_to_box(camera, fit);
    if (zoom != 1.0f)
        camera.set_zoom(camera.zoom() * double(zoom));
    viewer.render();

    Render::FramebufferCreationData framebuffer_data;
    framebuffer_data.width = std::size_t(width);
    framebuffer_data.height = std::size_t(height);
    framebuffer_data.num_samples = 4;
    framebuffer_data.color_attachments.resize(1);
    framebuffer_data.color_attachments[0].format = Slic3r::Domain::PixelFormat::RGBA8;
    framebuffer_data.color_attachments[0].mag_filter = Render::TextureMagFilter::Linear;
    framebuffer_data.color_attachments[0].min_filter = Render::TextureMinFilter::Linear;
    Render::Framebuffer* framebuffer = context.framebuffer_manager().create(framebuffer_data);

    std::vector<uint8_t> pixels(std::size_t(width) * std::size_t(height) * 4, 0);
    std::unique_ptr<Render::CommandBuffer> commands = device.create_command_buffer();
    commands->bind_framebuffer(*framebuffer);
    commands->set_depth_test_enabled(true);
    commands->set_viewport(viewport);
    commands->clear_buffers(true, true);
    Slic3r::App::Preview::PreviewSceneRenderCustomizer customizer;
    scene.render(device, *commands, &customizer);

    Render::FramebufferCreationData resolve_data;
    resolve_data.width = std::size_t(width);
    resolve_data.height = std::size_t(height);
    resolve_data.color_attachments.resize(1);
    resolve_data.color_attachments[0].format = Slic3r::Domain::PixelFormat::RGBA8;
    resolve_data.color_attachments[0].mag_filter = Render::TextureMagFilter::Linear;
    resolve_data.color_attachments[0].min_filter = Render::TextureMinFilter::Linear;
    Render::Framebuffer* resolve_target = context.framebuffer_manager().create(resolve_data);

    commands->blit_framebuffer(*framebuffer, *resolve_target, 0, 0, width, height,
                               Render::BlitFramebufferMask::ColorBufferBit,
                               Render::BlitFramebufferFilter::Linear);
    commands->read_pixels(*resolve_target, 0, 0, width, height,
                          Slic3r::Domain::PixelFormat::RGBA8, pixels.data());
    commands->unbind_framebuffer(*framebuffer);
    context.framebuffer_manager().destroy(framebuffer);
    context.framebuffer_manager().destroy(resolve_target);

    gl_context.doneCurrent();

    QImage frame(pixels.data(), width, height, width * 4, QImage::Format_RGBA8888);
    return frame.flipped(Qt::Vertical).save(outPath);
}

static PreviewData buildData(pg::ProcessorResult& result, double filamentCostPerKg)
{
    PreviewData data;

    const std::shared_ptr<const pg::MoveVertices> vertices = result.const_moves();
    if (!vertices || vertices->empty())
        return data;

    data.roles.resize(int(kRoleCount));
    for (std::size_t index = 0; index < kRoleCount; ++index) {
        PreviewRole& role = data.roles[int(index)];
        role.role = int(index);
        role.name = QString::fromLatin1(kRoleNames[index]);
        role.color = next_layer::featureColor(role.name, QVariantMap());
    }

    data.moves.reserve(int(vertices->size()));

    QVector3D previous;
    bool havePrevious = false;
    pg::MoveVertex previousMove;
    bool previousContinuous = false;
    int layerIndex = -1;
    int layerId = -1;
    QVector3D low(0.0f, 0.0f, 0.0f);
    QVector3D high(0.0f, 0.0f, 0.0f);

    for (const pg::MoveVertex& move : *vertices) {
        const QVector3D position(float(move.position.x()), float(move.position.y()),
                                 float(move.position.z()));

        if (!havePrevious) {
            low = high = position;
            havePrevious = true;
        } else {
            low.setX(std::min(low.x(), position.x()));
            low.setY(std::min(low.y(), position.y()));
            low.setZ(std::min(low.z(), position.z()));
            high.setX(std::max(high.x(), position.x()));
            high.setY(std::max(high.y(), position.y()));
            high.setZ(std::max(high.z(), position.z()));
        }

        if (int(move.layer_id) != layerId) {
            layerId = int(move.layer_id);
            ++layerIndex;
            PreviewLayer layer;
            layer.z = float(move.position.z());
            layer.start = data.moves.size();
            layer.end = layer.start;
            data.layers.append(layer);
        }
        if (layerIndex >= 0)
            data.layers.last().end = data.moves.size() + 1;

        const bool segmentValid = havePrevious && move.is_extrusion()
            && previousMove.type == move.type
            && move.position != previousMove.position;

        if (segmentValid) {
            PreviewMove segment;
            segment.x0 = previousMove.position.x();
            segment.y0 = previousMove.position.y();
            segment.z0 = previousMove.position.z();
            segment.x1 = position.x();
            segment.y1 = position.y();
            segment.z1 = position.z();
            segment.width0 = previousMove.width > 0.0f ? previousMove.width : pg::DEFAULT_TOOLPATH_WIDTH;
            segment.height0 = previousMove.height > 0.0f ? previousMove.height : pg::DEFAULT_TOOLPATH_HEIGHT;
            segment.width1 = move.width > 0.0f ? move.width : pg::DEFAULT_TOOLPATH_WIDTH;
            segment.height1 = move.height > 0.0f ? move.height : pg::DEFAULT_TOOLPATH_HEIGHT;
            segment.role0 = int(previousMove.extrusion_role);
            segment.role1 = int(move.extrusion_role);
            segment.layer = std::max(0, layerIndex);
            segment.continuous = previousContinuous;
            data.moves.append(segment);

            if (segment.role1 >= 0 && segment.role1 < int(kRoleCount)) {
                PreviewRole& role = data.roles[segment.role1];
                ++role.moves;
                role.seconds += double(roleSeconds(move));
            }
        }

        previousContinuous = segmentValid;
        previousMove = move;
        previous = position;
    }

    if (data.moves.isEmpty() || data.layers.isEmpty())
        return PreviewData{};

    data.boundsMin = low;
    data.boundsMax = high;

    // Length and mass per role come from the processor's own statistics, not from
    // summing the moves: the processor inserts "phantom" vertices between paths
    // for its viewer, and those keep `delta_extruder`, so a per-move sum counts
    // filament twice (measured: 2.17 m for a 0.97 m print). `mass` is also in
    // density-per-mm3 units, i.e. 1000x grams. The statistics are the numbers
    // The statistics are the numbers the engine prints in the G-code footer,
    // which is what the panel shows.
    using Slic3r::Domain::BasicPrintStatistics;
    using Slic3r::Domain::FullPrintStatistics;

    const std::map<Slic3r::Domain::GCodeExtrusionRole, std::pair<float, float>>* perRole = nullptr;
    if (const auto* basic = std::get_if<BasicPrintStatistics>(&result.print_statistics)) {
        perRole = &basic->used_filaments_per_role;
        data.totalSeconds = double(basic->normal_mode_time.time);
        for (const auto& entry : basic->cost_per_extruder)
            data.totalCost += double(entry.second);
    } else if (const auto* full = std::get_if<FullPrintStatistics>(&result.print_statistics)) {
        perRole = &full->used_filaments_per_role;
        data.totalSeconds = double(full->normal_mode_time.time);
        data.totalCost = double(full->total_filament_cost);
    }

    if (perRole != nullptr) {
        for (PreviewRole& role : data.roles) {
            const auto it = perRole->find(Slic3r::Domain::GCodeExtrusionRole(role.role));
            if (it == perRole->end())
                continue;
            // Measured, not from the doc comment: the statistics carry this in
            // metres (a 0.97 m print reads 0.97), while the footer prints mm.
            role.lengthMm = double(it->second.first) * 1000.0;
            role.massG = double(it->second.second);
        }
    }

    for (const PreviewRole& role : data.roles) {
        data.totalFilamentMm += role.lengthMm;
        data.totalFilamentG += role.massG;
    }

    // cost_per_extruder is empty in this snapshot; the filament price in the
    // config block is per kilogram, which is what the footer's cost is computed
    // from (2.89 g at 27.99/kg = 0.08).
    if (data.totalCost <= 0.0 && filamentCostPerKg > 0.0)
        data.totalCost = data.totalFilamentG / 1000.0 * filamentCostPerKg;

    return data;
}

static bool processFile(const QString& gcodePath, PreviewData& data,
                        Slic3r::App::libvgcode::FdmViewerInputData* input)
{
    QFile file(gcodePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    std::string gcode = file.readAll().toStdString();
    file.close();
    if (gcode.empty())
        return false;
    terminateLastLine(gcode);
    classifyBottomSurface(gcode);

    pg::ProcessorConfig config = configFor(gcode);
    const double filamentCostPerKg =
        config.filaments.costs.empty() ? 0.0 : double(config.filaments.costs.front());
    pg::Processor processor(std::move(config));
    processor.process_buffer(std::move(gcode));
    pg::ProcessorResult result = processor.finalize();

    data = buildData(result, filamentCostPerKg);

    if (input != nullptr) {
        input->vertices = result.const_moves();
        input->gcode = result.const_gcode();
        input->extruders_count = result.extruders_count;
        for (const std::string& encoded : result.extruder_str_colors) {
            Slic3r::Domain::ColorRGB colour;
            Slic3r::Biz::Algorithms::Color::decode_color(encoded, colour);
            input->tools_colors.push_back(colour);
        }
        input->color_print_colors = input->tools_colors;
    }

    return data.valid();
}

PreviewData PreviewReader::read(const QString& gcodePath)
{
    PreviewData data;
    processFile(gcodePath, data, nullptr);
    return data;
}

bool PreviewReader::readForRender(const QString& gcodePath, PreviewData& data,
                                  Slic3r::App::libvgcode::FdmViewerInputData& input)
{
    return processFile(gcodePath, data, &input);
}

QString PreviewReader::roleName(int role)
{
    if (role < 0 || role >= int(kRoleCount))
        return QString();
    return QString::fromLatin1(kRoleNames[role]);
}

int PreviewReader::roleIndex(const QString& name)
{
    for (std::size_t index = 0; index < kRoleCount; ++index) {
        if (name == QLatin1String(kRoleNames[index]))
            return int(index);
    }
    return -1;
}

QColor PreviewReader::roleColor(int role, const QVariantMap& palette)
{
    return next_layer::featureColor(roleName(role), palette);
}