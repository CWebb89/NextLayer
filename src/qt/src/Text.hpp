#pragma once

#include <Slic3r/Domain/TriangleMesh.hpp>

#include <string>
#include <vector>

namespace next_layer {

struct TextFont
{
    std::string family;
    std::string path;
};

/// Every font file on this machine, family name and path, alphabetically. The
/// engine's own Emboss::get_font_list() is a no-op off Windows, so the list comes
/// from fontconfig -- which the app already links for the engine's own use.
std::vector<TextFont> textFonts();

/// A line of text as a printable solid: the glyph outlines extruded to `depth`
/// millimetres, scaled so the tallest line is `height` millimetres, centred on the
/// origin in X/Y and sitting on z = 0. The engine does the outline extraction
/// (Emboss::text2shapes) and the triangulation (Biz::Algorithms::Tesselate).
bool textMesh(const std::string& text, double height, double depth,
              const std::string& fontPath, Slic3r::Domain::TriangleMesh& mesh,
              std::string& error);

}
