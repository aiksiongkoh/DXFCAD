#include <DXFCAD/LWPolyline.h>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
void bindFreeCAD(py::module_& module);

PYBIND11_MODULE(DXFCAD, module)
{
    module.doc() = "Native DXFCAD domain objects; independent of the FreeCAD GUI.";
    bindFreeCAD(module);

    py::class_<DXFCAD::Point2D>(module, "Point2D")
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

    py::class_<DXFCAD::ExtrusionDirection>(module, "ExtrusionDirection",
        "A native extrusion direction; LWPolyline validates it without normalization.")
        .def(py::init([](double x, double y, double z) {
            return DXFCAD::ExtrusionDirection{x, y, z};
        }), py::arg("x") = 0.0, py::arg("y") = 0.0, py::arg("z") = 1.0)
        .def_readonly("x", &DXFCAD::ExtrusionDirection::x)
        .def_readonly("y", &DXFCAD::ExtrusionDirection::y)
        .def_readonly("z", &DXFCAD::ExtrusionDirection::z);

    py::class_<DXFCAD::LWPolyline>(module, "LWPolyline",
        "Validated native LWPolyline data. Properties are read-only snapshots.")
        .def(py::init<std::vector<DXFCAD::LWPolylineVertex>, bool, double, double,
                      DXFCAD::ExtrusionDirection>(),
            py::arg("vertices") = std::vector<DXFCAD::LWPolylineVertex>{},
            py::arg("closed") = false, py::arg("constant_width") = 0.0,
            py::arg("elevation") = 0.0,
            py::arg("extrusion") = DXFCAD::ExtrusionDirection{})
        // Copy values across the UI boundary so Python cannot mutate native
        // state through a returned container or a dangling reference.
        .def_property_readonly("vertices", &DXFCAD::LWPolyline::vertices,
                               py::return_value_policy::copy)
        .def_property_readonly("closed", &DXFCAD::LWPolyline::closed)
        .def_property_readonly("constant_width", &DXFCAD::LWPolyline::constantWidth)
        .def_property_readonly("elevation", &DXFCAD::LWPolyline::elevation)
        .def_property_readonly("extrusion", &DXFCAD::LWPolyline::extrusion,
                               py::return_value_policy::copy)
        .def("segments", &DXFCAD::LWPolyline::segments,
             "Derive OCS centerline segments; arc is None for straight segments.")
        .def("world_segments", &DXFCAD::LWPolyline::worldSegments,
             "Derive world-coordinate centerlines using elevation and extrusion.")
        .def("width_regions", &DXFCAD::LWPolyline::widthRegions, py::arg("tolerance") = 0.0001);
}
