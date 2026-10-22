cmake_minimum_required(VERSION 3.20)

project(DXFCAD VERSION 0.1.0 LANGUAGES CXX)

# The domain layer deliberately has no GUI or downstream workbench dependency.
add_library(DXFCADDomain STATIC
    src/LWPolyline.cpp
    src/LWPolylineEditing.cpp
    src/LWPolylineWorldGeometry.cpp
    src/LWPolylineWidths.cpp
    src/OCSFrame.cpp
    src/DXFDocument.cpp
    src/Insert.cpp
    src/Drafting.cpp
    src/TrimExtend.cpp
    src/Contour.cpp
    src/BatchTransform.cpp
    src/CornerEdit.cpp
    src/SnapGeometry.cpp
    src/PolylineOffset.cpp
    src/LineFillet.cpp
    src/LineChamfer.cpp
    src/ArcCreation.cpp
)
add_library(DXFCAD::Domain ALIAS DXFCADDomain)
target_include_directories(DXFCADDomain PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(DXFCADDomain PUBLIC cxx_std_17)
set_target_properties(DXFCADDomain PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if(MSVC)
    target_compile_options(DXFCADDomain PRIVATE /W4 /permissive-)
else()
    target_compile_options(DXFCADDomain PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(DXFCAD_BUILD_PYTHON "Build the native Python domain module" OFF)
option(DXFCAD_BUILD_NATIVE_OBJECTS "Build native FreeCAD Part::Feature document objects" OFF)
if(DXFCAD_BUILD_NATIVE_OBJECTS AND NOT DXFCAD_BUILD_PYTHON)
    message(FATAL_ERROR "Native document objects require DXFCAD_BUILD_PYTHON=ON")
endif()
if(DXFCAD_BUILD_PYTHON)
    # Select the Python SDK matching the intended FreeCAD runtime. Never fetch
    # dependencies or rely on a sibling FreeCAD/XynaCAD source checkout.
    find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development.Module)
    set(PYBIND11_FINDPYTHON ON)
    find_package(pybind11 3.0 CONFIG REQUIRED)
    pybind11_add_module(DXFCAD MODULE src/PythonBindings.cpp src/FreeCADBindings.cpp)
    target_link_libraries(DXFCAD PRIVATE DXFCAD::Domain)
    if(DXFCAD_BUILD_NATIVE_OBJECTS)
        include(cmake/NativeFreeCAD.cmake)
    endif()
    # Install a complete workbench folder to an explicitly selected prefix.
    install(TARGETS DXFCAD LIBRARY DESTINATION . RUNTIME DESTINATION .)
    install(FILES Init.py InitGui.py dxfcad_workbench.py dxfcad_commands.py
        dxfcad_import.py dxfcad_objects.py dxfcad_edit.py dxfcad_export.py dxfcad_polyline_ui.py
        dxfcad_polyline_view.py dxfcad_layout_ui.py dxfcad_line_ui.py dxfcad_circle_ui.py
        dxfcad_move_ui.py dxfcad_transform_ui.py dxfcad_rectangle_ui.py
        dxfcad_drafting_ui.py dxfcad_text_ui.py dxfcad_view_snap.py dxfcad_arc_ui.py dxfcad_contour_ui.py
        DESTINATION .)
endif()

set(DXFCAD_FREECAD_PYTHON_EXECUTABLE "" CACHE FILEPATH
    "Optional FreeCAD Python interpreter for native module compatibility tests")
if(DXFCAD_FREECAD_PYTHON_EXECUTABLE AND NOT DXFCAD_BUILD_PYTHON)
    message(FATAL_ERROR "FreeCAD Python tests require DXFCAD_BUILD_PYTHON=ON")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(DXFCADLWPolylineTests tests/LWPolylineTests.cpp)
    target_link_libraries(DXFCADLWPolylineTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.LWPolyline COMMAND DXFCADLWPolylineTests)
    add_executable(DXFCADLWPolylineGeometryTests tests/LWPolylineGeometryTests.cpp)
    target_link_libraries(DXFCADLWPolylineGeometryTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.LWPolylineGeometry COMMAND DXFCADLWPolylineGeometryTests)
    add_executable(DXFCADLWPolylineWorldTests tests/LWPolylineWorldTests.cpp)
    target_link_libraries(DXFCADLWPolylineWorldTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.LWPolylineWorld COMMAND DXFCADLWPolylineWorldTests)
    add_executable(DXFCADDocumentTests tests/DXFDocumentTests.cpp)
    target_link_libraries(DXFCADDocumentTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.Document COMMAND DXFCADDocumentTests)
    add_executable(DXFCADMTextTests tests/MTextTests.cpp)
    target_link_libraries(DXFCADMTextTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.MText COMMAND DXFCADMTextTests)
    add_executable(DXFCADDimensionTests tests/DimensionTests.cpp)
    target_link_libraries(DXFCADDimensionTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.Dimension COMMAND DXFCADDimensionTests)
    add_executable(DXFCADInsertTests tests/InsertTests.cpp)
    target_link_libraries(DXFCADInsertTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.Insert COMMAND DXFCADInsertTests)
    add_executable(DXFCADContourTests tests/ContourTests.cpp)
    add_executable(DXFCADBatchTransformTests tests/BatchTransformTests.cpp)
    target_link_libraries(DXFCADBatchTransformTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.BatchTransform COMMAND DXFCADBatchTransformTests)
    target_link_libraries(DXFCADContourTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.Contour COMMAND DXFCADContourTests)
    add_executable(DXFCADViewportTests tests/ViewportTests.cpp)
    target_link_libraries(DXFCADViewportTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.Viewport COMMAND DXFCADViewportTests)
    add_executable(DXFCADDraftingTests tests/DraftingTests.cpp)
    target_link_libraries(DXFCADDraftingTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.Drafting COMMAND DXFCADDraftingTests)
    add_executable(DXFCADTrimExtendTests tests/TrimExtendTests.cpp)
    target_link_libraries(DXFCADTrimExtendTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.TrimExtend COMMAND DXFCADTrimExtendTests)
    add_executable(DXFCADSnapGeometryTests tests/SnapGeometryTests.cpp)
    target_link_libraries(DXFCADSnapGeometryTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.SnapGeometry COMMAND DXFCADSnapGeometryTests)
    add_executable(DXFCADPolylineOffsetTests tests/PolylineOffsetTests.cpp)
    target_link_libraries(DXFCADPolylineOffsetTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.PolylineOffset COMMAND DXFCADPolylineOffsetTests)
    add_executable(DXFCADLineFilletTests tests/LineFilletTests.cpp)
    add_executable(DXFCADCornerEditTests tests/CornerEditTests.cpp)
    target_link_libraries(DXFCADCornerEditTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.CornerEdit COMMAND DXFCADCornerEditTests)
    target_link_libraries(DXFCADLineFilletTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.LineFillet COMMAND DXFCADLineFilletTests)
    add_executable(DXFCADLineChamferTests tests/LineChamferTests.cpp)
    target_link_libraries(DXFCADLineChamferTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.LineChamfer COMMAND DXFCADLineChamferTests)
    add_executable(DXFCADArcCreationTests tests/ArcCreationTests.cpp)
    target_link_libraries(DXFCADArcCreationTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.ArcCreation COMMAND DXFCADArcCreationTests)
    add_executable(DXFCADRectangleTests tests/RectangleTests.cpp)
    target_link_libraries(DXFCADRectangleTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.Rectangle COMMAND DXFCADRectangleTests)
    add_executable(DXFCADMirrorTests tests/MirrorTests.cpp)
    target_link_libraries(DXFCADMirrorTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.Mirror COMMAND DXFCADMirrorTests)
    add_executable(DXFCADScaleTests tests/ScaleTests.cpp)
    target_link_libraries(DXFCADScaleTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.Scale COMMAND DXFCADScaleTests)
    add_executable(DXFCADRotationTests tests/RotationTests.cpp)
    target_link_libraries(DXFCADRotationTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.Rotation COMMAND DXFCADRotationTests)
    add_executable(DXFCADMoveTests tests/MoveTests.cpp)
    target_link_libraries(DXFCADMoveTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.Move COMMAND DXFCADMoveTests)
    add_executable(DXFCADCircleCreationTests tests/CircleCreationTests.cpp)
    target_link_libraries(DXFCADCircleCreationTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.CircleCreation COMMAND DXFCADCircleCreationTests)
    add_executable(DXFCADLineCreationTests tests/LineCreationTests.cpp)
    target_link_libraries(DXFCADLineCreationTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.LineCreation COMMAND DXFCADLineCreationTests)
    add_executable(DXFCADLWPolylineEditingTests tests/LWPolylineEditingTests.cpp)
    target_link_libraries(DXFCADLWPolylineEditingTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.LWPolylineEditing COMMAND DXFCADLWPolylineEditingTests)
    add_executable(DXFCADWidthTests tests/LWPolylineWidthTests.cpp)
    target_link_libraries(DXFCADWidthTests PRIVATE DXFCAD::Domain)
    add_test(NAME DXFCAD.LWPolylineWidths COMMAND DXFCADWidthTests)
    if(DXFCAD_BUILD_PYTHON)
        add_test(NAME DXFCAD.WorkbenchStartup
            COMMAND "${Python_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_workbench_startup.py"
        )
        add_test(NAME DXFCAD.PythonLWPolyline
            COMMAND "${Python_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_lwpolyline.py"
                "$<TARGET_FILE_DIR:DXFCAD>"
        )
        if(DXFCAD_BUILD_NATIVE_OBJECTS)
            add_test(NAME DXFCAD.NativeTextObject
                COMMAND "${Python_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_text_object.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.NativeCircleObject
                COMMAND "${Python_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_segment_object.py"
                    "$<TARGET_FILE_DIR:DXFCAD>" CIRCLE)
            add_test(NAME DXFCAD.NativeLineObject
                COMMAND "${Python_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_segment_object.py"
                    "$<TARGET_FILE_DIR:DXFCAD>" LINE)
            add_test(NAME DXFCAD.NativeArcObject
                COMMAND "${Python_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_segment_object.py"
                    "$<TARGET_FILE_DIR:DXFCAD>" ARC)
            add_test(NAME DXFCAD.NativeLWPolylineObject
                COMMAND "${Python_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_object.py"
                    "$<TARGET_FILE_DIR:DXFCAD>"
            )
        endif()
        if(DXFCAD_FREECAD_PYTHON_EXECUTABLE)
            add_test(NAME DXFCAD.FreeCADPolylineCornerPlan
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_polyline_corner_plan.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADContourOperations
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_contour_operations.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADTextEditSession
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_text_edit_session.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADTrimPlan
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_trim_plan.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADOffsetPlan
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_offset_plan.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADEntityCopy
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_entity_copy.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADEntityUpdate
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_entity_update.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADEntityCreation
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_entity_creation.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADCornerEdit
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_corner_edit.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADBatchTransform
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_batch_transform.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADPopulation
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_population.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADLayers
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_layers.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADDocumentObject
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_document_object.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADContour
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_contour.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADInsert
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_insert.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADDimension
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_dimension.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADMText
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mtext.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADPolylineEditing
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_polyline_editing.py"
                    "$<TARGET_FILE_DIR:DXFCAD>")
            add_test(NAME DXFCAD.FreeCADPythonLWPolyline
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_freecad_bindings.py"
                    "$<TARGET_FILE_DIR:DXFCAD>"
            )
            add_test(NAME DXFCAD.FreeCADImport
                COMMAND "${DXFCAD_FREECAD_PYTHON_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_freecad_import.py"
                    "$<TARGET_FILE_DIR:DXFCAD>"
            )
        endif()
    endif()
endif()
