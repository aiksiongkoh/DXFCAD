#include <DXFCAD/ChangeSet.h>
#include <DXFCAD/BatchTransform.h>
#include <DXFCAD/Contour.h>
#include <DXFCAD/CornerEdit.h>
#include <DXFCAD/Drafting.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

using namespace DXFCAD;
static_assert(std::is_abstract_v<ChangeSet>);
static_assert(std::has_virtual_destructor_v<ChangeSet>);
static_assert(std::is_convertible_v<BatchTransformChangeSet*, ChangeSet*>);
static_assert(std::is_convertible_v<ContourChangeSet*, ChangeSet*>);
static_assert(std::is_convertible_v<CornerChangeSet*, ChangeSet*>);

int main()
{
    try {
        const std::string first = "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n10\n21\n0\n";
        const std::string second = "0\nLINE\n8\n0\n10\n10\n20\n0\n11\n10\n21\n10\n";
        const auto source = Drawing::empty();
        const std::vector<std::pair<int, std::string>> live{{-1, first}, {-1, second}};
        const auto original = source.serialize();
        std::vector<std::unique_ptr<const ChangeSet>> changes;
        {
            const BatchTransformChangeSet transform({{first, ""}}, "move", {2, 3, 0});
            const ContourChangeSet contour(source, live, {0}, Contour::split(first, {4}));
            const auto result = Drafting::filletLines(first, second, 2);
            const CornerChangeSet corner(source, live, {0, 1},
                {result.first, result.second}, result.arc, "ARC");
            // Concrete copies and moves must still own results after these
            // originals leave scope. Base pointers own all three specializations.
            auto moved = transform;
            changes.push_back(std::make_unique<BatchTransformChangeSet>(std::move(moved)));
            changes.push_back(std::make_unique<ContourChangeSet>(contour));
            changes.push_back(std::make_unique<CornerChangeSet>(corner));
        }
        const auto& transform = dynamic_cast<const BatchTransformChangeSet&>(*changes[0]);
        const auto line = parseEntity(transform.records().at(0))->geometry().segments.at(0);
        if (line.start.x != 2 || line.start.y != 3 || line.end.x != 12 || line.end.y != 3)
            throw std::runtime_error("Transform result changed through base ownership");
        const auto& contour = dynamic_cast<const ContourChangeSet&>(*changes[1]);
        if (contour.records().size() != 2 ||
            Drawing::parse(contour.drawingBytes()).entities.size() != 3)
            throw std::runtime_error("Contour result did not survive concrete copy");
        const auto& corner = dynamic_cast<const CornerChangeSet&>(*changes[2]);
        if (corner.updates().size() != 2 || parseEntity(corner.connector())->type() != "ARC" ||
            Drawing::parse(corner.drawingBytes()).entities.size() != 3)
            throw std::runtime_error("Corner result did not survive concrete copy");
        if (source.serialize() != original)
            throw std::runtime_error("Change sets modified their source drawing");
        changes.clear(); // Destroy the concrete results through the public base.
        std::cout << "ChangeSet hierarchy, ownership, value semantics and source preservation passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
