// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "TransformController.hpp"

#include <Slic3r/App/CLI/CLIApp.hpp>
#include <Slic3r/App/Init.hpp>
#include <Slic3r/Domain/Types.hpp>

#include <QFileInfo>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QString>

namespace {

QString jobPath(const QString& tag)
{
    static QTemporaryDir directory;
    return directory.filePath(QStringLiteral("transform-%1.stl").arg(tag));
}

}

bool runTransform(const std::string& modelPath, const std::string& outputPath,
                  const std::function<void(Slic3r::App::InitParams&)>& configure)
{
    Slic3r::App::InitParams params;
    params.input.input_files = {modelPath};
    params.action.export_stl = true;
    params.transform.dont_arrange = true;
    params.misc.output = outputPath;
    configure(params);
    const int code = Slic3r::App::CLI::run(params);
    const QFileInfo exported(QString::fromStdString(outputPath));
    return code == 0 && exported.exists() && exported.size() > 0;
}

TransformController::TransformController(std::string modelPath, QObject* parent)
    : QObject(parent)
    , m_modelPath(std::move(modelPath))
{
}

TransformController::~TransformController()
{
    if (m_worker.joinable())
        m_worker.join();
}

void TransformController::setModelPath(std::string modelPath)
{
    if (m_busy)
        return;
    m_modelPath = std::move(modelPath);
}

void TransformController::rotate(double x, double y, double z)
{
    run(QStringLiteral("rotate"), [x, y, z](Slic3r::App::InitParams& params) {
        Slic3r::Domain::Vec3d rotation;
        rotation[0] = x;
        rotation[1] = y;
        rotation[2] = z;
        params.transform.rotation = rotation;
        params.transform.ensure_on_bed = true;
    });
}

void TransformController::scale(double factor)
{
    run(QStringLiteral("scale"), [factor](Slic3r::App::InitParams& params) {
        params.transform.scale = Slic3r::Domain::FloatOrPercentage{factor};
        params.transform.ensure_on_bed = true;
    });
}

void TransformController::moveTo(double x, double y)
{
    run(QStringLiteral("move"), [x, y](Slic3r::App::InitParams& params) {
        Slic3r::Domain::Vec2d target;
        target[0] = x;
        target[1] = y;
        params.transform.align_xy = target;
        params.transform.dont_arrange = true;
    });
}

void TransformController::center()
{
    run(QStringLiteral("center"), [](Slic3r::App::InitParams& params) {
        params.transform.center = Slic3r::Domain::Vec2d{125.0, 110.0};
        params.transform.dont_arrange = true;
    });
}

void TransformController::dropToBed()
{
    run(QStringLiteral("drop"), [](Slic3r::App::InitParams& params) {
        params.transform.ensure_on_bed = true;
        params.transform.dont_arrange = true;
    });
}

void TransformController::cut(double z)
{
    run(QStringLiteral("cut"), [z](Slic3r::App::InitParams& params) {
        params.transform.cut_z = z;
        params.transform.ensure_on_bed = true;
    });
}

void TransformController::duplicate(int count)
{
    run(QStringLiteral("duplicate"), [count](Slic3r::App::InitParams& params) {
        params.transform.duplicate = count;
        params.transform.ensure_on_bed = true;
    });
}

void TransformController::run(const QString& label,
                              const std::function<void(Slic3r::App::InitParams&)>& configure)
{
    if (m_busy)
        return;
    if (m_modelPath.empty()) {
        emit failed(QStringLiteral("%1 failed: no model loaded").arg(label));
        return;
    }
    if (m_worker.joinable())
        m_worker.join();

    const QString output = jobPath(QStringLiteral("%1-%2").arg(label).arg(++m_jobIndex));
    const std::string model = m_modelPath;

    m_busy = true;
    emit busyChanged();

    m_worker = std::thread([this, label, output, model, configure] {
        const bool ok = runTransform(model, output.toStdString(), configure);
        const QString message = ok
            ? QStringLiteral("%1 applied").arg(label)
            : QStringLiteral("%1 failed: the engine produced no model").arg(label);
        QMetaObject::invokeMethod(
            this,
            [this, ok, message, output] {
                m_busy = false;
                emit busyChanged();
                if (ok)
                    emit applied(output, message);
                else
                    emit failed(message);
            },
            Qt::QueuedConnection
        );
    });
}
