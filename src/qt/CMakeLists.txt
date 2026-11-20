# The slicing engine is HFW, reached through its C ABI. The library is built by
# cargo rather than by this build, so the path is a cache variable and the build
# refreshes it before linking: a stale .a is the one way a symbol can disagree
# with the header this target compiles against.
set(NEXT_LAYER_HFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../HFW" CACHE PATH
    "HFW checkout whose C library this application links")
find_program(CARGO_EXECUTABLE cargo REQUIRED)
set(HFW_CAPI_LIBRARY "${NEXT_LAYER_HFW_DIR}/target/release/libhfw_capi.a")
add_custom_target(hfw-capi-build
    COMMAND "${CARGO_EXECUTABLE}" build --release -p hfw-capi
    WORKING_DIRECTORY "${NEXT_LAYER_HFW_DIR}"
    COMMENT "Building HFW's C library")
add_library(hfw-capi STATIC IMPORTED)
set_target_properties(hfw-capi PROPERTIES IMPORTED_LOCATION "${HFW_CAPI_LIBRARY}")
set_property(TARGET hfw-capi APPEND PROPERTY
    INTERFACE_INCLUDE_DIRECTORIES "${NEXT_LAYER_HFW_DIR}/crates/capi/include")

find_package(Qt6 REQUIRED COMPONENTS Quick Qml Quick3D Svg OpenGL Network WebEngineQuick)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)

set(CMAKE_AUTOMOC ON)

add_executable(next-layer-qt
    src/main.cpp
    src/MeshGeometry.cpp
    src/MeshGeometry.hpp
    src/FeatureColors.hpp
    src/Palette.hpp
    src/Host.cpp
    src/Host.hpp
    src/Session.cpp
    src/Session.hpp
    src/Calibration.cpp
    src/Calibration.hpp
    src/ConfigBoxJson.cpp
    src/ConfigBoxJson.hpp
    src/ForeignSettings.cpp
    src/ForeignSettings.hpp
    src/PlateDocument.cpp
    src/PlateDocument.hpp
    src/PlateScene.cpp
    src/PlateScene.hpp
    src/SceneSelftest.cpp
    src/SceneSelftest.hpp
    src/SettingsModel.cpp
    src/SettingsModel.hpp
    src/PrinterLink.cpp
    src/PrinterLink.hpp
    src/PresetStore.cpp
    src/PresetStore.hpp
    src/PresetImport.cpp
    src/PresetImport.hpp
    src/PresetFiles.hpp
    src/PresetYaml.hpp
    src/TransformController.cpp
    src/TransformController.hpp
    src/HfwEngine.cpp
    src/HfwEngine.hpp
    src/SliceController.cpp
    src/SliceController.hpp
    src/SliceRuntime.cpp
    src/SliceRuntime.hpp
    src/QtPlatform.cpp
    src/QtPlatform.hpp
    src/PreviewData.cpp
    src/PreviewData.hpp
    src/GcodeView.cpp
    src/GcodeView.hpp
    src/GcodeBrand.hpp
    src/PlateThumbnail.hpp
    src/Resources.hpp
    src/ThreeMfProject.cpp
    src/ThreeMfProject.hpp
    src/Scratch.hpp
    src/Cut.cpp
    src/Cut.hpp
    src/Text.cpp
    src/Text.hpp
    src/Hollow.cpp
    src/Hollow.hpp
    src/Orient.cpp
    src/Orient.hpp
    src/Arranger.cpp
    src/Arranger.hpp
    src/Paint.cpp
    src/Paint.hpp
    src/ProjectFile.cpp
    src/ProjectFile.hpp
    src/GlewWrap.cpp
    src/Toolpath.cpp
    src/Toolpath.hpp
)

qt_add_resources(next-layer-qt "app_assets"
    PREFIX "/next-layer"
    BASE assets
    FILES assets/base-config.json
          assets/engine-capabilities.json
          assets/settings-layout.json
          assets/fonts/MaterialSymbolsOutlined.ttf
          assets/bed/voron24_350_plate.stl
          assets/bed/voron24_350_bed.png
          assets/bed/voron24_350_logo.svg
          assets/brand/next-layer-mark.svg
          assets/brand/next-layer-hero.png
          assets/brand/next-layer-printer.png
)

# The generic filament library: one preset per material, shipped as the engine reads
# it and copied into a machine's bundle when that machine is created or first seen.
file(GLOB FILAMENT_LIBRARY CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/filaments/*.yaml")
qt_add_resources(next-layer-qt "filament_library"
    PREFIX "/next-layer/filaments"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/filaments"
    FILES ${FILAMENT_LIBRARY}
)

# The application version. `tools/check`, `tools/package` and `tools/release` read
# it from this line, the C++ and Rust halves both take it from the variable, so a
# release bump is one edit.
set(NEXT_LAYER_VERSION "0.1.2")

# The application backend is a cxx-qt crate: it builds the Rust static library
# and the generated `NextLayer` QML module, and the executable links both.
set(NEXT_LAYER_APP_CRATE "${CMAKE_CURRENT_SOURCE_DIR}/../app")
set(NEXT_LAYER_APP_TARGET_DIR "${CMAKE_BINARY_DIR}/next-layer-app")
set(NEXT_LAYER_APP_LIB "${NEXT_LAYER_APP_TARGET_DIR}/release/libnext_layer_app.a")
file(GLOB_RECURSE NEXT_LAYER_APP_SOURCES CONFIGURE_DEPENDS
    "${NEXT_LAYER_APP_CRATE}/src/*.rs"
    "${NEXT_LAYER_APP_CRATE}/build.rs"
    "${NEXT_LAYER_APP_CRATE}/Cargo.toml"
)
add_custom_command(
    OUTPUT "${NEXT_LAYER_APP_LIB}"
    COMMAND "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${NEXT_LAYER_APP_TARGET_DIR}"
            "NEXT_LAYER_RESOURCES_DIR=${CMAKE_SOURCE_DIR}/resources"
            "NEXT_LAYER_VERSION=${NEXT_LAYER_VERSION}"
            cargo build --release --manifest-path "${NEXT_LAYER_APP_CRATE}/Cargo.toml"
    DEPENDS ${NEXT_LAYER_APP_SOURCES}
    COMMENT "Building the Rust application backend"
    VERBATIM
)
add_custom_target(next-layer-app-rust DEPENDS "${NEXT_LAYER_APP_LIB}")
add_dependencies(next-layer-qt next-layer-app-rust)

target_include_directories(next-layer-qt PRIVATE src)
target_include_directories(next-layer-qt PRIVATE "${NEXT_LAYER_APP_TARGET_DIR}/release/include")

target_compile_definitions(next-layer-qt PRIVATE
    NEXT_LAYER_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    NEXT_LAYER_APP_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources"
    NEXT_LAYER_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources"
    NEXT_LAYER_VERSION="${NEXT_LAYER_VERSION}"
    NEXT_LAYER_TAGLINE="a slicer for tool changers"
)

add_dependencies(next-layer-qt hfw-capi-build)
target_link_libraries(next-layer-qt PRIVATE
    hfw-capi
    "${NEXT_LAYER_APP_LIB}"
    pthread
    dl
    m
    util
    rt
    slic3r-app-cli
    slic3r-biz-arrange
    libpgcode
    miniz
    PkgConfig::FONTCONFIG
    Qt6::Quick
    Qt6::Quick3D
    Qt6::Svg
    Qt6::OpenGL
    Qt6::Network
    Qt6::WebEngineQuick
)

target_link_options(next-layer-qt PRIVATE
    -Wl,--wrap=glewInit
# The engine's dependency tree builds a static OpenSSL, and this executable
# exports dynamic symbols, so Qt's TLS plugin was binding to that copy while
# libssl.so.3 used the system one; loading the default CA bundle segfaulted.
# Hiding the static archives keeps every OpenSSL call on the system library.
    "LINKER:--exclude-libs,libssl.a"
    "LINKER:--exclude-libs,libcrypto.a"
)

# STEP goes through the engine's OCCT wrapper, which is a module library it looks
# for beside the executable. Built without it beside the binary, every STEP file
# fails with "Cannot load OCCTWrapper.so" however valid it is.
if(TARGET OCCTWrapper)
    add_dependencies(next-layer-qt OCCTWrapper)
    add_custom_command(TARGET next-layer-qt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:OCCTWrapper> $<TARGET_FILE_DIR:next-layer-qt>/
        COMMENT "Copying the OCCT wrapper beside the app, for STEP loading")
endif()
