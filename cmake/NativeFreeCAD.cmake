# An external FreeCAD source/build pair is read-only SDK input, never a subproject.
set(DXFCAD_FREECAD_SOURCE_DIR "" CACHE PATH "Matching FreeCAD source tree (SDK headers)")
set(DXFCAD_FREECAD_BUILD_DIR "" CACHE PATH "Matching compiled FreeCAD build tree")
set(DXFCAD_FREECAD_LIBPACK_DIR "" CACHE PATH "Matching FreeCAD dependency SDK")
foreach(required DXFCAD_FREECAD_SOURCE_DIR DXFCAD_FREECAD_BUILD_DIR DXFCAD_FREECAD_LIBPACK_DIR)
    if(NOT IS_DIRECTORY "${${required}}")
        message(FATAL_ERROR "Native document objects require ${required}")
    endif()
endforeach()

find_package(Qt6 REQUIRED COMPONENTS Core Xml)
find_package(OpenCASCADE REQUIRED COMPONENTS FoundationClasses ModelingData ModelingAlgorithms)
file(GLOB dxfcad_boost_roots LIST_DIRECTORIES TRUE "${DXFCAD_FREECAD_LIBPACK_DIR}/include/boost-*")
find_path(DXFCAD_BOOST_INCLUDE_DIR boost/version.hpp
    PATHS ${dxfcad_boost_roots} "${DXFCAD_FREECAD_LIBPACK_DIR}/include" REQUIRED)
find_library(DXFCAD_FREECAD_APP_LIBRARY NAMES FreeCADApp
    PATHS "${DXFCAD_FREECAD_BUILD_DIR}/src/App" "${DXFCAD_FREECAD_BUILD_DIR}/lib" REQUIRED NO_DEFAULT_PATH)
find_library(DXFCAD_FREECAD_BASE_LIBRARY NAMES FreeCADBase
    PATHS "${DXFCAD_FREECAD_BUILD_DIR}/src/Base" "${DXFCAD_FREECAD_BUILD_DIR}/lib" REQUIRED NO_DEFAULT_PATH)
find_library(DXFCAD_FREECAD_PART_LIBRARY NAMES Part
    PATHS "${DXFCAD_FREECAD_BUILD_DIR}/src/Mod/Part/App" "${DXFCAD_FREECAD_BUILD_DIR}/lib" REQUIRED NO_DEFAULT_PATH)

target_compile_definitions(DXFCAD PRIVATE DXFCAD_NATIVE_OBJECTS)
pybind11_add_module(_DXFCADObjects MODULE src/Native/Module.cpp src/Native/LWPolylineObject.cpp
    src/Native/DXFEntityObject.cpp src/Native/LineObject.cpp src/Native/ArcObject.cpp
    src/Native/CircleObject.cpp src/Native/TextObject.cpp)
target_link_libraries(_DXFCADObjects PRIVATE DXFCAD::Domain)
add_dependencies(DXFCAD _DXFCADObjects)
install(TARGETS _DXFCADObjects LIBRARY DESTINATION . RUNTIME DESTINATION .)
target_compile_definitions(_DXFCADObjects PRIVATE NOMINMAX PYCXX_6_2_COMPATIBILITY)
# Current FreeCAD development headers require C++23. The independent domain
# library retains its C++17 requirement.
target_compile_features(_DXFCADObjects PRIVATE cxx_std_23)
target_include_directories(_DXFCADObjects SYSTEM PRIVATE
    "${DXFCAD_FREECAD_SOURCE_DIR}/src"
    "${DXFCAD_FREECAD_BUILD_DIR}/src"
    "${DXFCAD_FREECAD_BUILD_DIR}"
    "${DXFCAD_FREECAD_SOURCE_DIR}/src/3rdParty"
    "${DXFCAD_FREECAD_SOURCE_DIR}/src/3rdParty/PyCXX"
    "${DXFCAD_FREECAD_SOURCE_DIR}/src/3rdParty/FastSignals/libfastsignals/include"
    "${DXFCAD_BOOST_INCLUDE_DIR}"
    "${OpenCASCADE_INCLUDE_DIR}")
target_link_libraries(_DXFCADObjects PRIVATE
    "${DXFCAD_FREECAD_APP_LIBRARY}" "${DXFCAD_FREECAD_BASE_LIBRARY}" "${DXFCAD_FREECAD_PART_LIBRARY}"
    Qt6::Core Qt6::Xml TKernel TKMath TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKBO TKBool TKOffset)
if(MSVC)
    target_compile_options(_DXFCADObjects PRIVATE /Zc:__cplusplus /bigobj /utf-8)
    target_compile_definitions(_DXFCADObjects PRIVATE _OCC64)
endif()
