#include <Python.h>
#include <DXFCAD/DXFDocumentObject.h>
#include <DXFCAD/DXFEntityObject.h>
#include <App/Document.h>
#include <QByteArray>
#include <stdexcept>

namespace DXFCAD {
PROPERTY_SOURCE(DXFCAD::DXFDocumentObject, App::DocumentObjectGroup)

DXFDocumentObject::DXFDocumentObject()
{
    constexpr auto output=static_cast<App::PropertyType>(App::Prop_ReadOnly | App::Prop_Output);
    ADD_PROPERTY_TYPE(DXFSourceData, (""), "DXF", App::Prop_Hidden,
                      "Base64-encoded authoritative DXF drawing snapshot");
    ADD_PROPERTY_TYPE(SourceFile, (""), "DXF", App::Prop_ReadOnly,
                      "Original source path for provenance; not needed for restoration");
    ADD_PROPERTY_TYPE(SourceUnitCode, (4), "DXF", output, "Source DXF insertion-unit code");
    ADD_PROPERTY_TYPE(ImportWarnings, (std::vector<std::string>{}), "DXF", output,
                      "Warnings from interpreting the retained source snapshot");
    ADD_PROPERTY_TYPE(DisplayNotes, ("Source coordinates; widths where present; substitute font for TEXT/MTEXT"),
                      "DXF", App::Prop_ReadOnly, "Drawing display conventions");
    setDrawing(DXFDocument::empty(4));
}

void DXFDocumentObject::setDrawing(const DXFDocument& source, const std::string& sourceFile)
{
    // Complete validation and encoding before mutating persistent properties.
    const auto bytes=source.serialize();
    const auto validated=DXFDocument::parse(bytes);
    const auto encoded=QByteArray(bytes.data(),static_cast<qsizetype>(bytes.size())).toBase64();
    DXFSourceData.setValue(encoded.constData());
    SourceFile.setValue(sourceFile);
    SourceUnitCode.setValue(validated.insertionUnits);
    ImportWarnings.setValues(validated.warnings);
}

std::string DXFDocumentObject::sourceBytes() const
{
    const auto decoded=QByteArray::fromBase64Encoding(QByteArray(DXFSourceData.getValue()),
                                                     QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded) throw std::invalid_argument("Invalid Base64 DXFSourceData");
    return {decoded.decoded.constData(),static_cast<std::size_t>(decoded.decoded.size())};
}

DXFDocument DXFDocumentObject::drawing() const
{
    return DXFDocument::parse(sourceBytes());
}

std::string DXFDocumentObject::currentBytes() const
{
    if (!getDocument()) throw std::invalid_argument("Drawing is not in a document");
    std::vector<std::pair<int,std::string>> records;
    for (const auto* object : getDocument()->getObjects()) {
        const auto* owner=dynamic_cast<const App::PropertyLinkHidden*>(object->getPropertyByName("DXFSourceRoot"));
        if (!owner || owner->getValue() != this) continue;
        const auto* data=dynamic_cast<const App::PropertyString*>(object->getPropertyByName("DXFData"));
        if (!data) continue;
        const auto* index=dynamic_cast<const App::PropertyInteger*>(object->getPropertyByName("DXFSourceIndex"));
        const auto* feature=dynamic_cast<const Part::Feature*>(object);
        if (!index || !feature) throw std::invalid_argument("Drawing has an invalid DXF entity");
        if (!feature->Placement.getValue().isIdentity())
            throw std::invalid_argument("Reset FreeCAD Placement before DXFCAD export; use DXFCAD transforms for native coordinates");
        records.emplace_back(index->getValue(),data->getValue());
    }
    return drawing().serializeCurrent(records);
}

short DXFDocumentObject::mustExecute() const
{
    return DXFSourceData.isTouched() ? 1 : App::DocumentObjectGroup::mustExecute();
}

App::DocumentObjectExecReturn* DXFDocumentObject::execute()
{
    try {
        const auto source=drawing();
        SourceUnitCode.setValue(source.insertionUnits);
        ImportWarnings.setValues(source.warnings);
        return App::DocumentObjectGroup::execute();
    } catch (const std::exception& error) {
        return new App::DocumentObjectExecReturn(error.what(),this);
    }
}

void DXFDocumentObject::onDocumentRestored()
{
    App::DocumentObjectGroup::onDocumentRestored();
    touch();
}
}
