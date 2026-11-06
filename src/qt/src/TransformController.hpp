#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <string>
#include <thread>

namespace Slic3r::App {
struct InitParams;
}

// Model transforms that the engine already implements: it loads the mesh,
// applies the transform parameters, and exports the result as STL, which the
// session then reloads. Every action runs on a worker thread because the engine
// call is synchronous and can take a second on a dense mesh.
class TransformController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit TransformController(std::string modelPath, QObject* parent = nullptr);
    ~TransformController() override;

    bool busy() const { return m_busy; }
    void setModelPath(std::string modelPath);

    Q_INVOKABLE void rotate(double x, double y, double z);
    Q_INVOKABLE void scale(double factor);
    Q_INVOKABLE void moveTo(double x, double y);
    Q_INVOKABLE void center();
    Q_INVOKABLE void dropToBed();
    Q_INVOKABLE void cut(double z);
    Q_INVOKABLE void duplicate(int count);

signals:
    void busyChanged();
    void applied(const QString& path, const QString& message);
    void failed(const QString& message);

private:
    void run(const QString& label, const std::function<void(Slic3r::App::InitParams&)>& configure);

    std::string m_modelPath;
    std::thread m_worker;
    int m_jobIndex = 0;
    bool m_busy = false;
};

// Runs a transform through the CLI in-place and writes the result to
// `outputPath`. Exposed so the application's own `--transform` flag can verify
// the engine path without a window.
bool runTransform(const std::string& modelPath, const std::string& outputPath,
                  const std::function<void(Slic3r::App::InitParams&)>& configure);
