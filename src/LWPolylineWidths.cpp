#include <DXFCAD/OCSFrame.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace DXFCAD {
namespace {
bool equal(const Point2D& a, const Point2D& b) { return a.x == b.x && a.y == b.y; }
}

std::vector<LWPolylineWidthRegion> LWPolyline::widthRegions(double tolerance) const
{
    if (!std::isfinite(tolerance) || tolerance <= 0) {
        throw std::invalid_argument("Width outline tolerance must be finite and positive");
    }
    const auto local = segments();
    const OCSFrame frame(extrusion_);
    const bool variable = std::any_of(vertices_.begin(), vertices_.end(), [](const auto& v) {
        return v.hasWidths || v.startWidth != 0 || v.endWidth != 0;
    });
    std::vector<LWPolylineWidthRegion> result;
    auto add = [&](std::size_t index, const std::vector<Point2D>& boundary) {
        std::vector<Point2D> unique;
        for (const auto& p : boundary) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
                throw std::overflow_error("Width region coordinates are not representable");
            }
            if (unique.empty() || !equal(unique.back(), p)) unique.push_back(p);
        }
        if (unique.size() > 1 && equal(unique.front(), unique.back())) unique.pop_back();
        if (unique.size() < 3) return;
        // OCC's coplanar fuse requires consistent face normals. Keep the first
        // boundary point stable while giving all regions clockwise OCS winding.
        double extent = 0;
        for (const auto& p : unique) extent = std::max({extent, std::abs(p.x-unique[0].x), std::abs(p.y-unique[0].y)});
        double signedArea = 0;
        if (extent > 0 && std::isfinite(extent)) {
            for (std::size_t j = 1; j+1 < unique.size(); ++j) {
                const double ax = (unique[j].x-unique[0].x)/extent, ay = (unique[j].y-unique[0].y)/extent;
                const double bx = (unique[j+1].x-unique[0].x)/extent, by = (unique[j+1].y-unique[0].y)/extent;
                signedArea += ax*by-ay*bx;
            }
            if (signedArea > 0) std::reverse(unique.begin()+1, unique.end());
        }
        LWPolylineWidthRegion region{index, {}};
        region.points.reserve(unique.size());
        for (const auto& p : unique) region.points.push_back(frame.toWorld(p, elevation_));
        result.push_back(std::move(region));
    };
    for (std::size_t i = 0; i < local.size(); ++i) {
        const auto& segment = local[i];
        const auto& vertex = vertices_[i];
        const double startWidth = variable ? vertex.startWidth : constantWidth_;
        const double endWidth = variable ? vertex.endWidth : constantWidth_;
        if (startWidth == 0 && endWidth == 0) continue;
        const double dx = segment.end.x-segment.start.x, dy = segment.end.y-segment.start.y;
        const double chord = std::hypot(dx, dy);
        if (chord == 0) continue;
        if (!std::isfinite(chord)) throw std::overflow_error("Width region chord is not representable");
        if (!segment.arc) {
            const double nx = -dy/chord, ny = dx/chord;
            add(i, {{segment.start.x+nx*startWidth/2, segment.start.y+ny*startWidth/2},
                    {segment.end.x+nx*endWidth/2, segment.end.y+ny*endWidth/2},
                    {segment.end.x-nx*endWidth/2, segment.end.y-ny*endWidth/2},
                    {segment.start.x-nx*startWidth/2, segment.start.y-ny*startWidth/2}});
            continue;
        }
        const auto& arc = *segment.arc;
        const double rx = segment.start.x-arc.center.x, ry = segment.start.y-arc.center.y;
        const double sweep = std::abs(arc.sweep);
        // A radial boundary r(theta) has |p''| <= r + 2|r'|. Linear width
        // interpolation therefore admits this conservative chord-error bound.
        const double bound = arc.radius + std::max(startWidth, endWidth)/2
            + std::abs(endWidth-startWidth)/sweep;
        const double count = std::ceil(std::max(sweep*std::sqrt(bound/tolerance/8),
                                                sweep/(std::acos(-1.0)/8)));
        constexpr double maxSteps = 65536;
        if (!std::isfinite(count) || count > maxSteps) {
            throw std::overflow_error("Width outline requires more than 65536 arc subdivisions");
        }
        const auto steps = static_cast<std::size_t>(std::max(1.0, count));
        std::vector<double> parameters;
        for (std::size_t j = 0; j <= steps; ++j) parameters.push_back(static_cast<double>(j)/steps);
        // Split exactly where the inner radial boundary crosses the arc center.
        // The positive and negative radial portions are separate simple regions;
        // their union handles wide arcs without a self-intersecting bow-tie wire.
        if (endWidth != startWidth) {
            const double cross = (2*arc.radius-startWidth)/(endWidth-startWidth);
            if (cross > 0 && cross < 1) parameters.push_back(cross);
            std::sort(parameters.begin(), parameters.end());
            parameters.erase(std::unique(parameters.begin(), parameters.end()), parameters.end());
        }
        std::vector<Point2D> outer, inner, negative;
        for (const double t : parameters) {
            const double a = arc.sweep*t;
            const double width = (1-t)*startWidth+t*endWidth;
            const double c = std::cos(a), s = std::sin(a);
            const double ux = (rx/arc.radius)*c-(ry/arc.radius)*s;
            const double uy = (rx/arc.radius)*s+(ry/arc.radius)*c;
            // Rotate relative to the start instead of subtracting a huge arc
            // radius from its center. This preserves nearly straight bulges.
            const double cm1 = -2*std::sin(a/2)*std::sin(a/2);
            Point2D base{segment.start.x+rx*cm1-ry*s, segment.start.y+rx*s+ry*cm1};
            if (t == 0) base = segment.start;
            if (t == 1) base = segment.end;
            const double rInner = arc.radius-width/2;
            outer.push_back({base.x+ux*width/2, base.y+uy*width/2});
            inner.push_back(rInner <= 0 ? arc.center : Point2D{base.x-ux*width/2, base.y-uy*width/2});
            if (rInner <= 0) negative.push_back({arc.center.x+ux*rInner, arc.center.y+uy*rInner});
        }
        outer.insert(outer.end(), inner.rbegin(), inner.rend());
        add(i, outer);
        if (negative.size() > 1) {
            negative.push_back(arc.center);
            add(i, negative);
        }
    }
    // Close outer gaps at vertices with intersections of the actual offset
    // tangents (including taper). A bounded miter avoids unbounded spikes at
    // near reversals; those corners use a bevel instead. Segment ends stay flat.
    struct End {
        Point2D point, tangent, boundary, direction;
        double width;
    };
    auto endpoint = [&](std::size_t i, bool atEnd, double side) -> std::optional<End> {
        const auto& segment = local[i];
        const auto& v = vertices_[i];
        const double sw = variable ? v.startWidth : constantWidth_;
        const double ew = variable ? v.endWidth : constantWidth_;
        const double w = atEnd ? ew : sw;
        const auto p = atEnd ? segment.end : segment.start;
        double dx = segment.end.x-segment.start.x, dy = segment.end.y-segment.start.y;
        double length = std::hypot(dx, dy), curvature = 0;
        if (length == 0) return {};
        Point2D tangent{dx/length, dy/length};
        if (segment.arc) {
            const auto& arc = *segment.arc;
            const double sign = arc.sweep > 0 ? 1 : -1;
            tangent = {-sign*(p.y-arc.center.y)/arc.radius, sign*(p.x-arc.center.x)/arc.radius};
            length = arc.radius*std::abs(arc.sweep);
            curvature = sign/arc.radius;
        }
        const Point2D n{-tangent.y, tangent.x};
        const double slope = (ew-sw)/length;
        End end{p, tangent, {p.x+side*w*n.x/2, p.y+side*w*n.y/2},
            {(1-side*w*curvature/2)*tangent.x+side*slope*n.x/2,
             (1-side*w*curvature/2)*tangent.y+side*slope*n.y/2}, w};
        return end;
    };
    auto cross = [](Point2D a, Point2D b) { return a.x*b.y-a.y*b.x; };
    const auto joins = closed_ ? local.size() : (local.empty() ? 0 : local.size()-1);
    for (std::size_t i = 0; i < joins; ++i) {
        const std::size_t next = (i+1)%local.size();
        auto a = endpoint(i, true, 1), b = endpoint(next, false, 1);
        if (!a || !b || a->width == 0 || b->width == 0) continue;
        const double turn = cross(a->tangent, b->tangent);
        if (std::abs(turn) < 1e-12) continue;
        const double side = turn > 0 ? -1 : 1;
        a = endpoint(i, true, side); b = endpoint(next, false, side);
        const Point2D delta{b->boundary.x-a->boundary.x, b->boundary.y-a->boundary.y};
        const double determinant = cross(a->direction, b->direction);
        bool miter = std::isfinite(determinant) && std::abs(determinant) > 1e-12;
        Point2D intersection{};
        if (miter) {
            const double t = cross(delta, b->direction)/determinant;
            const double u = cross(delta, a->direction)/determinant;
            intersection = {a->boundary.x+t*a->direction.x, a->boundary.y+t*a->direction.y};
            const double distance = std::hypot(intersection.x-a->point.x, intersection.y-a->point.y);
            miter = t >= 0 && u <= 0 && std::isfinite(distance)
                && distance <= 10*std::max(a->width,b->width)/2;
        }
        if (miter) add(i, {a->point, a->boundary, intersection, b->boundary});
        else add(i, {a->point, a->boundary, b->boundary});
    }
    return result;
}

} // namespace DXFCAD
