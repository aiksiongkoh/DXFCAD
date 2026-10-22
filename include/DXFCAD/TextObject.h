#pragma once
#include <DXFCAD/DXFEntityObject.h>
#include <App/PropertyFile.h>

namespace DXFCAD {
class TextObject : public DXFEntityObject {
    PROPERTY_HEADER_WITH_OVERRIDE(DXFCAD::TextObject);
public:
    TextObject();
    App::PropertyFile TextFont;
    short mustExecute() const override;
protected:
    TextObject(const char* type, const char* initialData);
    const char* entityType() const override { return "TEXT"; }
    TopoDS_Shape buildShape(const DXFEntity& entity) const override;
    const char* shapeStatus() const override { return "Text outlines (substitute font)"; }
};
class MTextObject final : public TextObject {
    PROPERTY_HEADER_WITH_OVERRIDE(DXFCAD::MTextObject);
public:
    MTextObject();
protected:
    const char* entityType() const override { return "MTEXT"; }
};
class DimensionObject final : public TextObject {
    PROPERTY_HEADER_WITH_OVERRIDE(DXFCAD::DimensionObject);
public:
    DimensionObject();
    App::PropertyString DXFContext;
    short mustExecute() const override;
protected:
    const char* entityType() const override { return "DIMENSION"; }
    std::string entityContext() const override { return DXFContext.getValue(); }
    const char* shapeStatus() const override { return "Cached dimension picture (substitute font)"; }
};
class InsertObject final : public TextObject {
    PROPERTY_HEADER_WITH_OVERRIDE(DXFCAD::InsertObject);
public:
    InsertObject();
    App::PropertyString DXFContext;
    short mustExecute() const override;
protected:
    const char* entityType() const override { return "INSERT"; }
    std::string entityContext() const override { return DXFContext.getValue(); }
    const char* shapeStatus() const override { return "Block instance (substitute font for text)"; }
};
}
