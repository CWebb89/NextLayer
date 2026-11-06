// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Curtis Webb

#include "QtPlatform.hpp"

#include <Slic3r/Biz/Emboss/Emboss.hpp>
#include <Slic3r/Directories.hpp>

#include <fontconfig/fontconfig.h>
#include <nlohmann/json.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <optional>
#include <set>
#include <utility>

namespace next_layer {

namespace {

std::optional<nlohmann::json> parse(const QByteArray& bytes)
{
    const nlohmann::json document = nlohmann::json::parse(bytes.constData(),
                                                         bytes.constData() + bytes.size(),
                                                         nullptr, false);
    if (document.is_discarded() || !document.is_object())
        return std::nullopt;
    return document;
}

}

AppConfigProvider::AppConfigProvider(std::string dataDir)
    : m_path(std::move(dataDir) + "/qt-app-config.json")
    , m_downloadDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation).toStdString())
{
    read();
}

void AppConfigProvider::read()
{
    QFile file(QString::fromStdString(m_path));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const std::optional<nlohmann::json> document = parse(file.readAll());
    if (!document.has_value())
        return;

    const nlohmann::json& root = document.value();
    if (root.contains("download_dir") && root["download_dir"].is_string())
        m_downloadDir = root["download_dir"].get<std::string>();
    if (root.contains("last_physical_printer") && root["last_physical_printer"].is_string())
        m_lastPhysicalPrinter = root["last_physical_printer"].get<std::string>();
    if (root.contains("show_step_import_parameters") && root["show_step_import_parameters"].is_boolean())
        m_showStepImportParameters = root["show_step_import_parameters"].get<bool>();
    if (root.contains("step_linear_precision") && root["step_linear_precision"].is_number())
        m_stepLinearPrecision = root["step_linear_precision"].get<double>();
    if (root.contains("step_angle_precision") && root["step_angle_precision"].is_number())
        m_stepAnglePrecision = root["step_angle_precision"].get<double>();
}

void AppConfigProvider::write() const
{
    nlohmann::json root;
    root["download_dir"] = m_downloadDir.string();
    root["last_physical_printer"] = m_lastPhysicalPrinter;
    root["show_step_import_parameters"] = m_showStepImportParameters;
    root["step_linear_precision"] = m_stepLinearPrecision;
    root["step_angle_precision"] = m_stepAnglePrecision;

    const QByteArray bytes = QByteArray::fromStdString(root.dump(2));
    QFile file(QString::fromStdString(m_path));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(bytes);
}

boost::filesystem::path AppConfigProvider::download_dir() const
{
    return m_downloadDir;
}

std::string AppConfigProvider::last_used_physical_printer() const
{
    return m_lastPhysicalPrinter;
}

void AppConfigProvider::set_last_used_physical_printer(const std::string& uuid)
{
    m_lastPhysicalPrinter = uuid;
    write();
}

bool AppConfigProvider::get_show_step_import_parameters() const
{
    return m_showStepImportParameters;
}

void AppConfigProvider::set_show_step_import_parameters(bool show)
{
    m_showStepImportParameters = show;
    write();
}

double AppConfigProvider::get_step_linear_precision() const
{
    return m_stepLinearPrecision;
}

void AppConfigProvider::set_step_linear_precision(double precision)
{
    m_stepLinearPrecision = precision;
    write();
}

double AppConfigProvider::get_step_angle_precision() const
{
    return m_stepAnglePrecision;
}

void AppConfigProvider::set_step_angle_precision(double precision)
{
    m_stepAnglePrecision = precision;
    write();
}

void FileExplorerHandler::open_folder(const boost::filesystem::path& path)
{
    if (path.empty()) {
        invoke_listeners<Slic3r::App::Platform::IFileExplorerErrorListener>(
            [&path](Slic3r::App::Platform::IFileExplorerErrorListener* listener) {
                listener->on_file_explorer_error(
                    path, Slic3r::App::Platform::FileExplorerError::EmptyPath);
            });
        return;
    }

    const QFileInfo info(QString::fromStdString(path.string()));
    if (!info.exists()) {
        invoke_listeners<Slic3r::App::Platform::IFileExplorerErrorListener>(
            [&path](Slic3r::App::Platform::IFileExplorerErrorListener* listener) {
                listener->on_file_explorer_error(
                    path, Slic3r::App::Platform::FileExplorerError::DoesNotExist);
            });
        return;
    }
    if (!info.isDir()) {
        invoke_listeners<Slic3r::App::Platform::IFileExplorerErrorListener>(
            [&path](Slic3r::App::Platform::IFileExplorerErrorListener* listener) {
                listener->on_file_explorer_error(
                    path, Slic3r::App::Platform::FileExplorerError::NotADirectory);
            });
        return;
    }

    launch(path);
}

void FileExplorerHandler::open_datadir_folder()
{
    open_folder(boost::filesystem::path(Slic3r::data_dir()));
}

bool FileExplorerHandler::launch(const boost::filesystem::path& path)
{
    const bool started = QProcess::startDetached(QStringLiteral("xdg-open"),
                                                 {QString::fromStdString(path.string())});
    if (!started) {
        invoke_listeners<Slic3r::App::Platform::IFileExplorerErrorListener>(
            [&path](Slic3r::App::Platform::IFileExplorerErrorListener* listener) {
                listener->on_file_explorer_error(
                    path, Slic3r::App::Platform::FileExplorerError::LaunchFailed);
            });
    }
    return started;
}

void FontManager::scan()
{
    m_scanned = true;

    if (!FcInit())
        return;

    FcPattern* pattern = FcPatternCreate();
    FcObjectSet* objects = FcObjectSetBuild(FC_FILE, FC_FAMILY, FC_STYLE, nullptr);
    FcFontSet* fonts = FcFontList(nullptr, pattern, objects);
    if (fonts != nullptr) {
        std::set<std::string> seen;
        for (int index = 0; index < fonts->nfont; ++index) {
            FcPattern* font = fonts->fonts[index];
            FcChar8* file = nullptr;
            FcChar8* family = nullptr;
            FcChar8* style = nullptr;
            if (FcPatternGetString(font, FC_FILE, 0, &file) != FcResultMatch || file == nullptr)
                continue;
            if (FcPatternGetString(font, FC_FAMILY, 0, &family) != FcResultMatch || family == nullptr)
                continue;
            if (FcPatternGetString(font, FC_STYLE, 0, &style) != FcResultMatch)
                style = nullptr;

            const std::string path = reinterpret_cast<const char*>(file);
            if (!seen.insert(path).second)
                continue;

            std::string name = reinterpret_cast<const char*>(family);
            if (style != nullptr) {
                const std::string styleName = reinterpret_cast<const char*>(style);
                if (styleName != "Regular")
                    name += " " + styleName;
            }
            if (name.find("Vertical") != std::string::npos)
                continue;
            if (!load(path))
                continue;

            m_openable.push_back({std::move(name), path,
                                  Slic3r::Domain::FontDescriptor::Type::file_path});
        }
        FcFontSetDestroy(fonts);
    }
    if (objects != nullptr)
        FcObjectSetDestroy(objects);
    FcPatternDestroy(pattern);
}

std::unique_ptr<const Slic3r::Domain::FontFile> FontManager::load(const std::string& path)
{
    std::unique_ptr<Slic3r::Domain::FontFile> file =
        Slic3r::Biz::Emboss::create_font_file(path.c_str());
    if (!file)
        return nullptr;
    return file;
}

bool FontManager::resolve(const std::string& family, std::string& path)
{
    if (!FcInit())
        return false;

    FcPattern* pattern = FcNameParse(reinterpret_cast<const FcChar8*>(family.c_str()));
    if (pattern == nullptr)
        return false;
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result = FcResultNoMatch;
    FcPattern* match = FcFontMatch(nullptr, pattern, &result);
    bool found = false;
    if (match != nullptr) {
        FcChar8* file = nullptr;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file != nullptr) {
            path = reinterpret_cast<const char*>(file);
            found = true;
        }
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pattern);
    return found;
}

const Slic3r::Domain::FontList& FontManager::get_fonts()
{
    if (!m_scanned)
        scan();
    return m_openable;
}

std::unique_ptr<const Slic3r::Domain::FontFile> FontManager::open(
    const Slic3r::Domain::FontDescriptor& descriptor)
{
    if (descriptor.type == Slic3r::Domain::FontDescriptor::Type::file_path)
        return load(descriptor.path);

    std::string path;
    if (resolve(descriptor.name, path))
        return load(path);
    return nullptr;
}

FontManager::Descriptor FontManager::get_current_descriptor(
    const Slic3r::Domain::FontDescriptor& other_descriptor)
{
    if (other_descriptor.type == Slic3r::Domain::FontDescriptor::Type::file_path)
        return other_descriptor;

    std::string path;
    if (!resolve(other_descriptor.name, path))
        return tl::unexpected(ConversionError::AnotherOS);
    return Slic3r::Domain::FontDescriptor{other_descriptor.name, path,
                                          Slic3r::Domain::FontDescriptor::Type::file_path};
}

Slic3r::Domain::FontDescriptor::Type FontManager::get_current_type() const
{
    return Slic3r::Domain::FontDescriptor::Type::file_path;
}

Slic3r::Domain::FontList FontManager::create_favorit()
{
    Slic3r::Domain::FontList favorites;
    for (const char* family : {"DejaVu Sans", "DejaVu Sans Mono", "Liberation Sans", "Noto Sans"}) {
        std::string path;
        if (!resolve(family, path))
            continue;
        favorites.push_back({family, path, Slic3r::Domain::FontDescriptor::Type::file_path});
    }
    return favorites;
}

}
