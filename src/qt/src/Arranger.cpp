#include "Arranger.hpp"

#include <Slic3r/Biz/Algorithms/ClipperUtils.hpp>
#include <Slic3r/Biz/Algorithms/Scaling.hpp>
#include <Slic3r/Biz/Arrange/Arrange.hpp>
#include <Slic3r/Domain/ElementRef.hpp>
#include <Slic3r/Domain/ExPolygon.hpp>
#include <Slic3r/Domain/Point.hpp>
#include <Slic3r/Domain/Polygon.hpp>

#include <libslic3r/TriangleMeshSlicer.hpp>

#include <cmath>

namespace next_layer {

namespace {

namespace Scaling = Slic3r::Biz::Algorithms::Scaling;

using Slic3r::Biz::Arrange::InputShape;
using Slic3r::Biz::Arrange::Settings;
using Slic3r::Domain::ElementRef;
using Slic3r::Domain::Point;
using Slic3r::Domain::Points;
using Slic3r::Domain::Polygon;
using Slic3r::Domain::Polygons;
using Slic3r::Domain::Vec2d;

Points bedContour(QVector2D centre, QVector2D size)
{
    const double halfWidth = size.x() * 0.5;
    const double halfDepth = size.y() * 0.5;
    const Vec2d corners[4] = {
        Vec2d(centre.x() - halfWidth, centre.y() - halfDepth),
        Vec2d(centre.x() + halfWidth, centre.y() - halfDepth),
        Vec2d(centre.x() + halfWidth, centre.y() + halfDepth),
        Vec2d(centre.x() - halfWidth, centre.y() + halfDepth),
    };

    Points contour;
    contour.reserve(4);
    for (const Vec2d& corner : corners)
        contour.push_back(Point(static_cast<Slic3r::Domain::coord_t>(
                                    std::llround(Scaling::scaled(corner.x()))),
                                static_cast<Slic3r::Domain::coord_t>(
                                    std::llround(Scaling::scaled(corner.y())))));
    return contour;
}

}

std::vector<Placement> arrangeOnBed(const std::vector<PlaceableObject>& objects,
                                    QVector2D bedCentre,
                                    QVector2D bedSize,
                                    bool allowRotations)
{
    std::vector<Placement> placements;
    if (objects.empty())
        return placements;

    std::vector<InputShape> shapes;
    shapes.reserve(objects.size());

    for (size_t index = 0; index < objects.size(); ++index) {
        const PlaceableObject& object = objects[index];
        if (object.mesh == nullptr)
            return {};

        Slic3r::Domain::Transform3d transform = object.transform;
        transform.translation() = Slic3r::Domain::Vec3d::Zero();

        const Polygons outline = Slic3r::project_mesh(object.mesh->its, transform, []() {});
        if (outline.empty())
            return {};

        const Slic3r::Domain::ExPolygons merged =
            Slic3r::Biz::Algorithms::ClipperUtils::union_ex(outline);
        if (merged.empty())
            return {};

        shapes.push_back(InputShape{ElementRef(index, 0, 0), merged});
    }

    Settings settings;
    settings.strategy = Slic3r::Biz::Arrange::Strategy::Overfit;
    settings.allow_rotations = allowRotations;

    std::vector<Slic3r::Biz::Arrange::ArrangeItem> items =
        Slic3r::Biz::Arrange::to_arrange_items(shapes, settings);
    const Slic3r::Biz::Arrange::StopCondition neverStop = []() { return false; };

    const std::optional<Slic3r::Biz::Arrange::ArrangeResult> result =
        Slic3r::Biz::Arrange::arrange(bedContour(bedCentre, bedSize), items, {}, settings,
                                      neverStop);
    if (!result.has_value())
        return {};

    placements.reserve(result->packed.size());
    for (const Slic3r::Biz::Arrange::ArrangeItem& item : result->packed) {
        const size_t index = item.get_element_ref().object_id;
        if (index >= objects.size())
            continue;
        Placement placement;
        placement.id = objects[index].id;
        const Vec2d offset = Scaling::unscaled<double>(item.get_translation());
        placement.offset = Slic3r::Domain::Vec3d(offset.x(), offset.y(), 0.0);
        placement.rotationZ = item.get_rotation();
        placements.push_back(placement);
    }
    return placements;
}

}
