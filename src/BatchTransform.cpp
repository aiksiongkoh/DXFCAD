#include <DXFCAD/BatchTransform.h>
#include <cmath>
#include <stdexcept>

namespace DXFCAD {
BatchTransformPlan::BatchTransformPlan(const std::vector<BatchTransformInput>& inputs,
    const std::string& operation,Vector3D offset,double value,Vector3D pivot,bool allowIdentity)
{
    if (inputs.empty()) throw std::invalid_argument("Select DXFCAD objects to transform");
    std::string op=operation;
    if (op=="translated") op="move";
    if (op=="rotated") op="rotate";
    if (op=="scaled") op="scale";
    if (op=="mirrored") op="mirror";
    if (op!="move" && op!="copy" && op!="rotate" && op!="scale" && op!="mirror")
        throw std::invalid_argument("Unknown batch transformation: "+operation);
    copies_=op=="copy";
    if (op=="move" || copies_) {
        if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z))
            throw std::invalid_argument("Displacement must be finite");
        if (op=="move" && !allowIdentity) displacement({},offset);
    }
    records_.reserve(inputs.size());
    for (const auto& input : inputs) {
        const auto entity=parseEntity(input.raw,input.context);
        // Validate the original even when a transform could conceal a defect.
        entity->translated({0,0,0});
        if (op=="move") records_.push_back(entity->translated(offset));
        else if (copies_) records_.push_back(entity->copied(offset));
        else if (op=="rotate") records_.push_back(entity->rotated(value,pivot));
        else if (op=="scale") records_.push_back(entity->scaled(value,pivot));
        else records_.push_back(entity->mirrored(value,pivot));
    }
}
}
