#include "GcodeBrand.hpp"

#include <Slic3r/Version.hpp>
#include "GcodeView.hpp"
#include "Host.hpp"
#include "MeshGeometry.hpp"
#include "Palette.hpp"
#include "PreviewData.hpp"
#include "PresetStore.hpp"
#include "Calibration.hpp"
#include "PrinterLink.hpp"
#include "Updater.hpp"
#include "PlateDocument.hpp"
#include "PlateScene.hpp"
#include "QtPlatform.hpp"
#include "SceneSelftest.hpp"
#include "SliceRuntime.hpp"
#include "Session.hpp"
#include "SettingsModel.hpp"
#include "SliceController.hpp"
#include "Toolpath.hpp"
#include "TransformController.hpp"

#include <Slic3r/App/CLI/CLIApp.hpp>
#include <Slic3r/App/Init.hpp>
#include <Slic3r/Biz/Format/STL.hpp>
#include <Slic3r/Biz/Scene/BedGeometry.hpp>
#include <Slic3r/Directories.hpp>
#include <Slic3r/Domain/ConfigBoxesFDM.hpp>
#include <Slic3r/Domain/ConfigDefsFDM.hpp>
#include <Slic3r/Domain/TriangleMesh.hpp>
#include <Slic3r/Domain/Workbench.hpp>

#include <QCommandLineOption>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <algorithm>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QEventLoop>
#include <QPointer>
#include <QFile>
#include <QPainter>
#include <QCryptographicHash>
#include <QGuiApplication>

#include <string_view>

#include <QtWebEngineQuick/QtWebEngineQuick>

#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointF>
#include <QDateTime>
#include <QSvgRenderer>
#include <QUrl>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QMouseEvent>
#include <QThread>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

namespace {

constexpr const char* kBaseConfigPath = ":/next-layer/base-config.json";

QString qmlEntry(const QString& override)
{
    if (!override.isEmpty())
        return override;

    const QString beside = QCoreApplication::applicationDirPath() + "/qml/main.qml";
    if (QFile::exists(beside))
        return beside;

    return QString::fromLatin1(NEXT_LAYER_QML_DIR) + "/main.qml";
}

// The close guard: a plate is work, and closing the window must not be how it is
// lost. The QML side owns the question -- it knows whether there is a file to
// save to -- so the filter only decides whether to ask at all.
class CloseGuard : public QObject
{
public:
    explicit CloseGuard(QQuickWindow* window)
        : QObject(window)
        , m_window(window)
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::Close || watched != m_window)
            return QObject::eventFilter(watched, event);

        if (m_window->property("allowClose").toBool())
            return QObject::eventFilter(watched, event);

        const bool unsaved = m_window->property("unsavedChanges").toBool();
        if (!unsaved)
            return QObject::eventFilter(watched, event);

        event->ignore();
        QMetaObject::invokeMethod(m_window, "promptClose");
        return true;
    }

private:
    QPointer<QQuickWindow> m_window;
};

void sendMouse(QQuickWindow* window, QEvent::Type type, Qt::MouseButton button,
               Qt::MouseButtons buttons, const QPointF& local)
{
    QMouseEvent event(type, local, window->mapToGlobal(local.toPoint()), button, buttons,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(window, &event);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void dragTest(QQuickWindow* window, QObject* viewport, Qt::MouseButton button,
              const QPoint& from, const QPoint& delta, const char* label)
{
    const QPointF start(from);
    sendMouse(window, QEvent::MouseButtonPress, button, button, start);
    for (int step = 1; step <= 6; ++step)
        sendMouse(window, QEvent::MouseMove, Qt::NoButton, button,
                  start + QPointF(delta.x() * step / 6.0, delta.y() * step / 6.0));
    if (button == Qt::LeftButton && (viewport->property("selected").toBool()
                                     || QString::fromLatin1(label) == QLatin1String("drag model"))) {
        QQmlExpression offset(qmlContext(viewport), viewport,
                              QStringLiteral("String(dragOffset.x) + ',' + String(dragOffset.y)"));
        std::fprintf(stdout, "  dragOffset %s\n", offset.evaluate().toString().toUtf8().constData());
    }

    sendMouse(window, QEvent::MouseButtonRelease, button, Qt::NoButton, start + QPointF(delta));

    const QVector3D pan = viewport->property("panOffset").value<QVector3D>();
    std::fprintf(stdout, "%s: yaw %.2f pitch %.2f pan (%.1f, %.1f, %.1f) selected %d\n", label,
                 viewport->property("yaw").toDouble(), viewport->property("pitch").toDouble(),
                 pan.x(), pan.y(), pan.z(), viewport->property("selected").toBool());
}

// Drives a preview slider with synthetic pointer events and reports the layer
// after every intermediate move. Anything other than a one-layer step per
// pointer move means the slider is not scrubbing layer by layer.
void dragSliderHandle(QQuickWindow* window, QQuickItem* slider, const QPoint& delta, int steps)
{
    QQmlContext* context = qmlContext(slider);
    const auto layerNow = [context, slider] {
        QQmlExpression read(context, slider, QStringLiteral("toolpath.currentLayer"));
        return read.evaluate().toInt();
    };

    const QPointF handle = slider->property("upperHandlePoint").toPointF();
    const QPointF origin = slider->mapToScene(handle);
    const int startLayer = layerNow();
    std::fprintf(stdout, "slider test: %s at (%g, %g), start layer %d\n",
                 slider->objectName().toUtf8().constData(), origin.x(), origin.y(), startLayer);

    sendMouse(window, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton, origin);
    std::fprintf(stdout, "slider test: grabbed '%s', size %gx%g\n",
                 slider->property("draggingHandle").toString().toUtf8().constData(),
                 slider->width(), slider->height());

    QStringList seen;
    seen.append(QString::number(layerNow()));
    int previous = startLayer;
    bool stepped = true;
    for (int step = 1; step <= steps; ++step) {
        sendMouse(window, QEvent::MouseMove, Qt::NoButton, Qt::LeftButton,
                  origin + QPointF(delta.x() * step, delta.y() * step));
        const int layer = layerNow();
        seen.append(QString::number(layer) + "/" + QString::number(slider->width()));
        if (std::abs(layer - previous) > 1)
            stepped = false;
        previous = layer;
    }
    sendMouse(window, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton,
              origin + QPointF(delta.x() * steps, delta.y() * steps));

    std::fprintf(stdout, "slider test: layers %s\n",
                 seen.join(QLatin1Char(',')).toUtf8().constData());
    std::fprintf(stdout, "slider test: %s\n", stepped ? "layer by layer" : "steps skipped");
}

void runLayerSliderTest(QQuickWindow* window)
{
    if (auto* vertical = window->findChild<QQuickItem*>(QStringLiteral("layerSlider")))
        dragSliderHandle(window, vertical, QPoint(0, 3), 24);
    else
        std::fprintf(stderr, "slider test: viewport layer slider not found\n");
}


void runInputTest(QQuickWindow* window, QObject* geometry)
{
    QObject* viewport = window->findChild<QObject*>(QStringLiteral("viewport"));
    if (viewport == nullptr) {
        std::fprintf(stderr, "input test: viewport object not found\n");
        return;
    }

    std::fprintf(stdout, "input test: yaw %.2f pitch %.2f zoom %.3f\n",
                 viewport->property("yaw").toDouble(),
                 viewport->property("pitch").toDouble(),
                 viewport->property("zoom").toDouble());

    const auto boundsOf = [geometry] {
        const QVector3D low = geometry->property("boundsMin").value<QVector3D>();
        const QVector3D high = geometry->property("boundsMax").value<QVector3D>();
        return QVector3D((low.x() + high.x()) / 2, (low.y() + high.y()) / 2,
                         (low.z() + high.z()) / 2);
    };
    const QVector3D centreBefore = boundsOf();

    dragTest(window, viewport, Qt::LeftButton, QPoint(700, 420), QPoint(90, 40), "orbit background");
    dragTest(window, viewport, Qt::LeftButton, QPoint(1100, 620), QPoint(60, -30), "orbit on model");
    const QVector3D centreAfterOrbit = boundsOf();
    std::fprintf(stdout, "centre before (%.2f, %.2f, %.2f) after orbit (%.2f, %.2f, %.2f)\n",
                 centreBefore.x(), centreBefore.y(), centreBefore.z(),
                 centreAfterOrbit.x(), centreAfterOrbit.y(), centreAfterOrbit.z());
    dragTest(window, viewport, Qt::RightButton, QPoint(900, 500), QPoint(80, 40), "pan right");
    dragTest(window, viewport, Qt::MiddleButton, QPoint(900, 500), QPoint(-60, -20), "pan middle");

    const QPointF wheelPos(900, 500);
    QWheelEvent wheel(wheelPos, window->mapToGlobal(wheelPos.toPoint()), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window, &wheel);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    std::fprintf(stdout, "zoom after wheel up: %.3f\n", viewport->property("zoom").toDouble());

    viewport->setProperty("yaw", 28.0);
    viewport->setProperty("pitch", -18.0);
    viewport->setProperty("zoom", 1.0);
    viewport->setProperty("panOffset", QVector3D(0, 0, 0));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
}

void runDragTest(QQuickWindow* window, QObject* geometry)
{
    QObject* viewport = window->findChild<QObject*>(QStringLiteral("viewport"));
    auto* viewportItem = qobject_cast<QQuickItem*>(viewport);
    if (viewportItem == nullptr) {
        std::fprintf(stderr, "drag test: viewport object not found\n");
        return;
    }

    QQmlExpression finder(qmlContext(viewport), viewport, QStringLiteral("findModelPoint()"));
    const QString found = finder.evaluate().toString();
    if (found == QLatin1String("none")) {
        std::fprintf(stdout, "drag test: model not found on screen\n");
        return;
    }

    const QStringList parts = found.split(QLatin1Char(','));
    const QPoint start = viewportItem->mapToScene(QPointF(parts.at(0).toInt(),
                                                          parts.at(1).toInt())).toPoint();
    QQmlExpression depth(qmlContext(viewport), viewport,
                         QStringLiteral("String(hitAt(%1, %2).w)").arg(parts.at(0)).arg(parts.at(1)));
    const QPointF origin = viewportItem->mapToScene(QPointF(0, 0));
    std::fprintf(stdout, "viewport origin (%g, %g) size (%g, %g)\n", origin.x(), origin.y(),
                 viewportItem->width(), viewportItem->height());
    std::fprintf(stdout, "drag start local (%s, %s) window (%d, %d) depth %s\n",
                 parts.at(0).toUtf8().constData(), parts.at(1).toUtf8().constData(),
                 start.x(), start.y(), depth.evaluate().toString().toUtf8().constData());
    const auto centre = [geometry] {
        const QVector3D low = geometry->property("boundsMin").value<QVector3D>();
        const QVector3D high = geometry->property("boundsMax").value<QVector3D>();
        return QVector3D((low.x() + high.x()) / 2, (low.y() + high.y()) / 2,
                         (low.z() + high.z()) / 2);
    };

    const QVector3D before = centre();
    dragTest(window, viewport, Qt::LeftButton, start, QPoint(70, 30), "drag model");
    for (int tick = 0; tick < 25; ++tick) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QThread::msleep(100);
    }
    const QVector3D after = centre();
    std::fprintf(stdout, "object centre (%.2f, %.2f) -> (%.2f, %.2f), selected %d\n",
                 before.x(), before.y(), after.x(), after.y(),
                 viewport->property("selected").toBool());

}


int countNonBackground(const QImage& image)
{
    const QImage frame = image.convertToFormat(QImage::Format_ARGB32);
    if (frame.isNull())
        return -1;
    const QRgb background = frame.pixel(0, 0);
    int lit = 0;
    for (int y = 0; y < frame.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(frame.constScanLine(y));
        for (int x = 0; x < frame.width(); ++x)
            if (row[x] != background)
                ++lit;
    }
    return lit;
}

std::string dataDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return (base + "/next-layer").toStdString();
}

Slic3r::App::InitParams engineParams()
{
    Slic3r::App::InitParams params;
    params.misc.datadir = dataDir();
    return params;
}

void prepareDataDir()
{
    const QString data = QString::fromStdString(dataDir());
    QDir().mkpath(data + "/physical_printer");

    const QString vendor = data + "/presets/local/prusa-research-fff";
    if (!QFileInfo::exists(vendor)) {
        const QString assets = QString::fromLatin1(NEXT_LAYER_RESOURCES_DIR)
                             + "/presets/prusa-research-fff";
        if (QFileInfo::exists(assets))
            QFile::link(assets, vendor);
    }

    // The engine's shipped printer presets spell an all-zero extruder offset as
    // `'0x0'`, which its own reader reports as a type mismatch on every start. It
    // is left alone: removing the entry makes a multi-extruder machine fail to
    // slice outright ("error 5 extruder_offset"), and a warning is much cheaper
    // than a broken tool changer. Printed once per profile load, by the engine's
    // own bundle rather than by anything this app writes.

    // The engine's bed resolver checks <datadir>/bed/<file> before falling back
    // to the shipped resources. Copy the vendor bed meshes up front so slicing
    // can resolve a bed model even when the resources tree is not shipped.
    const QString assetsDir = QString::fromLatin1(NEXT_LAYER_RESOURCES_DIR)
                            + "/presets/prusa-research-fff/PrusaResearch/assets";
    const QString bedDir = data + "/bed";
    QDir().mkpath(bedDir);
    const QStringList beds = {
        QStringLiteral("mk2_bed.stl"), QStringLiteral("mk3_bed.stl"),
        QStringLiteral("mk4_bed.stl"), QStringLiteral("mini_bed.stl"),
        QStringLiteral("xl_bed.stl"), QStringLiteral("coreone_bed.stl"),
        QStringLiteral("coreonel_bed.stl"), QStringLiteral("coreoneindx_bed.stl")
    };
    for (const QString& bed : beds) {
        const QString src = assetsDir + QLatin1Char('/') + bed;
        const QString dst = bedDir + QLatin1Char('/') + bed;
        if (!QFileInfo::exists(src))
            continue;
        if (!QFileInfo::exists(dst)
            || QFileInfo(dst).lastModified() < QFileInfo(src).lastModified()) {
            QFile::remove(dst);
            QFile::copy(src, dst);
        }
    }
}

void initEngine()
{
    Slic3r::App::init_common();
    Slic3r::App::init_paths(engineParams());
    Slic3r::set_resources_dir(NEXT_LAYER_RESOURCES_DIR);
    Slic3r::Biz::Scene::BedGeometry::set_resolver([](const std::string& name) {
        const QString fileName = QFileInfo(QString::fromStdString(name)).fileName();
        if (fileName.isEmpty())
            return std::string();

        const QString bed = QString::fromStdString(dataDir()) + QStringLiteral("/bed/") + fileName;
        if (QFileInfo(bed).isFile())
            return bed.toStdString();
        return std::string(NEXT_LAYER_RESOURCES_DIR) + name;
    });
    prepareDataDir();
}

struct PrinterSetup
{
    QVector<QPointF> bed;
    double maxHeight = 0.0;
    QString model;
};

// The plate mesh a printer model ships with. Voron machines are not in the
// engine's vendor manifest (it is a PrusaSlicer tree), so their asset ships with
// the app from the same OrcaSlicer profile the machine settings come from. Prusa
// beds come from the engine's own vendor assets, keyed by the `printer_model`
// config value (the vendor's base model plus any tool/nozzle variant). Artwork is
// deliberately absent: the plate is drawn as a solid colour with the grid over it.
QString bedStlForPrinter(const QString& model)
{
    if (model.startsWith(QStringLiteral("COREONEL")))
        return QStringLiteral("coreonel_bed.stl");
    if (model.startsWith(QStringLiteral("COREONE_INDX")))
        return QStringLiteral("coreoneindx_bed.stl");
    if (model.startsWith(QStringLiteral("COREONE")))
        return QStringLiteral("coreone_bed.stl");
    if (model.startsWith(QStringLiteral("MK4")) || model.startsWith(QStringLiteral("MK3.9")))
        return QStringLiteral("mk4_bed.stl");
    if (model.startsWith(QStringLiteral("MK3")))
        return QStringLiteral("mk3_bed.stl");
    if (model.startsWith(QStringLiteral("MK2")))
        return QStringLiteral("mk2_bed.stl");
    if (model.startsWith(QStringLiteral("MINI")))
        return QStringLiteral("mini_bed.stl");
    if (model.startsWith(QStringLiteral("XL")))
        return QStringLiteral("xl_bed.stl");
    return QString();
}

QString bedAssetResource(const QString& printerModel)
{
    if (printerModel.compare(QStringLiteral("Voron 2.4 350"), Qt::CaseInsensitive) == 0)
        return QStringLiteral(":/next-layer/bed/voron24_350_plate.stl");

    const QString stl = bedStlForPrinter(printerModel);
    if (stl.isEmpty())
        return QString();
    return QString::fromLatin1(NEXT_LAYER_RESOURCES_DIR)
         + QStringLiteral("/presets/prusa-research-fff/PrusaResearch/assets/")
         + stl;
}

// The plate mesh has to be a real file: the engine's STL reader takes a path.
QString bedPlatePath(const QString& printerModel)
{
    const QString resource = bedAssetResource(printerModel);
    if (resource.isEmpty())
        return QString();

    const QString target = QString::fromStdString(dataDir()) + QStringLiteral("/bed/")
                         + QFileInfo(resource).fileName();
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!QFileInfo::exists(target) || QFileInfo(target).lastModified() < QFileInfo(resource).lastModified()) {
        QFile::remove(target);
        if (!QFile::copy(resource, target))
            return QString();
    }
    return target;
}

PrinterSetup readPrinterSetup()
{
    PrinterSetup setup;

    QFile file(QString::fromLatin1(kBaseConfigPath));
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject printer = QJsonDocument::fromJson(file.readAll())
            .object()
            .value("configuration")
            .toObject()
            .value("printer_settings")
            .toObject();

        for (const QJsonValue& point : printer.value("bed_shape").toArray()) {
            const QJsonArray pair = point.toArray();
            if (pair.size() == 2)
                setup.bed.append(QPointF(pair.at(0).toDouble(), pair.at(1).toDouble()));
        }
        setup.maxHeight = printer.value("max_print_height").toDouble();
        setup.model = printer.value("printer_model").toString();
    }

    if (setup.bed.size() < 3 || setup.maxHeight <= 0.0) {
        std::fprintf(stderr, "config template carried no usable bed, falling back to 250x220x270\n");
        setup.bed = {QPointF(0.0, 0.0), QPointF(250.0, 0.0), QPointF(250.0, 220.0), QPointF(0.0, 220.0)};
        setup.maxHeight = 270.0;
    }
    return setup;
}

QVariantList toVariantBed(const QVector<QPointF>& bed)
{
    QVariantList points;
    points.reserve(bed.size());
    for (const QPointF& point : bed)
        points.append(QVariantList{point.x(), point.y()});
    return points;
}

QVector<QPointF> bedFromVariant(const QVariantList& bed)
{
    QVector<QPointF> points;
    points.reserve(bed.size());
    for (const QVariant& value : bed) {
        const QVariantList point = value.toList();
        if (point.size() < 2)
            continue;
        points.append(QPointF(point.at(0).toDouble(), point.at(1).toDouble()));
    }
    return points;
}

QVector3D bedExtent(const QVector<QPointF>& bed, bool maximum)
{
    double x = maximum ? -1e9 : 1e9;
    double y = maximum ? -1e9 : 1e9;
    for (const QPointF& point : bed) {
        x = maximum ? std::max(x, point.x()) : std::min(x, point.x());
        y = maximum ? std::max(y, point.y()) : std::min(y, point.y());
    }
    return QVector3D(float(x), float(y), 0.0f);
}

// A `bed_model` mesh is authored around its own origin with its top face on
// z = 0; the plate works in printer coordinates with the bed starting at
// (0, 0). Centre the plate on the bed and keep its top on the bed plane.
bool loadPlateGeometry(MeshGeometry& geometry, const QString& path, const QVector2D& bedCentre)
{
    const auto mesh = Slic3r::Biz::load_stl(path.toStdString());
    if (!mesh || mesh->its.vertices.empty()) {
        geometry.setMesh(Slic3r::Domain::TriangleMesh{});
        return false;
    }

    Slic3r::Domain::TriangleMesh plate = *mesh;
    Slic3r::Domain::Vec3f low = plate.its.vertices.front();
    Slic3r::Domain::Vec3f high = plate.its.vertices.front();
    for (const Slic3r::Domain::Vec3f& vertex : plate.its.vertices) {
        low = low.cwiseMin(vertex);
        high = high.cwiseMax(vertex);
    }
    plate.translate(Slic3r::Domain::Vec3f(bedCentre.x() - (low.x() + high.x()) * 0.5f,
                                          bedCentre.y() - (low.y() + high.y()) * 0.5f,
                                          -high.z()));
    geometry.setMesh(plate);
    return true;
}

QVector2D bedCentre(const QVector<QPointF>& bed)
{
    qreal minX = bed.constFirst().x();
    qreal maxX = minX;
    qreal minY = bed.constFirst().y();
    qreal maxY = minY;
    for (const QPointF& point : bed) {
        minX = qMin(minX, point.x());
        maxX = qMax(maxX, point.x());
        minY = qMin(minY, point.y());
        maxY = qMax(maxY, point.y());
    }
    return QVector2D(qreal((minX + maxX) * 0.5), qreal((minY + maxY) * 0.5));
}

double bedRadius(const QVector<QPointF>& bed)
{
    qreal minX = bed.constFirst().x();    qreal maxX = minX;
    qreal minY = bed.constFirst().y();
    qreal maxY = minY;
    for (const QPointF& point : bed) {
        minX = qMin(minX, point.x());
        maxX = qMax(maxX, point.x());
        minY = qMin(minY, point.y());
        maxY = qMax(maxY, point.y());
    }
    return 0.5 * std::hypot(double(maxX - minX), double(maxY - minY));
}

QString settingsValueText(const QVariant& value)
{
    if (!value.isValid())
        return QStringLiteral("<empty>");
    if (value.typeId() != QMetaType::QString && value.canConvert<QVariantList>())
        return QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(value.toList()))
                                     .toJson(QJsonDocument::Compact));
    return value.toString();
}

QStringList specWords(const QString& spec)
{
    return spec.split(QLatin1Char(';'), Qt::SkipEmptyParts);
}

// The toolpath only: comment lines carry the generation timestamp and fresh
// UUIDs, so they cannot be compared between two runs of the same slice.
QString toolpathDigest(const QString& gcodePath)
{
    QFile file(gcodePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("unreadable");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        if (line.startsWith(';'))
            continue;
        hash.addData(line);
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool applyTransformSpec(const QString& spec, Slic3r::App::InitParams& params)
{
    for (const QString& token : specWords(spec)) {
        const QStringList parts = token.split(QLatin1Char('='));
        const QString key = parts.first();
        const QString value = parts.size() > 1 ? parts.at(1) : QString();
        const QStringList numbers = value.split(QLatin1Char(','));

        if (key == QLatin1String("rotate")) {
            if (numbers.size() != 3)
                return false;
            Slic3r::Domain::Vec3d rotation;
            rotation[0] = numbers.at(0).toDouble();
            rotation[1] = numbers.at(1).toDouble();
            rotation[2] = numbers.at(2).toDouble();
            params.transform.rotation = rotation;
        } else if (key == QLatin1String("scale")) {
            params.transform.scale = Slic3r::Domain::FloatOrPercentage{value.toDouble()};
        } else if (key == QLatin1String("cut")) {
            params.transform.cut_z = value.toDouble();
        } else if (key == QLatin1String("duplicate")) {
            params.transform.duplicate = uint32_t(value.toUInt());
        } else if (key == QLatin1String("move")) {
            if (numbers.size() != 2)
                return false;
            Slic3r::Domain::Vec2d target;
            target[0] = numbers.at(0).toDouble();
            target[1] = numbers.at(1).toDouble();
            params.transform.align_xy = target;
        } else if (key == QLatin1String("center")) {
            params.transform.center = Slic3r::Domain::Vec2d{125.0, 110.0};
        } else if (key == QLatin1String("drop")) {
            params.transform.ensure_on_bed = true;
        } else {
            return false;
        }
    }
    return true;
}

int sliceToFile(const QString& model, const QString& config, const QString& output){
    Slic3r::App::InitParams params = engineParams();
    params.input.input_files.push_back(model.toStdString());
    params.input.config_file   = config.toStdString();
    params.action.export_gcode = true;
    params.misc.output         = output.toStdString();
    const int code = Slic3r::App::CLI::run(params);
    std::fprintf(stdout, "slice exit %d -> %s\n", code, output.toUtf8().constData());
    return code;
}

}

int main(int argc, char* argv[])
{
    // QtWayland 6.12 logs on every v3 text-input disable, including for surfaces
    // that were never focused. Only its logging is dropped, not input handling.
    QLoggingCategory::setFilterRules(QStringLiteral("qt.qpa.wayland.textinput=false"));

    // The engine's project model is written for a single thread: its interactors
    // and the slicing jobs assume the GUI thread to be the one that owns the
    // workbench. The basic render loop keeps Qt's scene graph on that thread, so
    // the slice controller and the viewport never sit on a cross-thread boundary.
    qputenv("QSG_RENDER_LOOP", "basic");

    // The Device tab shows the printer's own web interface, and Chromium has to be
    // initialised before the application object exists. A headless run has no
    // Device tab, so it skips this: initialising Chromium there costs a GPU process
    // and can take the GL surface eglfs renders the smoke frame on.
    bool headless = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--smoke" || argument == "--slice" || argument == "--selftest-slice"
            || argument == "--calibration-test" || argument == "--mesh-tools-test"
            || argument == "--print-host-test" || argument == "--print-host-start"
            || argument == "--update-install" || argument == "--dump-config"
            || argument == "--scene-selftest" || argument == "--dump-presets"
            || argument == "--dump-presets-json")
            headless = true;
    }
    if (!headless)
        QtWebEngineQuick::initialize();

    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("next-layer"));
    QGuiApplication::setApplicationName(QStringLiteral("next-layer"));

    QCommandLineParser parser;
    parser.setApplicationDescription("next-layer: slicer frontend over the PrusaSlicer 3.0 engine");
    parser.addPositionalArgument("model", "STL model to open (optional: the window opens with an empty bed)");
    const QCommandLineOption qml_option("qml", "QML entry point.", "path");
    const QCommandLineOption smoke_option("smoke", "Render one frame, report coverage, exit.");
    const QCommandLineOption paint_test_option("paint-test",
        "Paint RADIUS mm of every downward-facing facet above the bed on the first object, so a --smoke frame shows the painted overlay.",
        "radius");
    const QCommandLineOption input_test_option("input-test", "Feed synthetic mouse input to the viewport and report the camera state.");
    const QCommandLineOption slider_test_option("slider-test",
        "Drag the preview layer slider with synthetic input and report the layer after every step.");
    const QCommandLineOption shot_option("shot", "Write the rendered frame to a PNG.", "path");
    const QCommandLineOption slice_option("slice", "Slice the model with CONFIG into GCODE and exit without a window.", "CONFIG,GCODE");
    const QCommandLineOption preview_option("preview", "Open a G-code file in the toolpath preview.", "path");
    const QCommandLineOption dump_config_option("dump-config", "Write the settings model's engine config to a file and exit.", "path");
    const QCommandLineOption dump_tabs_option("dump-tabs", "Print the settings tab tree with per-sub-tab counts and exit.");
    const QCommandLineOption dump_names_option("dump-setting-names",
        "Print every setting name the sidebar exposes and exit.");
    const QCommandLineOption dump_subtab_option("dump-subtab", "Print the settings of one sub-tab (TAB/SUBTAB) and exit.", "tab/subtab");
    const QCommandLineOption transform_option("transform",
        "Apply transforms (rotate=x,y,z;scale=f;cut=z;duplicate=n;move=x,y;center;drop) and export STL to OUT.",
        "SPEC,OUT");
    const QCommandLineOption hide_feature_option("hide-feature", "Hide a preview line type by its legend name (comma separated).", "names");
    const QCommandLineOption view_type_option("view-type", "Preview colouring mode by name, e.g. 'Speed' or 'Layer time'.", "name");
    const QCommandLineOption hide_option_option("hide-option", "Hide a preview option by name, e.g. Travel or Seam (comma separated).", "names");
    const QCommandLineOption single_layer_option("single-layer", "Show only the current preview layer, not the whole part.");
    const QCommandLineOption view_option("view", "Camera as YAW,PITCH,ZOOM for smoke captures.", "yaw,pitch,zoom");
    const QCommandLineOption camera_view_option("camera-view", "Camera preset: iso, top, bottom, front, rear, left, right.", "name");
    const QCommandLineOption ortho_option("ortho", "Start with the orthographic camera.");
    const QCommandLineOption layer_option("layer", "Open the preview at layer N (1-based).", "n");
    const QCommandLineOption layer_from_option("layer-from", "Open the preview with layer N as the first visible layer (1-based).", "n");
    const QCommandLineOption moves_option("moves", "Limit the preview's current layer to moves FROM,TO (1-based).", "from,to");
    const QCommandLineOption plates_option("plates", "Start with N build plates.", "count");
    const QCommandLineOption settings_tab_option("settings-tab", "Open the settings panel on TAB/SUBTAB.", "tab/subtab");
    const QCommandLineOption preset_dialog_option("preset-dialog",
        "Open the preset editor on TAB (process, filament, printer) before the frame is captured.", "tab");
    const QCommandLineOption panel_left_option("panel-left", "Dock the settings panel on the left.");
    const QCommandLineOption scene_selftest_option("scene-selftest",
        "Load the positional models into one plate, place them apart, slice and export: CONFIG,GCODE.",
        "CONFIG,GCODE");
    const QCommandLineOption selftest_option("selftest-slice", "Slice through SliceController, report the result, exit.");
    const QCommandLineOption project_test_option("project-test",
        "Save a plate to a project file, reload it into a cleared document, and slice the reloaded plate; CONFIG is a resolved config.",
        "CONFIG");
    const QCommandLineOption slice_features_option("slice-features",
        "Slice one model with a per-object layer height, a layer height profile and painted facets, and report what each changed; CONFIG is a resolved config.",
        "CONFIG");
    const QCommandLineOption volume_settings_option("volume-settings-test",
        "Give a modifier its own infill and check the slice follows it; CONFIG is a resolved "
        "config.", "CONFIG");
    const QCommandLineOption print_host_test_option("print-host-test",
        "Test the print-host connection and upload a G-code file through the engine's own "
        "uploader: HOST,PORT,APIKEY,GCODE. The port may be empty.", "spec");
    const QCommandLineOption print_host_start_option("print-host-start",
        "The same upload, and the machine starts the job as it lands: HOST,PORT,APIKEY,GCODE. "
        "The port may be empty.", "spec");
    const QCommandLineOption calibration_test_option("calibration-test",
        "Build every calibration test on a plate, slice the towers and report the band commands "
        "that reached the G-code; CONFIG is a resolved config.", "CONFIG");
    const QCommandLineOption update_option("update",
        "Check the release channel and, with --update-install, download, verify and install the "
        "newest release into the install prefix: BASE_URL. Exits 0 when an update is available "
        "(or installed).", "base_url");
    const QCommandLineOption update_install_option("update-install",
        "Install the release --update found. The prefix comes from NEXT_LAYER_UPDATE_ROOT, and "
        "defaults to the running executable's directory.");
    const QCommandLineOption mesh_tools_option("mesh-tools-test",
        "Measure a cube, hollow it, then merge two boxes and split the result back into two "
        "objects; CONFIG is a resolved config.", "CONFIG");
    const QCommandLineOption multi_tool_option("multi-tool-test",
        "Paint the top of a box with tool 2, slice it on a tool changer and report the tool "
        "changes and colours; CONFIG is a resolved config of a printer with two or more tools.",
        "CONFIG");
    const QCommandLineOption shape_test_option("shape-test",
        "Add a primitive to the plate, cut it with a negative volume, remove the volume and add a second shape, slicing between each step; CONFIG is a resolved config.",
        "CONFIG");
    const QCommandLineOption plate_tools_option("plate-tools",
        "Exercise arrange, auto-orient and place-on-face headlessly; CONFIG is a resolved config. Exits non-zero if any tool fails.",
        "CONFIG");
    const QCommandLineOption keep_gcode_option("keep-gcode",
        "With --selftest-slice, copy the G-code there so two slice paths can be compared byte for byte.",
        "GCODE");
    const QCommandLineOption preview_stats_option("preview-stats",
        "Read GCODE with the engine's G-code processor, write per-line-type statistics to OUT (JSON), exit.",
        "GCODE,OUT");
    const QCommandLineOption vgcode_shot_option("vgcode-shot",
        "Render GCODE with libvgcode, the engine's own preview renderer, to OUT (PNG), exit.",
        "GCODE,OUT");
    const QCommandLineOption dump_fonts_option("dump-fonts",
        "List the fonts the emboss text tool can open, and prove one opens with its face "
        "metrics. Prints a count line first so a harness reads a figure, not an exit code.");
    const QCommandLineOption dump_object_settings_option("dump-object-settings",
        "List the settings a single object can override, with their types, and exit.");
    const QCommandLineOption dump_presets_option("dump-presets",
        "List the engine's printer profiles and exit. With --printer, also list its print and filament profiles.");
    const QCommandLineOption dump_presets_json_option("dump-presets-json",
        "With --dump-presets, print the profile list as JSON on stdout and nothing else.");
    const QCommandLineOption preset_printer_option("printer", "Printer profile to select (with --process and --filament, resolves a full config).", "name");
    const QCommandLineOption preset_process_option("process", "Print (process) profile to select.", "name");
    const QCommandLineOption preset_filament_option("filament", "Filament (material) profile to select.", "name");
    const QCommandLineOption preset_filaments_option("filaments",
        "One material per tool, comma separated (tool 1 first). Overrides --filament and sets "
        "the extruder count the slice sees.", "names");
    const QCommandLineOption set_option("set",
        "Set a setting before acting, NAME=VALUE (repeatable).", "name=value");
    const QCommandLineOption add_printer_option("add-printer",
        "Create a custom printer from the current settings: NAME,WIDTH,DEPTH,HEIGHT.", "spec");
    const QCommandLineOption save_preset_option("save-preset",
        "Save the tab's changed values as a user preset: KIND,NAME.", "kind,name");
    const QCommandLineOption rename_preset_option("rename-preset",
        "Rename a user preset: KIND,OLD,NEW.", "kind,old,new");
    const QCommandLineOption delete_preset_option("delete-preset",
        "Delete a user preset: KIND,NAME.", "kind,name");
    const QCommandLineOption export_preset_option("export-preset",
        "Write a user preset's own file to PATH: KIND,NAME,PATH.", "kind,name,path");
    const QCommandLineOption import_preset_option("import-preset",
        "Read a shared preset file into the user presets: KIND,PATH. Prints the name it "
        "landed under.", "kind,path");
    parser.addOptions({qml_option, smoke_option, shot_option, slice_option, preview_option, dump_config_option,
                       dump_tabs_option, dump_names_option, dump_subtab_option, transform_option, hide_feature_option,
                       view_type_option, hide_option_option,
                       single_layer_option, layer_option, layer_from_option, moves_option, view_option, camera_view_option,
                       ortho_option, plates_option,
                       settings_tab_option, paint_test_option, input_test_option, slider_test_option,
                       preset_dialog_option,
                       panel_left_option, selftest_option, plate_tools_option, slice_features_option,
                       project_test_option, mesh_tools_option, update_option,
                       update_install_option, multi_tool_option, print_host_test_option, print_host_start_option, volume_settings_option, calibration_test_option, shape_test_option, keep_gcode_option, scene_selftest_option, preview_stats_option, vgcode_shot_option,
                       dump_presets_option, dump_presets_json_option, dump_fonts_option, dump_object_settings_option, preset_printer_option, preset_process_option, preset_filament_option, preset_filaments_option,
                       set_option, add_printer_option, save_preset_option, rename_preset_option,
                       delete_preset_option, export_preset_option, import_preset_option});
    parser.process(app);

    const QStringList files = parser.positionalArguments();
    const QString modelArg = files.isEmpty() ? QString() : files.first();

    initEngine();

    if (parser.isSet(vgcode_shot_option)) {
        const QStringList parts = parser.value(vgcode_shot_option).split(',');
        if (parts.size() != 2) {
            std::fprintf(stderr, "vgcode-shot expects GCODE,OUT\n");
            return 1;
        }
        float yaw = 45.0f;
        float pitch = 20.0f;
        float zoom = 1.0f;
        if (parser.isSet(view_option)) {
            const QStringList view = parser.value(view_option).split(',');
            if (view.size() == 3) {
                yaw = view.at(0).toFloat();
                pitch = view.at(1).toFloat();
                zoom = view.at(2).toFloat();
            }
        }
        if (!renderPreviewWithVgcode(parts.at(0), parts.at(1), yaw, pitch, zoom)) {
            std::fprintf(stderr, "vgcode-shot: failed for %s\n", parts.at(0).toUtf8().constData());
            return 1;
        }
        std::fprintf(stdout, "vgcode-shot: wrote %s\n", parts.at(1).toUtf8().constData());
        return 0;
    }

    if (parser.isSet(preview_stats_option)) {
        const QStringList parts = parser.value(preview_stats_option).split(',');
        if (parts.size() != 2) {
            std::fprintf(stderr, "preview-stats expects GCODE,OUT\n");
            return 1;
        }
        const PreviewData data = PreviewReader::read(parts.at(0));
        if (!data.valid()) {
            std::fprintf(stderr, "preview-stats: no extrusion moves in %s\n", parts.at(0).toUtf8().constData());
            return 1;
        }
        QJsonObject root;
        root.insert(QStringLiteral("file"), parts.at(0));
        root.insert(QStringLiteral("layers"), data.layers.size());
        root.insert(QStringLiteral("moves"), data.moves.size());
        root.insert(QStringLiteral("seconds"), data.totalSeconds);
        root.insert(QStringLiteral("filamentMm"), data.totalFilamentMm);
        root.insert(QStringLiteral("filamentG"), data.totalFilamentG);
        root.insert(QStringLiteral("cost"), data.totalCost);
        float widthMin = 1e9f, widthMax = 0.0f, heightMin = 1e9f, heightMax = 0.0f;
        for (const PreviewMove& move : data.moves) {
            widthMin = std::min({ widthMin, move.width0, move.width1 });
            widthMax = std::max({ widthMax, move.width0, move.width1 });
            heightMin = std::min({ heightMin, move.height0, move.height1 });
            heightMax = std::max({ heightMax, move.height0, move.height1 });
        }
        root.insert(QStringLiteral("widthMin"), double(widthMin));
        root.insert(QStringLiteral("widthMax"), double(widthMax));
        root.insert(QStringLiteral("heightMin"), double(heightMin));
        root.insert(QStringLiteral("heightMax"), double(heightMax));
        QJsonArray layerZs;
        for (int index = 0; index < std::min<int>(5, data.layers.size()); ++index)
            layerZs.append(double(data.layers.at(index).z));
        root.insert(QStringLiteral("firstLayerZs"), layerZs);
        QJsonArray roles;
        for (const PreviewRole& role : data.roles) {
            if (role.moves == 0)
                continue;
            QJsonObject entry;
            entry.insert(QStringLiteral("name"), role.name);
            entry.insert(QStringLiteral("color"), role.color.name(QColor::HexRgb));
            entry.insert(QStringLiteral("moves"), role.moves);
            entry.insert(QStringLiteral("seconds"), role.seconds);
            entry.insert(QStringLiteral("lengthMm"), role.lengthMm);
            entry.insert(QStringLiteral("massG"), role.massG);
            roles.append(entry);
        }
        root.insert(QStringLiteral("roles"), roles);
        QFile out(parts.at(1));
        if (!out.open(QIODevice::WriteOnly)) {
            std::fprintf(stderr, "preview-stats: cannot write %s\n", parts.at(1).toUtf8().constData());
            return 1;
        }
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out.close();
        std::fprintf(stdout, "preview stats: %d layers, %d moves, %.1f s, %.2f m, %.2f g -> %s\n",
                     int(data.layers.size()), int(data.moves.size()), data.totalSeconds,
                     data.totalFilamentMm / 1000.0, data.totalFilamentG,
                     parts.at(1).toUtf8().constData());
        return 0;
    }

    if (parser.isSet(slice_option)) {
        if (modelArg.isEmpty()) {
            std::fprintf(stderr, "--slice needs a model\n");
            return 2;
        }
        const QStringList parts = parser.value(slice_option).split(',');
        if (parts.size() != 2) {
            std::fprintf(stderr, "--slice expects CONFIG,GCODE\n");
            return 2;
        }
        return sliceToFile(modelArg, parts.at(0), parts.at(1));
    }

    if (parser.isSet(transform_option)) {
        if (modelArg.isEmpty()) {
            std::fprintf(stderr, "--transform needs a model\n");
            return 2;
        }
        const QStringList parts = parser.value(transform_option).split(',');
        if (parts.size() < 2) {
            std::fprintf(stderr, "--transform expects SPEC,OUT\n");
            return 2;
        }
        const QString out = parts.last();
        const QString spec = QStringList(parts.mid(0, parts.size() - 1)).join(QLatin1Char(','));
        const bool ok = runTransform(modelArg.toStdString(), out.toStdString(),
                                     [spec](Slic3r::App::InitParams& params) {
                                         applyTransformSpec(spec, params);
                                     });
        std::fprintf(stdout, "transform %s -> %s\n", ok ? "ok" : "failed", out.toUtf8().constData());
        return ok ? 0 : 1;
    }

    if (parser.isSet(project_test_option)) {
        if (files.isEmpty()) {
            std::fprintf(stderr, "--project-test needs a model\n");
            return 2;
        }
        return next_layer::runProjectSelftest(files.first(), parser.value(project_test_option),
                                              QString::fromStdString(dataDir()));
    }

    if (parser.isSet(shape_test_option)) {
        return next_layer::runShapeSelftest(parser.value(shape_test_option),
                                            QString::fromStdString(dataDir()));
    }

    if (parser.isSet(slice_features_option)) {
        if (files.isEmpty()) {
            std::fprintf(stderr, "--slice-features needs a model\n");
            return 2;
        }
        return next_layer::runSliceFeaturesSelftest(files.first(),
                                                    parser.value(slice_features_option),
                                                    QString::fromStdString(dataDir()));
    }

    if (parser.isSet(volume_settings_option))
        return next_layer::runVolumeSettingsSelftest(parser.value(volume_settings_option),
                                                   QString::fromStdString(dataDir()));

    if (parser.isSet(print_host_test_option) || parser.isSet(print_host_start_option)) {
        const bool startPrint = parser.isSet(print_host_start_option);
        const QStringList parts = (startPrint ? parser.value(print_host_start_option)
                                             : parser.value(print_host_test_option))
                                      .split(QLatin1Char(','));
        if (parts.size() != 4) {
            std::fprintf(stderr, "--print-host-%s expects HOST,PORT,APIKEY,GCODE\n",
                         startPrint ? "start" : "test");
            return 2;
        }
        next_layer::PrinterLink link;
        link.setHost(parts.at(0));
        link.setPort(parts.at(1));
        link.setApiKey(parts.at(2));
        link.setKind(QStringLiteral("moonraker"));
        link.save();
        const QString result = link.runHeadless(parts.at(3), startPrint);
        std::printf("print host: %s\n", result.toUtf8().constData());
        return result.contains(QLatin1String("sent ")) ? 0 : 1;
    }

    if (parser.isSet(calibration_test_option))
        return next_layer::runCalibrationSelftest(parser.value(calibration_test_option),
                                                 QString::fromStdString(dataDir()));

    if (parser.isSet(update_option)) {
        next_layer::Updater updater;
        next_layer::ReleaseInfo release;
        const bool newer = updater.checkSync(parser.value(update_option), release);
        if (!release.error.isEmpty()) {
            std::fprintf(stderr, "update: %s\n", release.error.toUtf8().constData());
            return 1;
        }
        std::printf("current %s, channel %s\n",
                    next_layer::Updater::currentVersion().toUtf8().constData(),
                    qPrintable(parser.value(update_option)));
        if (!newer) {
            std::printf("up to date\n");
            return 0;
        }
        std::printf("available %s: %s\n", release.version.toUtf8().constData(),
                    release.notes.toUtf8().constData());
        if (!parser.isSet(update_install_option))
            return 0;

        const QString root = next_layer::Updater::installRoot();
        std::printf("install root %s\n", qPrintable(root));
        QString error;
        if (!updater.downloadSync(release, root, error)) {
            std::fprintf(stderr, "update: %s\n", error.toUtf8().constData());
            return 1;
        }
        if (!updater.installSync(root, error)) {
            std::fprintf(stderr, "update: %s\n", error.toUtf8().constData());
            return 1;
        }
        std::printf("installed %s\n", release.version.toUtf8().constData());
        return 0;
    }

    if (parser.isSet(mesh_tools_option))
        return next_layer::runMeshToolsSelftest(parser.value(mesh_tools_option),
                                               QString::fromStdString(dataDir()));

    if (parser.isSet(multi_tool_option))
        return next_layer::runMultiToolSelftest(parser.value(multi_tool_option),
                                               QString::fromStdString(dataDir()));

    if (parser.isSet(plate_tools_option)) {
        return next_layer::runPlateToolsSelftest(files, parser.value(plate_tools_option),
                                                QString::fromStdString(dataDir()));
    }

    if (parser.isSet(scene_selftest_option)) {
        const QStringList parts = parser.value(scene_selftest_option).split(',');
        if (parts.size() != 2) {
            std::fprintf(stderr, "--scene-selftest expects CONFIG,GCODE\n");
            return 2;
        }
        if (files.size() < 2) {
            std::fprintf(stderr, "--scene-selftest needs at least two models\n");
            return 2;
        }
        return next_layer::runSceneSelftest(files, parts.at(0), parts.at(1),
                                            QString::fromStdString(dataDir()));
    }

    const PrinterSetup printer = readPrinterSetup();
    const QVector2D centre = bedCentre(printer.bed);

    next_layer::Palette palette;
    auto* settings = new SettingsModel;
    auto* presets = new PresetStore;
    presets->setSettings(settings);

    QObject::connect(presets, &PresetStore::resolved, settings, [settings, presets] {
        settings->seedFromConfigJson(QString::fromUtf8(presets->resolvedConfigJson()));
    });

    if (parser.isSet(dump_object_settings_option)) {
        int count = 0;
        for (const Slic3r::Domain::ConfigItemDef& def :
             Slic3r::Domain::get_defs_fdm().defs()) {
            if (def.overrides_in.count(Slic3r::Domain::FDMConfigLocation::Object) == 0)
                continue;
            std::string type = def.type == nullptr ? "?" : def.type->name();
            const std::string marker = "Slic3r::Domain::";
            const std::size_t at = type.find(marker);
            if (at != std::string::npos)
                type.erase(at, marker.size());
            std::printf("%s\t%s\t%s\n", def.name.c_str(),
                        def.full_label.empty() ? def.label.c_str() : def.full_label.c_str(),
                        type.c_str());
            ++count;
        }
        std::printf("object settings: %d\n", count);
        return 0;
    }

    if (parser.isSet(dump_fonts_option)) {
        next_layer::FontManager fonts;
        const Slic3r::Domain::FontList& openable = fonts.get_fonts();
        std::printf("fonts: %zu openable\n", openable.size());
        const Slic3r::Domain::FontList favorites = fonts.create_favorit();
        std::printf("favorites: %zu\n", favorites.size());
        if (openable.empty())
            return 1;

        const std::unique_ptr<const Slic3r::Domain::FontFile> first = fonts.open(openable.front());
        if (!first) {
            std::fprintf(stderr, "fonts: could not open %s\n", openable.front().path.c_str());
            return 1;
        }
        std::printf("opened: %s (%zu bytes, %zu face(s))\n", openable.front().name.c_str(),
                    first->data->size(), first->infos.size());
        const Slic3r::Domain::FontFile::Info& info = first->infos.front();
        std::printf("metrics: ascent %d descent %d linegap %d unit_per_em %d\n", info.ascent,
                    info.descent, info.linegap, info.unit_per_em);
        if (info.unit_per_em <= 0 || info.ascent <= 0)
            return 1;
        return 0;
    }

    if (parser.isSet(dump_presets_option)) {
        if (!presets->refreshPrintersSync()) {
            std::fprintf(stderr, "presets: could not list printer profiles\n");
            return 1;
        }
        // The column dump is for a human: many printer names are longer than the
        // 42 columns it pads to, so a harness that splits on whitespace cannot
        // recover them and reports failures that are its own parsing.
        if (parser.isSet(dump_presets_json_option)) {
            const QJsonDocument document = QJsonDocument::fromVariant(presets->printers());
            const QByteArray json = document.toJson(QJsonDocument::Compact);
            std::fwrite(json.constData(), 1, static_cast<size_t>(json.size()), stdout);
            std::fputc('\n', stdout);
            return 0;
        }
        for (const QVariant& value : presets->printers()) {
            const QVariantMap printer = value.toMap();
            std::printf("%-42s %-18s %gx%g h=%g (%d extruder%s)\n",
                        printer.value(QStringLiteral("name")).toString().toUtf8().constData(),
                        printer.value(QStringLiteral("vendor")).toString().toUtf8().constData(),
                        printer.value(QStringLiteral("width")).toDouble(),
                        printer.value(QStringLiteral("height")).toDouble(),
                        printer.value(QStringLiteral("maxPrintHeight")).toDouble(),
                        printer.value(QStringLiteral("extruders")).toInt(),
                        printer.value(QStringLiteral("extruders")).toInt() == 1 ? "" : "s");
        }
        if (parser.isSet(preset_printer_option)) {
            const QString name = parser.value(preset_printer_option);
            if (!presets->selectPrinterSync(name)) {
                std::fprintf(stderr, "presets: no profiles for %s\n", name.toUtf8().constData());
                return 1;
            }
            std::printf("\nprint profiles for %s:\n", name.toUtf8().constData());
            for (const QVariant& value : presets->printProfiles())
                std::printf("  %s\n", value.toString().toUtf8().constData());
            std::printf("filament profiles: %lld\n",
                        static_cast<long long>(presets->filamentProfiles().size()));
            for (const QVariant& value : presets->filamentProfiles())
                std::printf("  %s\n", value.toString().toUtf8().constData());
        }
        return 0;
    }

    if (parser.isSet(preset_filaments_option)) {
        QStringList names;
        for (const QString& name : parser.value(preset_filaments_option).split(QLatin1Char(',')))
            names.append(name.trimmed());
        presets->setFilaments(names);
    }

    const bool preset_action = parser.isSet(add_printer_option) || parser.isSet(save_preset_option)
                            || parser.isSet(rename_preset_option) || parser.isSet(delete_preset_option)
                            || parser.isSet(export_preset_option) || parser.isSet(import_preset_option);

    // --set applies to whatever the settings model currently holds, so the
    // preset actions below and the plain resolve/dump path both honour it.
    const auto applySets = [&parser, &set_option, settings] {
        for (const QString& assignment : parser.values(set_option)) {
            const int equals = assignment.indexOf(QLatin1Char('='));
            if (equals <= 0)
                return false;
            settings->setValue(assignment.left(equals), assignment.mid(equals + 1));
        }
        return true;
    };

    if (!preset_action && parser.isSet(preset_printer_option) && parser.isSet(preset_process_option)
        && parser.isSet(preset_filament_option)) {
        const QString printer = parser.value(preset_printer_option);
        const QString process = parser.value(preset_process_option);
        const QString filament = parser.value(preset_filament_option);
        if (!presets->resolveSync(printer, process, filament)) {
            std::fprintf(stderr, "presets: could not resolve %s / %s / %s\n",
                         printer.toUtf8().constData(), process.toUtf8().constData(),
                         filament.toUtf8().constData());
            return 1;
        }
        settings->seedFromConfigJson(QString::fromUtf8(presets->resolvedConfigJson()));
        if (!applySets())
            return 2;
        std::printf("resolved %s / %s / %s\n", printer.toUtf8().constData(),
                    process.toUtf8().constData(), filament.toUtf8().constData());
        std::printf("  model=%s bed=%lld pts h=%g\n",
                    presets->resolvedPrinterModel().toUtf8().constData(),
                    static_cast<long long>(presets->resolvedBedShape().size()),
                    presets->resolvedMaxPrintHeight());
        if (parser.isSet(dump_config_option)) {
            QFile out(parser.value(dump_config_option));
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                std::fprintf(stderr, "presets: cannot write %s\n",
                             parser.value(dump_config_option).toUtf8().constData());
                return 1;
            }
            out.write(settings->configJson().toUtf8());
            std::fprintf(stdout, "config written to %s\n",
                         parser.value(dump_config_option).toUtf8().constData());
        }
        // A settings dump of the resolved preset is a legitimate combination, so
        // only leave here when no dump was asked for.
        if (!parser.isSet(dump_subtab_option) && !parser.isSet(dump_tabs_option))
            return 0;
    }

    if (preset_action) {
        if (!presets->refreshPrintersSync()) {            std::fprintf(stderr, "presets: could not list printer profiles\n");
            return 1;
        }

        const QString printer = parser.value(preset_printer_option);
        if (!printer.isEmpty()) {
            const QString process = parser.value(preset_process_option);
            const QString filament = parser.value(preset_filament_option);
            const bool resolved = !process.isEmpty() && !filament.isEmpty()
                               && presets->resolveSync(printer, process, filament);
            if (resolved)
                settings->seedFromConfigJson(QString::fromUtf8(presets->resolvedConfigJson()));
            else if (!presets->selectPrinterSync(printer)) {
                std::fprintf(stderr, "presets: no profiles for %s\n", printer.toUtf8().constData());
                return 1;
            }
        }

        if (!applySets())
            return 2;

        QString error;
        if (parser.isSet(add_printer_option)) {
            const QStringList parts = parser.value(add_printer_option).split(QLatin1Char(','));
            if (parts.size() != 4) {
                std::fprintf(stderr, "--add-printer expects NAME,WIDTH,DEPTH,HEIGHT\n");
                return 2;
            }
            error = presets->createPrinter(parts.at(0), parts.at(1).toDouble(),
                                           parts.at(2).toDouble(), parts.at(3).toDouble());
            if (error.isEmpty()
                && !presets->reloadSync(parts.at(0), QStringLiteral("Standard"),
                                        QStringLiteral("Generic")))
                error = presets->error();
        } else if (parser.isSet(save_preset_option)) {
            const QStringList parts = parser.value(save_preset_option).split(QLatin1Char(','));
            if (parts.size() != 2) {
                std::fprintf(stderr, "--save-preset expects KIND,NAME\n");
                return 2;
            }
            const bool filamentKind = parts.at(0) == QLatin1String("filament");
            const QString tab = filamentKind ? QStringLiteral("filament") : QStringLiteral("process");
            error = presets->saveUserPreset(parts.at(0), parts.at(1), tab);
            if (error.isEmpty()) {
                const QString nextPrinter = printer.isEmpty() ? presets->currentPrinter() : printer;
                const QString nextPrint = filamentKind ? presets->currentPrint() : parts.at(1);
                const QString nextFilament = filamentKind ? parts.at(1) : presets->currentFilament();
                if (!presets->reloadSync(nextPrinter, nextPrint, nextFilament))
                    error = presets->error();
            }
        } else if (parser.isSet(export_preset_option)) {
            const QStringList parts = parser.value(export_preset_option).split(QLatin1Char(','));
            if (parts.size() != 3) {
                std::fprintf(stderr, "--export-preset expects KIND,NAME,PATH\n");
                return 2;
            }
            error = presets->exportPreset(parts.at(0), parts.at(1), parts.at(2));
            if (error.isEmpty())
                std::printf("exported %s to %s\n", parts.at(1).toUtf8().constData(),
                            parts.at(2).toUtf8().constData());
        } else if (parser.isSet(import_preset_option)) {
            const QStringList parts = parser.value(import_preset_option).split(QLatin1Char(','));
            if (parts.size() != 2) {
                std::fprintf(stderr, "--import-preset expects KIND,PATH\n");
                return 2;
            }
            error = presets->importPreset(parts.at(0), parts.at(1));
            if (error.isEmpty())
                std::printf("imported %s\n",
                            presets->importedPresetName().toUtf8().constData());
        } else if (parser.isSet(rename_preset_option)) {
            const QStringList parts = parser.value(rename_preset_option).split(QLatin1Char(','));
            if (parts.size() != 3) {
                std::fprintf(stderr, "--rename-preset expects KIND,OLD,NEW\n");
                return 2;
            }
            error = presets->renameUserPreset(parts.at(0), parts.at(1), parts.at(2));
        } else {
            const QStringList parts = parser.value(delete_preset_option).split(QLatin1Char(','));
            if (parts.size() != 2) {
                std::fprintf(stderr, "--delete-preset expects KIND,NAME\n");
                return 2;
            }
            error = presets->deleteUserPreset(parts.at(0), parts.at(1));
        }

        if (!error.isEmpty()) {
            std::fprintf(stderr, "presets: %s\n", error.toUtf8().constData());
            return 1;
        }
        std::fprintf(stdout, "presets: ok\n");
        if (!parser.isSet(dump_config_option))
            return 0;
    }

    if (parser.isSet(dump_config_option)) {
        QFile out(parser.value(dump_config_option));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return 1;
        out.write(settings->configJson().toUtf8());
        std::fprintf(stdout, "config written to %s\n", parser.value(dump_config_option).toUtf8().constData());
        return 0;
    }

    if (parser.isSet(dump_tabs_option)) {
        std::printf("%s\n", settings->tabDump().toUtf8().constData());
        return 0;
    }

    if (parser.isSet(dump_names_option)) {
        std::printf("%s", settings->nameDump().toUtf8().constData());
        return 0;
    }

    if (parser.isSet(dump_subtab_option)) {
        const QStringList parts = parser.value(dump_subtab_option).split('/');
        if (parts.size() != 2) {
            std::fprintf(stderr, "--dump-subtab expects TAB/SUBTAB, e.g. process/100\n");
            return 2;
        }
        const QVariantList groups = settings->groups(parts.at(0), parts.at(1), QString());
        if (groups.isEmpty()) {
            std::fprintf(stderr, "no settings for %s\n", parser.value(dump_subtab_option).toUtf8().constData());
            return 1;
        }
        for (const QVariant& groupValue : groups) {
            const QVariantMap group = groupValue.toMap();
            std::printf("[%s]\n", group.value(QStringLiteral("title")).toString().toUtf8().constData());
            for (const QVariant& itemValue : group.value(QStringLiteral("items")).toList()) {
                const QVariantMap item = itemValue.toMap();
                std::printf("  %-44s %-6s %s\n",
                            item.value(QStringLiteral("name")).toString().toUtf8().constData(),
                            item.value(QStringLiteral("type")).toString().toUtf8().constData(),
                            settingsValueText(item.value(QStringLiteral("value"))).toUtf8().constData());
            }
        }
        return 0;
    }

    auto* geometry = new MeshGeometry;
    auto* bedPlate = new MeshGeometry;
    const bool hasPlate = loadPlateGeometry(*bedPlate, bedPlatePath(printer.model), centre);
    next_layer::Host host(palette, toVariantBed(printer.bed), printer.maxHeight,
                          QString::fromLatin1(kBaseConfigPath),
                          QVariantList{centre.x(), 0.0, -centre.y()},
                          bedRadius(printer.bed), printer.model,
                          bedExtent(printer.bed, false), bedExtent(printer.bed, true),
                          hasPlate);

    QObject::connect(presets, &PresetStore::resolved, &host, [presets, &host, bedPlate] {
        const QVector<QPointF> bed = bedFromVariant(presets->resolvedBedShape());
        if (bed.size() < 3)
            return;
        const QVector2D centre2 = bedCentre(bed);
        host.setPrinter(presets->resolvedBedShape(), presets->resolvedMaxPrintHeight(),
                        presets->resolvedPrinterModel(),
                        bedExtent(bed, false), bedExtent(bed, true),
                        bedRadius(bed), QVariantList{centre2.x(), 0.0, -centre2.y()});

        const QString plate = bedPlatePath(presets->resolvedPrinterModel());
        host.setBedPlateLoaded(!plate.isEmpty()
                               && loadPlateGeometry(*bedPlate, plate, centre2));
    });

    // The bed outline and height are ordinary printer settings, so editing them
    // in the panel or the preset dialog has to move the plate straight away
    // instead of waiting for the next preset resolve.
    QObject::connect(settings, &SettingsModel::changed, &host, [settings, &host] {
        const QVector<QPointF> bed = bedFromVariant(settings->value(QStringLiteral("bed_shape")).toList());
        if (bed.size() < 3)
            return;
        const double maxHeight = settings->value(QStringLiteral("max_print_height")).toDouble();
        if (host.bedShape() == toVariantBed(bed) && qFuzzyCompare(host.maxPrintHeight(), maxHeight))
            return;
        const QVector2D centre2 = bedCentre(bed);
        host.setPrinter(toVariantBed(bed), maxHeight, host.printerModel(),
                        bedExtent(bed, false), bedExtent(bed, true),
                        bedRadius(bed), QVariantList{centre2.x(), 0.0, -centre2.y()});
    });

    auto* slicer = new SliceController(std::string(), settings);
    auto* toolpath = new Toolpath(palette.roles());
    const QVector3D bedLow = bedExtent(printer.bed, false);
    const QVector3D bedHigh = bedExtent(printer.bed, true);
    auto* document = new PlateDocument(centre, QVector2D(bedHigh.x() - bedLow.x(), bedHigh.y() - bedLow.y()));
    slicer->setDocument(document);
    auto* scene = new PlateScene(document);
    auto* session = new Session(geometry, &host, slicer, document);
    auto* transforms = new TransformController(std::string());

    QObject::connect(settings, &SettingsModel::changed, document, [settings, document] {
        const QVector<QPointF> bed = bedFromVariant(settings->value(QStringLiteral("bed_shape")).toList());
        if (bed.size() < 3)
            return;
        const QVector3D low = bedExtent(bed, false);
        const QVector3D high = bedExtent(bed, true);
        document->setBed(bedCentre(bed), QVector2D(high.x() - low.x(), high.y() - low.y()));
    });

    // A different machine is a different bed, and the plate's objects are stored in
    // bed coordinates: without this an object placed for a 350 mm bed sits off the
    // edge of a 235 mm one after the setup flow switches machine.
    auto resolvedModel = std::make_shared<QString>(printer.model);
    // A tool changer paints by tool, so the palette comes from the printer's own
    // filament colours: what the overlay shows is what the machine prints.
    QObject::connect(presets, &PresetStore::resolved, document, [settings, document] {
        const QVariant value = settings->value(QStringLiteral("filament_colour"));
        QVariantList colours;
        const QVariantList entries = value.toList();
        for (const QVariant& entry : entries) {
            const QString colour = entry.toString().trimmed();
            if (!colour.isEmpty())
                colours.append(colour);
        }
        if (colours.isEmpty()) {
            colours = QVariantList{QStringLiteral("#e8583c"), QStringLiteral("#3c8ce8"),
                                   QStringLiteral("#3ce8a0"), QStringLiteral("#e8d53c")};
        }
        document->setToolColours(colours);
    });

    QObject::connect(presets, &PresetStore::resolved, document,
                     [presets, document, resolvedModel] { 
        if (presets->resolvedPrinterModel() == *resolvedModel)
            return;
        *resolvedModel = presets->resolvedPrinterModel();
        const QVariantList objects = document->objects();
        for (const QVariant& value : objects)
            document->centreObject(value.toMap().value(QStringLiteral("id")).toInt());
    });

    if (!files.isEmpty()) {
        if (files.size() == 1 ? !session->open(files.first()) : session->addModels(files) != files.size()) {
            std::fprintf(stderr, "load failed: %s\n", session->error().toUtf8().constData());
            return 1;
        }
        const QVector3D low = geometry->boundsMin();
        const QVector3D high = geometry->boundsMax();
        std::fprintf(stdout, "mesh %s, bounds (%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f) mm, bed %lld points centred (%.1f,%.1f)\n",
                     session->displayName().toUtf8().constData(),
                     low.x(), low.y(), low.z(), high.x(), high.y(), high.z(),
                     static_cast<long long>(printer.bed.size()), centre.x(), centre.y());
    } else {
        std::fprintf(stdout, "no model, empty bed %lld points centred (%.1f,%.1f)\n", 
                     static_cast<long long>(printer.bed.size()), centre.x(), centre.y());
    }

    if (parser.isSet(paint_test_option)) {
        const double radius = qMax(0.5, parser.value(paint_test_option).toDouble());
        int painted = 0;
        for (const QVariant& value : document->objects()) {
            const int id = value.toMap().value(QStringLiteral("id")).toInt();
            const PlateDocument::Object* object = document->object(id);
            if (object == nullptr || object->mesh == nullptr)
                continue;
            const Slic3r::Domain::Transform3d trafo = object->trafo.get_matrix();
            for (size_t facet = 0; facet < object->mesh->its.indices.size(); ++facet) {
                const auto& face = object->mesh->its.indices[facet];
                const Slic3r::Domain::Vec3d a =
                    object->mesh->its.vertices[face[0]].cast<double>();
                const Slic3r::Domain::Vec3d b =
                    object->mesh->its.vertices[face[1]].cast<double>();
                const Slic3r::Domain::Vec3d c =
                    object->mesh->its.vertices[face[2]].cast<double>();
                const Slic3r::Domain::Vec3d normal =
                    (trafo.linear() * (b - a).cross(c - a)).normalized();
                const Slic3r::Domain::Vec3d hit = trafo * ((a + b + c) / 3.0);
                if (normal.z() > -0.9 || hit.z() < 1.0)
                    continue;
                painted += document->paintObject(id, QStringLiteral("supports"),
                                                 QVector3D(hit.x(), hit.y(), hit.z()), radius,
                                                 false, 2, static_cast<int>(facet));
            }
            document->selectObject(id);
        }
        std::fprintf(stdout, "painted %d downward facet group(s) on %d object(s)\n",
                     painted, document->count());
    }

    QString startupGcodePath;
    int startupLayer = -1;
    int startupLayerFrom = 0;
    int startupMovesMoveFrom = 0;
    int startupMovesTo = -1;
    bool startupSingleLayer = false;
    QVariantList startupHiddenFeatures;
    QVariantList startupHiddenOptions;
    QString startupViewType;
    if (parser.isSet(preview_option)) {
        startupGcodePath = parser.value(preview_option);
        if (parser.isSet(layer_option))
            startupLayer = parser.value(layer_option).toInt() - 1;
        if (parser.isSet(layer_from_option))
            startupLayerFrom = std::max(0, parser.value(layer_from_option).toInt() - 1);
        startupSingleLayer = parser.isSet(single_layer_option);
        startupMovesMoveFrom = parser.value(moves_option).section(',', 0, 0).toInt();
        startupMovesTo = parser.value(moves_option).section(',', 1, 1).toInt();
        for (const QString& name : parser.value(hide_feature_option).split(',', Qt::SkipEmptyParts))
            startupHiddenFeatures.append(name.trimmed());
        startupViewType = parser.value(view_type_option);
        for (const QString& name : parser.value(hide_option_option).split(',', Qt::SkipEmptyParts))
            startupHiddenOptions.append(name.trimmed());
    }

    if (parser.isSet(selftest_option)) {
        QEventLoop loop;
        int exit_code = 1;
        QObject::connect(slicer, &SliceController::finished, &loop, [&] {
            std::fprintf(stdout, "slice finished in %.0f ms: %d layers, %d moves -> %s\n",
                         slicer->elapsedMs(), slicer->layerCount(), slicer->moveCount(),
                         slicer->gcodePath().toUtf8().constData());
            std::fprintf(stdout, "toolpath sha256 %s\n",
                         qPrintable(toolpathDigest(slicer->gcodePath())));
            const QString keep = parser.value(keep_gcode_option);
            if (!keep.isEmpty()) {
                QFile::remove(keep);
                if (!QFile::copy(slicer->gcodePath(), keep)) {
                    std::fprintf(stderr, "could not copy the G-code to %s\n", qPrintable(keep));
                    loop.quit();
                    return;
                }
            }
            exit_code = PreviewReader::read(slicer->gcodePath()).valid() ? 0 : 1;
            loop.quit();
        });
        QObject::connect(slicer, &SliceController::failed, &loop, [&](const QString& message) {
            std::fprintf(stderr, "slice failed: %s\n", message.toUtf8().constData());
            loop.quit();
        });
        slicer->slice();
        loop.exec();
        next_layer::SliceRuntime::shutdown();
        return exit_code;
    }

    QObject::connect(session, &Session::modelChanged, transforms, [session, transforms] {
        transforms->setModelPath(session->path().toStdString());
    });
    transforms->setModelPath(session->path().toStdString());
    QObject::connect(transforms, &TransformController::applied, session,
                     [session](const QString& path, const QString&) {
                         session->openTransformed(path, session->displayName(), false);
                     });

    qmlRegisterSingletonInstance("NextLayer", 1, 0, "Host", &host);
    qmlRegisterType<GcodeView>("NextLayer", 1, 0, "GcodeView");

    qAddPostRoutine([] { next_layer::SliceRuntime::shutdown(); });

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &engine,
                     [](const QList<QQmlError>& warnings) {
                         for (const QQmlError& warning : warnings)
                             std::fprintf(stdout, "qml: %s\n",
                                          warning.toString().toUtf8().constData());
                     });
    engine.rootContext()->setContextProperty("meshGeometry", geometry);
    engine.rootContext()->setContextProperty("plateDocument", document);
    engine.rootContext()->setContextProperty("calibration",
                                             new next_layer::Calibration(document, &engine));
    engine.rootContext()->setContextProperty("plateScene", scene);
    engine.rootContext()->setContextProperty("bedPlateGeometry", bedPlate);
    engine.rootContext()->setContextProperty("settingsModel", settings);
    engine.rootContext()->setContextProperty("presetStore", presets);
    auto* printerLink = new next_layer::PrinterLink(&engine);
    printerLink->load();
    engine.rootContext()->setContextProperty("printerLink", printerLink);
    auto* updater = new next_layer::Updater(&engine);
    engine.rootContext()->setContextProperty("updater", updater);
    engine.rootContext()->setContextProperty("sliceController", slicer);
    engine.rootContext()->setContextProperty("toolpath", toolpath);
    engine.rootContext()->setContextProperty("appVersionString", next_layer::applicationVersion());
    engine.rootContext()->setContextProperty(
        "engineVersionString",
        QStringLiteral("%1 %2").arg(QString::fromStdString(Slic3r::APP_NAME),
                                    QString::fromStdString(Slic3r::VERSION)));
    engine.rootContext()->setContextProperty("dataDirString", QString::fromStdString(dataDir()));
    engine.rootContext()->setContextProperty("presetDirString",
                                             QString::fromStdString(dataDir() + "/presets"));
    engine.rootContext()->setContextProperty("startupGcodePath", startupGcodePath);
    engine.rootContext()->setContextProperty("startupLayer", startupLayer);
    engine.rootContext()->setContextProperty("startupLayerFrom", startupLayerFrom);
    engine.rootContext()->setContextProperty("startupMovesFrom", startupMovesMoveFrom);
    engine.rootContext()->setContextProperty("startupMovesTo", startupMovesTo);
    engine.rootContext()->setContextProperty("startupCameraView", parser.value(camera_view_option));
    engine.rootContext()->setContextProperty("startupOrthographic", parser.isSet(ortho_option));
    engine.rootContext()->setContextProperty("startupSingleLayer", startupSingleLayer);
    engine.rootContext()->setContextProperty("startupHiddenFeatures", startupHiddenFeatures);
    engine.rootContext()->setContextProperty("startupHiddenOptions", startupHiddenOptions);
    engine.rootContext()->setContextProperty("startupViewType", startupViewType);
    engine.rootContext()->setContextProperty("session", session);
    engine.rootContext()->setContextProperty("transformController", transforms);
    engine.rootContext()->setContextProperty("startupSettingsTab", parser.value(settings_tab_option));
    engine.rootContext()->setContextProperty("startupPresetDialog", parser.value(preset_dialog_option));
    engine.rootContext()->setContextProperty("startupPanelLeft", parser.isSet(panel_left_option));
    engine.rootContext()->setContextProperty("startupPlates",
                                             parser.isSet(plates_option) ? parser.value(plates_option).toInt() : 1);
    const QString entry = qmlEntry(parser.value(qml_option));
    std::fprintf(stdout, "qml entry: %s\n", entry.toUtf8().constData());
    engine.load(QUrl::fromLocalFile(entry));
    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "QML failed to load\n");
        return 1;
    }

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (window == nullptr) {
        std::fprintf(stderr, "root object is not a window\n");
        return 1;
    }

    auto* closeGuard = new CloseGuard(window);
    window->installEventFilter(closeGuard);

    if (parser.isSet(view_option)) {
        const QStringList parts = parser.value(view_option).split(',');
        if (parts.size() != 3) {
            std::fprintf(stderr, "view expects YAW,PITCH,ZOOM\n");
            return 1;
        }
        if (auto* viewport = window->findChild<QObject*>(QStringLiteral("viewport"))) {
            viewport->setProperty("yaw", parts.at(0).toDouble());
            viewport->setProperty("pitch", parts.at(1).toDouble());
            viewport->setProperty("zoom", parts.at(2).toDouble());
        }
    }

    if (parser.isSet(input_test_option)) {
        QTimer::singleShot(1500, &app, [&] { runInputTest(window, geometry); });
        QTimer::singleShot(3200, &app, [&] { runDragTest(window, geometry); });
    }

    if (parser.isSet(slider_test_option)) {
        QTimer::singleShot(3000, &app, [&] {
            runLayerSliderTest(window);
            if (!parser.isSet(smoke_option))
                app.exit(0);
        });
    }

    if (!parser.isSet(smoke_option) && !parser.isSet(input_test_option)
        && !parser.isSet(slider_test_option))
        return app.exec();

    if (parser.isSet(smoke_option)) {
        const bool expect_preview = parser.isSet(preview_option);
        auto* gcodeView = window->findChild<GcodeView*>(QString(), Qt::FindChildrenRecursively);
        const qint64 settleMs = (parser.isSet(input_test_option) || parser.isSet(slider_test_option))
                                ? 3500 : 2500;
        const qint64 deadlineMs = expect_preview ? 12000 : settleMs;

        auto* poll = new QTimer(&app);
        poll->setInterval(200);
        auto* clock = new QElapsedTimer;
        clock->start();
        QObject::connect(poll, &QTimer::timeout, &app,
                         [&app, &parser, window, poll, clock, expect_preview, gcodeView, settleMs, deadlineMs,
                          &shot_option] {
            const qint64 elapsed = clock->elapsed();
            const bool ready = !expect_preview
                || (gcodeView != nullptr && gcodeView->initialized() && gcodeView->frameCount() > 0);
            if (elapsed < settleMs || (!ready && elapsed < deadlineMs))
                return;

            poll->stop();
            delete clock;
            poll->deleteLater();

            const QImage frame = window->grabWindow();
            if (parser.isSet(shot_option))
                frame.save(parser.value(shot_option));
            const int lit = countNonBackground(frame);
            const int total = frame.width() * frame.height();
            const double coverage = total > 0 ? 100.0 * lit / total : 0.0;
            std::fprintf(stdout, "frame %dx%d, covered %.2f%%\n", frame.width(), frame.height(), coverage);

            bool ok = lit > 0 && coverage < 99.0;
            if (expect_preview) {
                if (gcodeView == nullptr) {
                    std::fprintf(stderr, "smoke: no GcodeView item in the scene\n");
                    ok = false;
                } else if (!gcodeView->initialized()) {
                    std::fprintf(stderr, "smoke: GcodeView never initialized\n");
                    ok = false;
                } else if (gcodeView->frameCount() == 0) {
                    std::fprintf(stderr, "smoke: GcodeView rendered no frame\n");
                    ok = false;
                } else {
                    std::fprintf(stdout, "gcode view: %d frames\n", gcodeView->frameCount());
                }
            }
            app.exit(ok ? 0 : 1);
        });
        poll->start();
    }

    return app.exec();
}
