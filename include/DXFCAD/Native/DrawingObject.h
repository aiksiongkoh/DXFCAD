#pragma once
#include <App/DocumentObjectGroup.h>
#include <App/PropertyFile.h>
#include <App/PropertyStandard.h>
#include <DXFCAD/Drawing.h>

namespace DXFCAD {
// FreeCAD persistence adapter. The independent Drawing remains responsible
// for DXF semantics; entities are FreeCAD-owned children of layer groups.
class DrawingObject final : public App::DocumentObjectGroup {
    PROPERTY_HEADER_WITH_OVERRIDE(DXFCAD::DrawingObject);
public:
    DrawingObject();
    App::PropertyString DXFSourceData;
    App::PropertyFile SourceFile;
    App::PropertyInteger SourceUnitCode;
    App::PropertyStringList ImportWarnings;
    App::PropertyString DisplayNotes;

    void setDrawing(const Drawing& drawing, const std::string& sourceFile = "");
    std::string sourceBytes() const;
    Drawing drawing() const;
    std::string currentBytes() const;
    short mustExecute() const override;
    App::DocumentObjectExecReturn* execute() override;
    void onDocumentRestored() override;
};
}
