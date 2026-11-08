#pragma once

#include "AdapterTypes.h"
#include <DXFCAD/BatchTransform.h>
#include <DXFCAD/EditPlan.h>
#include <DXFCAD/Vector3D.h>

namespace DXFCAD::FreeCAD {

class BatchObjectTransformPlan : public EditPlan {
    struct Selected {
        py::object object,root;
        std::string name,raw,context,font;
        int index;
        bool hasIndex;
    };
    py::object document_;
    std::vector<Selected> selected_;
    std::unique_ptr<BatchTransformChangeSet> domain_;
    std::vector<std::string> records_;
    std::map<std::string,py::object> roots_;
    std::map<std::string,std::string> sourceSnapshots_,liveSnapshots_,drawingBytes_;
    bool applied_=false;
public:
    ~BatchObjectTransformPlan() override = default;
    BatchObjectTransformPlan(const std::vector<py::object>& objects,const std::string& operation,
                             Vector3D offset,double value,Vector3D pivot,bool allowIdentity,
                             const std::vector<std::string>* copyRecords=nullptr);
    py::object document() const;
    bool copies() const;
    py::list objects() const;
    py::list previewRecords() const;
    py::dict drawingBytes() const;
    void validateCurrent(const std::string& kind="batch transform") const;
    std::vector<py::object> apply();
};

} // namespace DXFCAD::FreeCAD
