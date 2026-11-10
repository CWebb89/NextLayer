find_package(Qt6 REQUIRED COMPONENTS Quick Qml Quick3D Svg OpenGL Network WebEngineQuick)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)

set(CMAKE_AUTOMOC ON)

add_executable(next-layer-qt
    src/main.cpp
    src/MeshGeometry.cpp
    src/MeshGeometry.hpp
    src/FeatureColors.hpp
    src/Palette.cpp
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
    src/GcodeBrand.cpp
    src/GcodeBrand.hpp
    src/PlateThumbnail.cpp
    src/PlateThumbnail.hpp
    src/Resources.cpp
    src/Resources.hpp
    src/ThreeMfProject.cpp
    src/ThreeMfProject.hpp
    src/Scratch.cpp
    src/Scratch.hpp
    src/Cut.cpp
    src/Cut.hpp
    src/Text.cpp
    src/Text.hpp
    src/Hollow.cpp
    src/Hollow.hpp
    src/Updater.cpp
    src/Updater.hpp
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
          assets/fonts/MaterialSymbolsOutlined.ttf
          assets/bed/voron24_350_plate.stl
          assets/bed/voron24_350_bed.png
          assets/bed/voron24_350_logo.svg
          assets/brand/next-layer-mark.svg
          assets/brand/next-layer-hero.png
)

# The generic filament library: one preset per material, shipped as the engine reads
# it and copied into a machine's bundle when that machine is created or first seen.
file(GLOB FILAMENT_LIBRARY CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/filaments/*.yaml")
qt_add_resources(next-layer-qt "filament_library"
    PREFIX "/next-layer/filaments"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/filaments"
    FILES ${FILAMENT_LIBRARY}
)

target_include_directories(next-layer-qt PRIVATE src)

target_compile_definitions(next-layer-qt PRIVATE
    NEXT_LAYER_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
    NEXT_LAYER_APP_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources"
    NEXT_LAYER_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources"
    NEXT_LAYER_VERSION="0.1.2"
    NEXT_LAYER_TAGLINE="a slicer for tool changers"
)

target_link_libraries(next-layer-qt PRIVATE
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
