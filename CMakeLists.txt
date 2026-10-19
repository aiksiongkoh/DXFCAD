cmake_minimum_required(VERSION 3.20)

project(DXFCAD VERSION 0.1.0 LANGUAGES CXX)

# The domain layer deliberately has no GUI or downstream workbench dependency.
add_library(DXFCADDomain STATIC
    src/LWPolyline.cpp
    src/LWPolylineWorldGeometry.cpp
    src/LWPolylineWidths.cpp
    src/OCSFrame.cpp
    src/DXFDocument.cpp
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
        dxfcad_import.py dxfcad_objects.py
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
