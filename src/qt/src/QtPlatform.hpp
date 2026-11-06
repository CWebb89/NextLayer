#pragma once

#include <Slic3r/App/Platform/IFileExplorerHandler.hpp>
#include <Slic3r/Biz/Emboss/IFontManager.hpp>
#include <Slic3r/Biz/Platform/IAppConfigProvider.hpp>

#include <boost/filesystem/path.hpp>

#include <string>

namespace next_layer {

class AppConfigProvider final : public Slic3r::Biz::Platform::IAppConfigProvider
{
public:
    explicit AppConfigProvider(std::string dataDir);

    boost::filesystem::path download_dir() const override;

    std::string last_used_physical_printer() const override;
    void set_last_used_physical_printer(const std::string& uuid) override;

    bool get_show_step_import_parameters() const override;
    void set_show_step_import_parameters(bool show) override;
    double get_step_linear_precision() const override;
    void set_step_linear_precision(double precision) override;
    double get_step_angle_precision() const override;
    void set_step_angle_precision(double precision) override;

private:
    void read();
    void write() const;

    std::string m_path;
    boost::filesystem::path m_downloadDir;
    std::string m_lastPhysicalPrinter;
    bool m_showStepImportParameters{true};
    double m_stepLinearPrecision{0.01};
    double m_stepAnglePrecision{5.0};
};

class FileExplorerHandler final : public Slic3r::App::Platform::IFileExplorerHandler
{
public:
    void open_folder(const boost::filesystem::path& path) override;
    void open_datadir_folder() override;

private:
    bool launch(const boost::filesystem::path& path);
};

class FontManager final : public Slic3r::Biz::Emboss::IFontManager
{
public:
    const Slic3r::Domain::FontList& get_fonts() override;
    std::unique_ptr<const Slic3r::Domain::FontFile> open(
        const Slic3r::Domain::FontDescriptor& descriptor) override;
    Descriptor get_current_descriptor(
        const Slic3r::Domain::FontDescriptor& other_descriptor) override;
    Slic3r::Domain::FontDescriptor::Type get_current_type() const override;
    Slic3r::Domain::FontList create_favorit() override;

private:
    void scan();
    static bool resolve(const std::string& family, std::string& path);
    static std::unique_ptr<const Slic3r::Domain::FontFile> load(const std::string& path);

    Slic3r::Domain::FontList m_openable;
    bool m_scanned{false};
};

}
