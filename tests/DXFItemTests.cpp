#include <DXFCAD/DXFItem.h>
#include <DXFCAD/Drawing.h>
#include <DXFCAD/DXFPolyline.h>
#include <DXFCAD/CartesianFrame.h>
#include <DXFCAD/ParametricCurves.h>
#include <DXFCAD/Drafting.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>

using namespace DXFCAD;
static_assert(std::is_abstract_v<DXFItem>);
static_assert(std::has_virtual_destructor_v<DXFItem>);
static_assert(std::is_convertible_v<DXFEntity*, DXFItem*>);
static_assert(std::is_convertible_v<DXFLWPolyline*, DXFItem*>);
static_assert(std::is_convertible_v<DXFPolyline*, DXFItem*>);
static_assert(std::is_convertible_v<DXFEllipse*, DXFItem*>);
static_assert(std::is_convertible_v<DXFSpline*, DXFItem*>);
static_assert(std::is_convertible_v<DXFViewport*, DXFItem*>);
static_assert(std::is_convertible_v<DXFLayout*, DXFItem*>);
static_assert(std::is_convertible_v<DXFLayer*, DXFItem*>);
static_assert(!std::is_base_of_v<DXFItem, CartesianFrame>);
static_assert(!std::is_base_of_v<DXFItem, Vector3D>);
static_assert(!std::is_base_of_v<DXFItem, Drawing>);
static_assert(!std::is_base_of_v<DXFItem, GroupSequence>);

namespace {
void check(bool value) {
    if (!value) throw std::runtime_error("DXFItem hierarchy or value contract failed");
}
}

int main()
{
    try {
        // Preserve the prior defaults and full/partial brace construction.
        const DXFLayer defaults{};
        const DXFLayer named{"Cutting"};
        DXFLayer layer{"Cutting", 3, 0x112233, false};
        check(defaults.name.empty() && defaults.color == 7 && defaults.trueColor == -1 && defaults.visible);
        check(named.color == 7 && named.trueColor == -1 && named.visible);
        check(layer.name == "Cutting" && layer.color == 3 && layer.trueColor == 0x112233 && !layer.visible);
        std::vector<std::unique_ptr<const DXFItem>> values;
        values.push_back(std::make_unique<DXFLayer>(layer));
        layer.name = "Changed";
        check(dynamic_cast<const DXFLayer&>(*values[0]).name == "Cutting");
        const auto viewport = readDXFGroups("0\nVIEWPORT\n5\nA\n67\n1\n69\n2\n68\n2\n10\n0\n20\n0\n"
                                          "40\n10\n41\n5\n45\n50\n16\n0\n26\n0\n36\n1\n");
        values.push_back(std::make_unique<DXFViewport>(viewport));
        check(dynamic_cast<const DXFViewport&>(*values.back()).raw() == viewport.raw());
        const auto raw = "0\nLINE\n5\nAB\n8\n0\n10\n0\n20\n0\n11\n2\n21\n3\n"
                         "1001\nTEST\n1000\nretain opaque data\n";
        const std::shared_ptr<DXFItem> item = parseEntity(raw);
        const auto entity = std::dynamic_pointer_cast<DXFEntity>(item);
        check(entity && entity->type() == "LINE" && entity->raw() == raw);
        check(entity->handle() == "AB" && entity->geometry().segments.size() == 1);
        values.clear(); // Destroy record and metadata types through DXFItem.
        std::cout << "DXF reference hierarchy, value construction, ownership and native data passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
