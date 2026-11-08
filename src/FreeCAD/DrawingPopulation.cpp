#include "DrawingPopulation.h"
#include "DocumentOperations.h"

#include <algorithm>
#include <limits>
#include <set>

namespace DXFCAD::FreeCAD {

DrawingPopulation::DrawingPopulation(py::object document,const Drawing& drawing,const std::string& sourceFile,
                  const std::string& label,const std::string& name,const std::string& font,py::object progress)
    : source(Drawing::parse(drawing.serialize()))
{
    // Populate exactly the retained snapshot, including resolved block
    // contexts and supported-entity slots. Never synthesize source layers.
    if (source.entities.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw py::value_error("Too many DXF entities for persistent source indices");
    auto module=py::module_::import("DXFCAD");
    root=module.attr("create_drawing")(document,py::cast(source),sourceFile,label,name);
    entities.reserve(source.entities.size());
    if (!progress.is_none()) progress(0,source.entities.size());
    for (std::size_t i=0;i<source.entities.size();++i) {
        const auto& entity=source.entities[i];
        const auto layer=entity->layer();
        if (!layerGroups.count(layer))
            layerGroups.emplace(layer,materializeLayerGroup(root,layer,py::none()));
        py::object object=createOwnedEntity(root,entity,static_cast<int>(i),font,layerGroups.at(layer));
        entities.push_back(object);
        if (!progress.is_none()) progress(i+1,source.entities.size());
    }
}

} // namespace DXFCAD::FreeCAD
