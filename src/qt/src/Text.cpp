#include "Text.hpp"

#include <Slic3r/Biz/Algorithms/Tesselate.hpp>
#include <Slic3r/Biz/Emboss/Emboss.hpp>

#include <fontconfig/fontconfig.h>

#include <algorithm>
#include <memory>
#include <string>

namespace next_layer {

namespace {

using Slic3r::Domain::ExPolygon;
using Slic3r::Domain::ExPolygons;
using Slic3r::Domain::TriangleMesh;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::Index3;

void appendIts(TriangleMesh& target, const ::indexed_triangle_set& its)
{
    const auto offset = static_cast<int>(target.its.vertices.size());
    target.its.vertices.insert(target.its.vertices.end(), its.vertices.begin(), its.vertices.end());
    for (const auto& face : its.indices)
        target.its.indices.push_back(Index3{face[0] + offset, face[1] + offset, face[2] + offset});
}

void appendSoup(TriangleMesh& target, const std::vector<Vec3d>& points)
{
    for (size_t i = 0; i + 2 < points.size(); i += 3) {
        const auto base = static_cast<int>(target.its.vertices.size());
        for (int corner = 0; corner < 3; ++corner) {
            const Vec3d& point = points[i + corner];
            target.its.vertices.push_back(
                Slic3r::Domain::Vec3f(float(point.x()), float(point.y()), float(point.z())));
        }
        target.its.indices.push_back(Index3{base, base + 1, base + 2});
    }
}

}

std::vector<TextFont> textFonts()
{
    std::vector<TextFont> fonts;
    if (!FcInit())
        return fonts;

    FcPattern* pattern = FcPatternCreate();
    FcObjectSet* objects = FcObjectSetBuild(FC_FILE, FC_FAMILY, nullptr);
    FcFontSet* set = FcFontList(nullptr, pattern, objects);

    if (set != nullptr) {
        for (int i = 0; i < set->nfont; ++i) {
            FcChar8* file = nullptr;
            FcChar8* family = nullptr;
            if (FcPatternGetString(set->fonts[i], FC_FILE, 0, &file) != FcResultMatch
                || file == nullptr)
                continue;
            if (FcPatternGetString(set->fonts[i], FC_FAMILY, 0, &family) != FcResultMatch)
                family = file;
            fonts.push_back(TextFont{reinterpret_cast<const char*>(family),
                                     reinterpret_cast<const char*>(file)});
        }
        FcFontSetDestroy(set);
    }
    FcObjectSetDestroy(objects);
    FcPatternDestroy(pattern);

    std::sort(fonts.begin(), fonts.end(), [](const TextFont& left, const TextFont& right) {
        return left.family == right.family ? left.path < right.path : left.family < right.family;
    });
    return fonts;
}

bool textMesh(const std::string& text, double height, double depth, const std::string& fontPath,
              TriangleMesh& mesh, std::string& error)
{
    mesh = TriangleMesh();

    const std::string trimmed = text.empty() ? std::string() : text;
    if (trimmed.find_first_not_of(" \t\n") == std::string::npos) {
        error = "there is no text to emboss";
        return false;
    }

    std::string path = fontPath;
    if (path.empty()) {
        const std::vector<TextFont> fonts = textFonts();
        if (fonts.empty()) {
            error = "no fonts on this machine";
            return false;
        }
        path = fonts.front().path;
    }

    std::unique_ptr<Slic3r::Domain::FontFile> file =
        Slic3r::Biz::Emboss::create_font_file(path.c_str());
    if (!file) {
        error = path + ": could not read the font file";
        return false;
    }

    Slic3r::Biz::Emboss::FontFileWithCache cached;
    cached.descriptor.name = path;
    cached.descriptor.path = path;
    cached.descriptor.type = Slic3r::Domain::FontDescriptor::Type::file_path;
    cached.font_file = std::shared_ptr<const Slic3r::Domain::FontFile>(std::move(file));
    cached.cache = std::make_shared<Slic3r::Biz::Emboss::Glyphs>();

    const Slic3r::Domain::HealedExPolygons healed =
        Slic3r::Biz::Emboss::text2shapes(cached, trimmed.c_str(), Slic3r::Domain::FontProp());
    ExPolygons shapes = healed.expolygons;
    if (shapes.empty()) {
        error = path + ": the font has no outline for \"" + trimmed + "\"";
        return false;
    }

    // The engine returns glyph outlines in font units scaled up by a fixed
    // constant. Rather than guess at that constant, the shapes are measured and
    // scaled to the height the user asked for -- which is the only height they can
    // see -- and centred so the object lands on the bed like any other.
    double lowX = 1e30;
    double lowY = 1e30;
    double highX = -1e30;
    double highY = -1e30;
    for (const ExPolygon& shape : shapes) {
        for (const auto& point : shape.contour.points) {
            lowX = std::min(lowX, double(point.x()));
            highX = std::max(highX, double(point.x()));
            lowY = std::min(lowY, double(point.y()));
            highY = std::max(highY, double(point.y()));
        }
        for (const auto& hole : shape.holes) {
            for (const auto& point : hole.points) {
                lowX = std::min(lowX, double(point.x()));
                highX = std::max(highX, double(point.x()));
                lowY = std::min(lowY, double(point.y()));
                highY = std::max(highY, double(point.y()));
            }
        }
    }

    const double span = highY - lowY;
    if (span <= 0.0) {
        error = "the glyphs have no height";
        return false;
    }

    const double wanted = std::clamp(height, 1.0, 200.0);
    const double wall = std::clamp(depth, 0.1, 50.0);
    // Shape coordinates are fixed-point in the polygon's own units, while the
    // polygons this app builds everywhere else are coord_t -- millimetres divided
    // by SCALING_FACTOR. The scale therefore maps the measured span onto the
    // requested height *in coord units*, which is what makes the object come out
    // the size the user typed.
    const double scale = (wanted / Slic3r::Domain::SCALING_FACTOR) / span;
    const Slic3r::Domain::Vec2d centre((lowX + highX) * 0.5, lowY + span * 0.5);

    auto transform = [centre, scale](const Slic3r::Domain::Point& point) {
        return Slic3r::Domain::Point(
            Slic3r::Domain::coord_t(std::lround((point.x() - centre.x()) * scale)),
            Slic3r::Domain::coord_t(std::lround((point.y() - centre.y()) * scale)));
    };

    ExPolygons placed = shapes;
    for (ExPolygon& shape : placed) {
        for (auto& point : shape.contour.points)
            point = transform(point);
        for (auto& hole : shape.holes)
            for (auto& point : hole.points)
                point = transform(point);
    }

    appendSoup(mesh, Slic3r::Biz::Algorithms::Tesselate::triangulate_expolygons_3d(
                         placed, 0.0, Slic3r::Biz::Algorithms::Tesselate::NORMALS_DOWN));
    appendSoup(mesh, Slic3r::Biz::Algorithms::Tesselate::triangulate_expolygons_3d(
                         placed, wall, Slic3r::Biz::Algorithms::Tesselate::NORMALS_UP));

    for (const ExPolygon& shape : placed) {
        appendIts(mesh, Slic3r::Biz::Algorithms::Tesselate::wall_strip(shape.contour, 0.0, wall));
        for (const auto& hole : shape.holes)
            appendIts(mesh, Slic3r::Biz::Algorithms::Tesselate::wall_strip(hole, 0.0, wall));
    }

    if (mesh.its.indices.empty()) {
        error = "the glyphs produced no faces";
        return false;
    }
    return true;
}

}
