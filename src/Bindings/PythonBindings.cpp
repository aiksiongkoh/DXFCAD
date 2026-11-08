#include <DXFCAD/DXFLWPolyline.h>
#include <DXFCAD/CartesianFrame.h>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
void bindFreeCAD(py::module_& module);

PYBIND11_MODULE(DXFCAD, module)
{
    module.doc() = "Native DXFCAD domain objects; independent of the FreeCAD GUI.";
    bindFreeCAD(module);

    py::class_<DXFCAD::Point2D>(module, "Point2D")
        .def(py::init([](double x, double y) { return DXFCAD::Point2D{x, y}; }), py::arg("x") = 0, py::arg("y") = 0)
        .def_readonly("x", &DXFCAD::Point2D::x)
        .def_readonly("y", &DXFCAD::Point2D::y);
    py::class_<DXFCAD::LWPolylineArc>(module, "LWPolylineArc")
        .def_readonly("center", &DXFCAD::LWPolylineArc::center)
        .def_readonly("midpoint", &DXFCAD::LWPolylineArc::midpoint)
        .def_readonly("radius", &DXFCAD::LWPolylineArc::radius)
        .def_readonly("sweep", &DXFCAD::LWPolylineArc::sweep);
    py::class_<DXFCAD::LWPolylineSegment>(module, "LWPolylineSegment")
        .def_readonly("start", &DXFCAD::LWPolylineSegment::start)
        .def_readonly("end", &DXFCAD::LWPolylineSegment::end)
        .def_readonly("arc", &DXFCAD::LWPolylineSegment::arc);

    py::class_<DXFCAD::Vector3D>(module, "Vector3D")
        .def(py::init([](double x, double y, double z) { return DXFCAD::Vector3D{x, y, z}; }),
             py::arg("x") = 0, py::arg("y") = 0, py::arg("z") = 0)
        .def_readonly("x", &DXFCAD::Vector3D::x)
        .def_readonly("y", &DXFCAD::Vector3D::y)
        .def_readonly("z", &DXFCAD::Vector3D::z);
    py::class_<DXFCAD::LWPolylineArc3D>(module, "LWPolylineArc3D")
        .def_readonly("center", &DXFCAD::LWPolylineArc3D::center)
        .def_readonly("midpoint", &DXFCAD::LWPolylineArc3D::midpoint)
        .def_readonly("normal", &DXFCAD::LWPolylineArc3D::normal)
        .def_readonly("radius", &DXFCAD::LWPolylineArc3D::radius)
        .def_readonly("sweep", &DXFCAD::LWPolylineArc3D::sweep);
    py::class_<DXFCAD::LWPolylineSegment3D>(module, "LWPolylineSegment3D")
        .def_readonly("start", &DXFCAD::LWPolylineSegment3D::start)
        .def_readonly("end", &DXFCAD::LWPolylineSegment3D::end)
        .def_readonly("arc", &DXFCAD::LWPolylineSegment3D::arc);
    py::class_<DXFCAD::LWPolylineWidthRegion>(module, "LWPolylineWidthRegion")
        .def_readonly("segment_index", &DXFCAD::LWPolylineWidthRegion::segmentIndex)
        .def_readonly("points", &DXFCAD::LWPolylineWidthRegion::points);

    py::class_<DXFCAD::LWPolylineVertex>(module, "LWPolylineVertex",
        "An OCS vertex; bulge and widths describe the outgoing segment.")
        .def(py::init([](double x, double y, double bulge,
                         double startWidth, double endWidth, bool hasWidths) {
            return DXFCAD::LWPolylineVertex{x, y, bulge, startWidth, endWidth, hasWidths};
        }), py::arg("x") = 0.0, py::arg("y") = 0.0, py::arg("bulge") = 0.0,
            py::arg("start_width") = 0.0, py::arg("end_width") = 0.0,
            py::arg("has_widths") = false)
        .def_readonly("x", &DXFCAD::LWPolylineVertex::x)
        .def_readonly("y", &DXFCAD::LWPolylineVertex::y)
        .def_readonly("bulge", &DXFCAD::LWPolylineVertex::bulge)
        .def_readonly("start_width", &DXFCAD::LWPolylineVertex::startWidth)
        .def_readonly("end_width", &DXFCAD::LWPolylineVertex::endWidth)
        .def_readonly("has_widths", &DXFCAD::LWPolylineVertex::hasWidths);

    py::class_<DXFCAD::CartesianFrame>(module, "CartesianFrame",
        "Right-handed Cartesian frame through the world origin, constructed from a normal.")
        .def(py::init<const DXFCAD::Vector3D&>(), py::arg("normal"))
        .def_property_readonly("normal", &DXFCAD::CartesianFrame::normal, py::return_value_policy::copy)
        .def_property_readonly("x_axis", &DXFCAD::CartesianFrame::xAxis, py::return_value_policy::copy)
        .def_property_readonly("y_axis", &DXFCAD::CartesianFrame::yAxis, py::return_value_policy::copy)
        .def("to_world", &DXFCAD::CartesianFrame::toWorld, py::arg("point"), py::arg("elevation"))
        .def("from_world", &DXFCAD::CartesianFrame::fromWorld, py::arg("point"))
        .def("intersect_line", &DXFCAD::CartesianFrame::intersectLine,
             py::arg("start"), py::arg("end"), py::arg("elevation"));

    py::class_<DXFCAD::DXFLWPolyline, DXFCAD::DXFEntity,
               std::shared_ptr<DXFCAD::DXFLWPolyline>>(module, "DXFLWPolyline",
        "Native LWPOLYLINE entity with record-preserving geometry and edits.")
        .def(py::init<std::vector<DXFCAD::LWPolylineVertex>, bool, double, double,
                      DXFCAD::Vector3D>(),
            py::arg("vertices") = std::vector<DXFCAD::LWPolylineVertex>{},
            py::arg("closed") = false, py::arg("constant_width") = 0.0,
            py::arg("elevation") = 0.0,
            py::arg("extrusion") = DXFCAD::Vector3D{0, 0, 1})
        .def_static("from_raw", &DXFCAD::DXFLWPolyline::fromRaw, py::arg("raw"))
        // Copy values across the UI boundary so Python cannot mutate native
        // state through a returned container or a dangling reference.
        .def_property_readonly("vertices", &DXFCAD::DXFLWPolyline::vertices,
                               py::return_value_policy::copy)
        .def_property_readonly("closed", &DXFCAD::DXFLWPolyline::closed)
        .def_property_readonly("constant_width", &DXFCAD::DXFLWPolyline::constantWidth)
        .def_property_readonly("elevation", &DXFCAD::DXFLWPolyline::elevation)
        .def_property_readonly("extrusion", &DXFCAD::DXFLWPolyline::extrusion,
                               py::return_value_policy::copy)
        .def("with_vertex", &DXFCAD::DXFLWPolyline::withVertex)
        .def("inserted_vertex", &DXFCAD::DXFLWPolyline::insertedVertex)
        .def("removed_vertex", &DXFCAD::DXFLWPolyline::removedVertex)
        .def("with_properties", &DXFCAD::DXFLWPolyline::withProperties,
             py::arg("closed"), py::arg("constant_width"), py::arg("elevation"), py::arg("extrusion"))
        .def("moved", &DXFCAD::DXFLWPolyline::moved, py::arg("offset"))
        .def("rotated_in_plane", &DXFCAD::DXFLWPolyline::rotatedInPlane, py::arg("radians"), py::arg("pivot") = DXFCAD::Point2D{0,0})
        .def("scaled_in_plane", &DXFCAD::DXFLWPolyline::scaledInPlane, py::arg("factor"), py::arg("pivot") = DXFCAD::Point2D{0,0})
        .def("mirrored_in_plane", &DXFCAD::DXFLWPolyline::mirroredInPlane, py::arg("line_start"), py::arg("line_end"))
        .def("world_vertices", &DXFCAD::DXFLWPolyline::worldVertices)
        .def("with_vertex_on_line", &DXFCAD::DXFLWPolyline::withVertexOnLine,
             py::arg("index"), py::arg("start"), py::arg("end"))
        .def("with_vertex_at_vertex", &DXFCAD::DXFLWPolyline::withVertexAtVertex,
             py::arg("index"), py::arg("target"))
        .def("with_vertex_at_world_point", &DXFCAD::DXFLWPolyline::withVertexAtWorldPoint,
             py::arg("index"), py::arg("point"), py::arg("plane_tolerance") = 1e-7)
        .def("segments", &DXFCAD::DXFLWPolyline::segments,
             "Derive OCS centerline segments; arc is None for straight segments.")
        .def("world_segments", &DXFCAD::DXFLWPolyline::worldSegments,
             "Derive world-coordinate centerlines using elevation and extrusion.")
        .def("width_regions", &DXFCAD::DXFLWPolyline::widthRegions, py::arg("tolerance") = 0.0001);

}
