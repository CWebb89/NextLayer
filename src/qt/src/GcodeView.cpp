// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "GcodeView.hpp"

#include "FeatureColors.hpp"
#include "PreviewData.hpp"

#include <Slic3r/App/Preview/PreviewSceneRenderCustomizer.hpp>
#include <Slic3r/App/Render/CommandBuffer.hpp>
#include <Slic3r/App/Render/Context.hpp>
#include <Slic3r/App/Render/Device.hpp>
#include <Slic3r/App/Render/Framebuffer.hpp>
#include <Slic3r/App/Render/FramebufferManager.hpp>
#include <Slic3r/App/Render/Init.hpp>
#include <Slic3r/App/Scene/Camera.hpp>
#include <Slic3r/App/Scene/GeometryDataFactory.hpp>
#include <Slic3r/App/Scene/Scene.hpp>
#include <Slic3r/App/Scene/Transform.hpp>
#include <Slic3r/App/libvgcode/FdmViewer.hpp>

#include <GL/glew.h>

#include <QColor>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QPainter>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr double kReferenceFovY = 30.0;
constexpr int kSupersample = 2;

using Slic3r::App::libvgcode::ViewType;
using Slic3r::Biz::libpgcode::OptionType;

struct ViewTypeEntry
{
    const char* name;
    ViewType type;
};

const ViewTypeEntry VIEW_TYPE_ENTRIES[] = {
    {"Feature type", ViewType::FeatureType},
    {"Height", ViewType::Height},
    {"Line width", ViewType::Width},
    {"Speed", ViewType::Speed},
    {"Actual speed", ViewType::ActualSpeed},
    {"Fan speed", ViewType::FanSpeed},
    {"Temperature", ViewType::Temperature},
    {"Flow", ViewType::VolumetricFlowRate},
    {"Actual flow", ViewType::ActualVolumetricFlowRate},
    {"Layer time", ViewType::LayerTimeLinear},
    {"Layer time (log)", ViewType::LayerTimeLogarithmic},
    {"Tool", ViewType::Tool},
    {"Filament", ViewType::ColorPrint},
};

struct OptionTypeEntry
{
    const char* name;
    OptionType type;
};

const OptionTypeEntry OPTION_TYPE_ENTRIES[] = {
    {"Travel", OptionType::Travels},
    {"Wipe", OptionType::Wipes},
    {"Retract", OptionType::Retractions},
    {"Unretract", OptionType::Unretractions},
    {"Seam", OptionType::Seams},
    {"Filament change", OptionType::ToolChanges},
    {"Color change", OptionType::ColorChanges},
    {"Pause print", OptionType::PausePrints},
    {"Custom G-code", OptionType::CustomGCodes},
};

QString toQString(const Slic3r::Domain::ColorRGB& color)
{
    return QColor::fromRgbF(color.r(), color.g(), color.b()).name(QColor::HexRgb);
}

Slic3r::Domain::Vec3d toPrinter(const QVector3D& scene)
{
    // The Quick 3D world is the printer frame rotated -90 about X: world
    // (x, y, z) is printer (x, -z, y).
    return Slic3r::Domain::Vec3d(double(scene.x()), -double(scene.z()), double(scene.y()));
}

} // namespace

// Owns libvgcode plus its desktop OpenGL context, rendered off-screen in a
// software GL surface so it stays independent of the scene graph's GLES context.
class GcodeView::Impl
{
public:
    ~Impl();

    bool ensureInitialized();
    bool load(Slic3r::App::libvgcode::FdmViewerInputData&& input);
    void reset();
    void setLayerRange(int min, int max);
    void setVisibleMoveRange(int min, int max);
    void applyVisibility(const QSet<int>& hidden);
    void applyViewType(const QString& name);
    void applyOptionVisibility(const QSet<QString>& hidden);
    QVariantList optionLegend() const;
    QVariantList rangeLegend() const;
    QImage render(int width, int height, const QVector3D& eye, const QVector3D& center,
                  const QVector3D& up, qreal fov);

private:
    void applyFeatureColors();
    bool ensureFramebuffer(int width, int height);

    QOffscreenSurface m_surface;
    QOpenGLContext m_gl;

    Slic3r::App::Render::Device* m_device = nullptr;
    std::unique_ptr<Slic3r::App::Scene::Scene> m_scene;
    std::unique_ptr<Slic3r::App::Scene::GeometryDataFactory> m_factory;
    std::unique_ptr<Slic3r::App::libvgcode::FdmViewer> m_viewer;
    std::unique_ptr<Slic3r::App::Render::CommandBuffer> m_commands;

    Slic3r::App::Render::Framebuffer* m_framebuffer = nullptr;
    Slic3r::App::Render::Framebuffer* m_resolveFramebuffer = nullptr;
    int m_framebufferWidth = 0;
    int m_framebufferHeight = 0;
    bool m_loaded = false;
    Slic3r::Domain::BoundingBox3d m_bounds;
};

GcodeView::Impl::~Impl()
{
    if (m_device == nullptr)
        return;
    if (m_gl.makeCurrent(&m_surface)) {
        if (m_framebuffer != nullptr)
            m_device->context().framebuffer_manager().destroy(m_framebuffer);
        if (m_resolveFramebuffer != nullptr)
            m_device->context().framebuffer_manager().destroy(m_resolveFramebuffer);
        m_viewer.reset();
        m_factory.reset();
        m_scene.reset();
        m_commands.reset();
        m_gl.doneCurrent();
    }
}

bool GcodeView::Impl::ensureInitialized()
{
    if (m_device != nullptr)
        return true;

    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setVersion(3, 3);
    format.setDepthBufferSize(24);
    m_surface.setFormat(format);
    m_surface.create();
    if (!m_surface.isValid()) {
        qCritical("GcodeView: offscreen surface invalid on %s",
                  qPrintable(QGuiApplication::platformName()));
        return false;
    }
    m_gl.setFormat(format);
    if (!m_gl.create()) {
        qCritical("GcodeView: QOpenGLContext::create failed");
        return false;
    }
    if (!m_gl.makeCurrent(&m_surface)) {
        qCritical("GcodeView: makeCurrent failed");
        return false;
    }
    glewExperimental = GL_TRUE;
    const GLenum glewStatus = glewInit();
    if (glewStatus != GLEW_OK) {
        qCritical("GcodeView: glewInit: %d %s", int(glewStatus),
                  reinterpret_cast<const char*>(glewGetErrorString(glewStatus)));
        return false;
    }
    while (glGetError() != GL_NO_ERROR) {
    }

    using namespace Slic3r::App;
    Render::initialize_render();
    m_device = &Render::Context::instance().device();
    m_scene = std::make_unique<Scene::Scene>();
    m_scene->set_background_enabled(false);
    m_factory = std::make_unique<Scene::GeometryDataFactory>(*m_device, "gcode-view");
    m_viewer = std::make_unique<libvgcode::FdmViewer>();
    m_viewer->init(*m_device, *m_scene, *m_factory);
    m_viewer->set_scene(*m_scene);
    m_commands = m_device->create_command_buffer();
    return true;
}

void GcodeView::Impl::applyFeatureColors()
{
    using Slic3r::Domain::GCodeExtrusionRole;
    const int count = int(GCodeExtrusionRole::Count);
    for (int role = 0; role < count; ++role) {
        const QString name = PreviewReader::roleName(role);
        const QColor color = next_layer::featureColor(name, QVariantMap());
        m_viewer->set_extrusion_role_color(
            GCodeExtrusionRole(role),
            Slic3r::Domain::ColorRGB(float(color.redF()), float(color.greenF()), float(color.blueF())));
    }
}

bool GcodeView::Impl::load(Slic3r::App::libvgcode::FdmViewerInputData&& input)
{
    if (!ensureInitialized())
        return false;
    if (!m_gl.makeCurrent(&m_surface))
        return false;
    m_viewer->load(std::move(input), Slic3r::App::Scene::Transform::Identity());
    m_viewer->set_view_type(Slic3r::App::libvgcode::ViewType::FeatureType);
    // The engine defaults the tool marker on, which draws a translucent cylinder
    // standing on the bed under the current position. It is shown only when
    // asked, and on a plate it reads as a stray blob beside the print.
    if (m_viewer->is_option_visible(Slic3r::Biz::libpgcode::OptionType::ToolMarker))
        m_viewer->toggle_option_visibility(Slic3r::Biz::libpgcode::OptionType::ToolMarker);
    m_bounds = m_viewer->extrusion_bounding_box();
    m_loaded = true;
    applyFeatureColors();
    m_gl.doneCurrent();
    return true;
}

void GcodeView::Impl::reset()
{
    if (m_device == nullptr)
        return;
    if (m_gl.makeCurrent(&m_surface)) {
        m_viewer->reset();
        m_bounds = Slic3r::Domain::BoundingBox3d();
        m_loaded = false;
        m_gl.doneCurrent();
    }
}

void GcodeView::Impl::setLayerRange(int min, int max)
{
    if (m_device == nullptr)
        return;
    if (m_gl.makeCurrent(&m_surface)) {
        m_viewer->set_layers_range(min, max);
        m_gl.doneCurrent();
    }
}

void GcodeView::Impl::setVisibleMoveRange(int min, int max)
{
    if (m_device == nullptr || !m_loaded)
        return;
    if (max < min)
        return;
    if (m_gl.makeCurrent(&m_surface)) {
        m_viewer->set_view_visible_range(min, max);
        m_gl.doneCurrent();
    }
}

void GcodeView::Impl::applyVisibility(const QSet<int>& hidden)
{
    if (m_device == nullptr)
        return;
    if (!m_gl.makeCurrent(&m_surface))
        return;
    using Slic3r::Domain::GCodeExtrusionRole;
    const int count = int(GCodeExtrusionRole::Count);
    for (int role = 0; role < count; ++role) {
        const GCodeExtrusionRole type = GCodeExtrusionRole(role);
        const bool shouldShow = !hidden.contains(role);
        if (m_viewer->is_extrusion_role_visible(type) != shouldShow)
            m_viewer->toggle_extrusion_role_visibility(type);
    }
    m_gl.doneCurrent();
}

void GcodeView::Impl::applyViewType(const QString& name)
{
    if (m_device == nullptr)
        return;
    if (!m_gl.makeCurrent(&m_surface))
        return;
    for (const ViewTypeEntry& entry : VIEW_TYPE_ENTRIES) {
        if (name == QLatin1String(entry.name)) {
            if (m_viewer->view_type() != entry.type)
                m_viewer->set_view_type(entry.type);
            break;
        }
    }
    m_gl.doneCurrent();
}

void GcodeView::Impl::applyOptionVisibility(const QSet<QString>& hidden)
{
    if (m_device == nullptr || !m_loaded)
        return;
    if (!m_gl.makeCurrent(&m_surface))
        return;
    for (const OptionTypeEntry& entry : OPTION_TYPE_ENTRIES) {
        const bool should_show = !hidden.contains(QString::fromLatin1(entry.name));
        if (m_viewer->is_option_visible(entry.type) != should_show)
            m_viewer->toggle_option_visibility(entry.type);
    }
    m_gl.doneCurrent();
}

QVariantList GcodeView::Impl::optionLegend() const
{
    QVariantList legend;
    if (m_viewer == nullptr || !m_loaded)
        return legend;

    for (const OptionTypeEntry& entry : OPTION_TYPE_ENTRIES) {
        if (!m_viewer->is_option_visible(entry.type))
            continue;
        QVariantMap row;
        row.insert(QStringLiteral("name"), QString::fromLatin1(entry.name));
        row.insert(QStringLiteral("color"), toQString(m_viewer->option_color(entry.type)));
        row.insert(QStringLiteral("seconds"), double(m_viewer->option_estimated_time(entry.type)));
        row.insert(QStringLiteral("visible"), true);
        legend.append(row);
    }

    for (const OptionTypeEntry& entry : OPTION_TYPE_ENTRIES) {
        if (m_viewer->is_option_visible(entry.type))
            continue;
        QVariantMap row;
        row.insert(QStringLiteral("name"), QString::fromLatin1(entry.name));
        row.insert(QStringLiteral("color"), toQString(m_viewer->option_color(entry.type)));
        row.insert(QStringLiteral("seconds"), double(m_viewer->option_estimated_time(entry.type)));
        row.insert(QStringLiteral("visible"), false);
        legend.append(row);
    }
    return legend;
}

QVariantList GcodeView::Impl::rangeLegend() const
{
    QVariantList legend;
    if (m_viewer == nullptr || !m_loaded)
        return legend;

    const ViewType type = m_viewer->view_type();
    if (type == ViewType::FeatureType || type == ViewType::Tool || type == ViewType::ColorPrint)
        return legend;

    QString unit;
    switch (type) {
        case ViewType::Height:
        case ViewType::Width: unit = QStringLiteral("mm"); break;
        case ViewType::Speed:
        case ViewType::ActualSpeed: unit = QStringLiteral("mm/s"); break;
        case ViewType::FanSpeed: unit = QStringLiteral("%"); break;
        case ViewType::Temperature: unit = QStringLiteral("°C"); break;
        case ViewType::VolumetricFlowRate:
        case ViewType::ActualVolumetricFlowRate: unit = QStringLiteral("mm³/s"); break;
        case ViewType::LayerTimeLinear:
        case ViewType::LayerTimeLogarithmic: unit = QStringLiteral("s"); break;
        default: break;
    }

    const Slic3r::App::libvgcode::ColorRange& range = m_viewer->color_range(type);
    const std::vector<float> values = range.values();
    const Slic3r::App::libvgcode::Palette& palette = range.palette();
    if (values.empty() || palette.empty())
        return legend;

    for (size_t i = 0; i < palette.size(); ++i) {
        const float value = values[std::min(i, values.size() - 1)];
        QVariantMap row;
        row.insert(QStringLiteral("name"), QStringLiteral("%1 %2").arg(value, 0, 'f', 2).arg(unit));
        row.insert(QStringLiteral("color"), toQString(palette[i]));
        row.insert(QStringLiteral("visible"), true);
        legend.append(row);
    }
    return legend;
}

bool GcodeView::Impl::ensureFramebuffer(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;
    if (m_framebuffer != nullptr && m_framebufferWidth == width && m_framebufferHeight == height)
        return true;

    if (m_framebuffer != nullptr)
        m_device->context().framebuffer_manager().destroy(m_framebuffer);
    if (m_resolveFramebuffer != nullptr)
        m_device->context().framebuffer_manager().destroy(m_resolveFramebuffer);

    Slic3r::App::Render::FramebufferCreationData data;
    data.width = std::size_t(width);
    data.height = std::size_t(height);
    data.num_samples = 4;
    data.color_attachments.resize(1);
    data.color_attachments[0].format = Slic3r::Domain::PixelFormat::RGBA8;
    data.color_attachments[0].mag_filter = Slic3r::App::Render::TextureMagFilter::Linear;
    data.color_attachments[0].min_filter = Slic3r::App::Render::TextureMinFilter::Linear;
    m_framebuffer = m_device->context().framebuffer_manager().create(data);

    Slic3r::App::Render::FramebufferCreationData resolve_data;
    resolve_data.width = std::size_t(width);
    resolve_data.height = std::size_t(height);
    resolve_data.color_attachments.resize(1);
    resolve_data.color_attachments[0].format = Slic3r::Domain::PixelFormat::RGBA8;
    resolve_data.color_attachments[0].mag_filter = Slic3r::App::Render::TextureMagFilter::Linear;
    resolve_data.color_attachments[0].min_filter = Slic3r::App::Render::TextureMinFilter::Linear;
    m_resolveFramebuffer = m_device->context().framebuffer_manager().create(resolve_data);

    m_framebufferWidth = width;
    m_framebufferHeight = height;
    return m_framebuffer != nullptr && m_resolveFramebuffer != nullptr;
}

QImage GcodeView::Impl::render(int width, int height, const QVector3D& eye, const QVector3D& center,
                               const QVector3D& up, qreal fov)
{
    if (!ensureInitialized())
        return QImage();
    if (!m_gl.makeCurrent(&m_surface))
        return QImage();
    if (!ensureFramebuffer(width, height)) {
        m_gl.doneCurrent();
        return QImage();
    }

    using namespace Slic3r::App;

    Scene::Camera& camera = m_scene->camera();
    camera.set_projection_type(Scene::CameraProjectionType::Perspective);
    camera.set_viewport({0, 0, width, height});
    camera.set_zoom(kReferenceFovY / std::max(double(fov), 1.0));

    const Slic3r::Domain::Vec3d eye_printer = toPrinter(eye);
    const Slic3r::Domain::Vec3d centre_printer = toPrinter(center);
    const double distance = (eye_printer - centre_printer).norm();
    double near_plane = std::max(distance * 0.01, 0.1);
    double far_plane = std::max(distance * 10.0, 100.0);
    const Slic3r::Domain::BoundingBox3d box = m_bounds;
    if (box.defined) {
        // Depth span of the toolpath along the view axis, from its own eight
        // corners. Bounding it by a sphere around the box centre clipped the
        // model the moment the camera orbited off that centre: the near plane
        // tracked a radius that says nothing about which side of the eye a
        // corner is on, so a sliced part lost whichever end was furthest from
        // the orbit point. Corner depths are exact for any eye position.
        const Slic3r::Domain::Vec3d forward = (centre_printer - eye_printer).normalized();
        double nearest = std::numeric_limits<double>::max();
        double furthest = -std::numeric_limits<double>::max();
        for (int corner = 0; corner < 8; ++corner) {
            const Slic3r::Domain::Vec3d point((corner & 1) ? box.max.x() : box.min.x(),
                                              (corner & 2) ? box.max.y() : box.min.y(),
                                              (corner & 4) ? box.max.z() : box.min.z());
            const double depth = (point - eye_printer).dot(forward);
            nearest = std::min(nearest, depth);
            furthest = std::max(furthest, depth);
        }
        near_plane = std::max(nearest * 0.5, 0.05);
        far_plane = std::max(furthest * 1.05, near_plane + 1.0);
    }
    camera.set_z_near_far(near_plane, far_plane);
    camera.look_at(eye_printer, centre_printer, toPrinter(up));

    m_device->load_state();
    m_commands->bind_framebuffer(*m_framebuffer);
    m_commands->set_viewport({0, 0, width, height});
    m_commands->set_depth_test_enabled(true);
    m_commands->set_clear_values({0.0f, 0.0f, 0.0f, 0.0f}, 1.0);
    m_commands->clear_buffers(true, true);

    m_viewer->render();
    Slic3r::App::Preview::PreviewSceneRenderCustomizer customizer;
    m_scene->render(*m_device, *m_commands, &customizer);

    m_commands->blit_framebuffer(*m_framebuffer, *m_resolveFramebuffer, 0, 0, width, height,
                                 Slic3r::App::Render::BlitFramebufferMask::ColorBufferBit,
                                 Slic3r::App::Render::BlitFramebufferFilter::Linear);

    std::vector<uint8_t> pixels(std::size_t(width) * std::size_t(height) * 4, 0);
    m_commands->read_pixels(*m_resolveFramebuffer, 0, 0, width, height,
                            Slic3r::Domain::PixelFormat::RGBA8, pixels.data());
    m_commands->unbind_framebuffer(*m_framebuffer);
    m_commands->submit();
    m_gl.doneCurrent();

    QImage frame(pixels.data(), width, height, width * 4, QImage::Format_RGBA8888);
    return frame.copy();
}

GcodeView::GcodeView(QQuickItem* parent)
    : QQuickPaintedItem(parent)
    , m_impl(std::make_unique<Impl>())
{
    setRenderTarget(QQuickPaintedItem::Image);
    setOpaquePainting(false);
    m_qualityTimer.setSingleShot(true);
    m_qualityTimer.setInterval(150);
    connect(&m_qualityTimer, &QTimer::timeout, this, &GcodeView::scheduleSettledRender);
    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(0);
    connect(&m_renderTimer, &QTimer::timeout, this, [this] {
        m_renderScheduled = false;
        if (!m_impl->ensureInitialized())
            return;
        if (!m_initialized) {
            m_initialized = true;
            emit initializedChanged();
        }

        if (m_clearPending) {
            m_impl->reset();
            m_clearPending = false;
        }
        if (m_pendingInput) {
            m_impl->load(std::move(*m_pendingInput));
            m_pendingInput.reset();
            m_viewTypeDirty = true;
            m_legendDirty = true;
            emit contentsLoaded();
        }
        if (m_layerDirty) {
            m_impl->setLayerRange(m_layerMin, m_layerMax);
            m_layerDirty = false;
            m_moveDirty = true;
        }
        if (m_moveDirty) {
            m_impl->setVisibleMoveRange(m_moveMin, m_moveMax);
            m_moveDirty = false;
        }
        m_impl->applyVisibility(m_hiddenRoles);
        if (m_viewTypeDirty) {
            m_impl->applyViewType(m_viewTypeName);
            m_viewTypeDirty = false;
        }
        m_impl->applyOptionVisibility(m_hiddenOptions);
        if (m_legendDirty) {
            m_legendDirty = false;
            emit legendChanged();
        }

        const qreal dpr = window() != nullptr ? window()->devicePixelRatio() : 1.0;
        const int target_width = std::max(1, int(std::lround(this->width() * dpr)));
        const int target_height = std::max(1, int(std::lround(this->height() * dpr)));
        const int supersample = m_qualityPending ? 1 : kSupersample;
        QImage frame = m_impl->render(target_width * supersample, target_height * supersample,
                                      m_cameraEye, m_cameraCenter, m_cameraUp, m_fov);
        if (frame.isNull())
            return;
        if (supersample > 1)
            frame = frame.scaled(target_width, target_height, Qt::IgnoreAspectRatio,
                                 Qt::SmoothTransformation);
        frame.setDevicePixelRatio(dpr);
        {
            QMutexLocker lock(&m_frameMutex);
            m_frame = frame;
            ++m_frameCount;
        }
        update();
        emit frameCountChanged();
    });
}

GcodeView::~GcodeView() = default;

void GcodeView::paint(QPainter* painter)
{
    QMutexLocker lock(&m_frameMutex);
    if (m_frame.isNull())
        return;
    painter->save();
    painter->translate(0.0, m_frame.height());
    painter->scale(1.0, -1.0);
    painter->drawImage(QRectF(0.0, 0.0, m_frame.width(), m_frame.height()), m_frame);
    painter->restore();
}

int GcodeView::frameCount() const
{
    QMutexLocker lock(&m_frameMutex);
    return m_frameCount;
}

void GcodeView::setCameraEye(const QVector3D& eye)
{
    if (eye == m_cameraEye)
        return;
    m_cameraEye = eye;
    scheduleRender();
}

void GcodeView::setCameraCenter(const QVector3D& center)
{
    if (center == m_cameraCenter)
        return;
    m_cameraCenter = center;
    scheduleRender();
}

void GcodeView::setCameraUp(const QVector3D& up)
{
    if (up == m_cameraUp)
        return;
    m_cameraUp = up;
    scheduleRender();
}

void GcodeView::setFov(qreal fov)
{
    if (qFuzzyCompare(fov, m_fov))
        return;
    m_fov = fov;
    scheduleRender();
}

void GcodeView::setInput(Slic3r::App::libvgcode::FdmViewerInputData&& input)
{
    m_pendingInput = std::make_unique<Slic3r::App::libvgcode::FdmViewerInputData>(std::move(input));
    scheduleRender();
}

void GcodeView::setLayerRange(int minLayer, int maxLayer)
{
    m_layerMin = minLayer;
    m_layerMax = maxLayer;
    m_layerDirty = true;
    scheduleRender();
}

void GcodeView::setVisibleMoveRange(int minMove, int maxMove)
{
    m_moveMin = minMove;
    m_moveMax = maxMove;
    m_moveDirty = true;
    scheduleRender();
}

void GcodeView::setRoleVisibility(int role, bool visible)
{
    if (visible)
        m_hiddenRoles.remove(role);
    else
        m_hiddenRoles.insert(role);
    scheduleRender();
}

void GcodeView::setViewType(const QString& name)
{
    if (name == m_viewTypeName && !m_viewTypeDirty)
        return;
    m_viewTypeName = name;
    m_viewTypeDirty = true;
    m_legendDirty = true;
    scheduleRender();
}

void GcodeView::setOptionVisible(const QString& name, bool visible)
{
    if (visible)
        m_hiddenOptions.remove(name);
    else
        m_hiddenOptions.insert(name);
    m_legendDirty = true;
    scheduleRender();
}

QVariantList GcodeView::optionLegend() const
{
    return m_impl->optionLegend();
}

QVariantList GcodeView::rangeLegend() const
{
    return m_impl->rangeLegend();
}

QVariantList GcodeView::viewTypeChoices()
{
    QVariantList choices;
    for (const ViewTypeEntry& entry : VIEW_TYPE_ENTRIES) {
        QVariantMap item;
        item.insert(QStringLiteral("value"), QString::fromLatin1(entry.name));
        item.insert(QStringLiteral("label"), QString::fromLatin1(entry.name));
        choices.append(item);
    }
    return choices;
}

void GcodeView::clearContents()
{
    m_clearPending = true;
    scheduleRender();
}

void GcodeView::scheduleRender()
{
    m_qualityPending = true;
    m_qualityTimer.start();
    if (m_renderScheduled)
        return;
    m_renderScheduled = true;
    m_renderTimer.start();
}

void GcodeView::scheduleSettledRender()
{
    if (!m_qualityPending)
        return;
    m_qualityPending = false;
    if (m_renderScheduled)
        return;
    m_renderScheduled = true;
    m_renderTimer.start();
}
