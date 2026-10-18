# kdenlive-morphcut - CMakeLists.txt
# Build of the MLT module, the RIFE engine and the test tool.
#
#   Copyright (C) 2026 Thorsten Schnebeck <thorsten.schnebeck@gmx.net>
#   Produced by Thorsten Schnebeck - the idea, the decisions, the testing.
#   Written by Anthropic Claude Opus 5.5 - AI generated content.
#
#   Free software under the GNU General Public License, version 3 or later.
#   There is no warranty, to the extent permitted by law. The full text is in
#   LICENSES/GPL-3.0-or-later.txt.
#
# SPDX-FileCopyrightText: (C) 2026 Thorsten Schnebeck <thorsten.schnebeck@gmx.net>
# SPDX-FileContributor: Anthropic Claude Opus 5.5 (AI generated content)
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.28)
project(kdenlive-morphcut VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(GNUInstallDirs)

# ---------------------------------------------------------------- MLT
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(MLT IMPORTED_TARGET mlt-framework-7)
endif()
if(MLT_FOUND)
    set(MLT_TARGET PkgConfig::MLT)
    pkg_get_variable(MLT_MODULE_DIR mlt-framework-7 moduledir)
    pkg_get_variable(MLT_DATA_DIR mlt-framework-7 mltdatadir)
else()
    # No libmlt-dev installed: use the bundled public headers and the runtime library.
    find_library(MLT_LIBRARY NAMES mlt-7 libmlt-7.so.7 REQUIRED)
    add_library(mlt_framework INTERFACE)
    target_include_directories(mlt_framework INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mlt)
    target_link_libraries(mlt_framework INTERFACE ${MLT_LIBRARY})
    set(MLT_TARGET mlt_framework)
    get_filename_component(_mlt_libdir ${MLT_LIBRARY} DIRECTORY)
    set(MLT_MODULE_DIR ${_mlt_libdir}/mlt-7)
    set(MLT_DATA_DIR /usr/share/mlt-7)
    message(STATUS "MLT dev package not found, using bundled headers with ${MLT_LIBRARY}")
endif()
if(NOT MLT_MODULE_DIR)
    set(MLT_MODULE_DIR ${CMAKE_INSTALL_FULL_LIBDIR}/mlt-7)
endif()
if(NOT MLT_DATA_DIR)
    set(MLT_DATA_DIR ${CMAKE_INSTALL_FULL_DATADIR}/mlt-7)
endif()

set(MORPHCUT_MODULE_DIR ${MLT_MODULE_DIR} CACHE PATH "Install directory of the MLT module")
set(MORPHCUT_DATA_DIR ${MLT_DATA_DIR}/morphcut CACHE PATH "Install directory for metadata and models")

# ---------------------------------------------------------------- ncnn
# Either point ncnn_DIR to an installed ncnn (with Vulkan), or let us build it.
find_package(ncnn QUIET)
if(NOT TARGET ncnn)
    include(FetchContent)
    message(STATUS "Building ncnn from source")
    set(NCNN_VULKAN ON CACHE BOOL "" FORCE)
    set(NCNN_SHARED_LIB OFF CACHE BOOL "" FORCE)
    set(NCNN_INSTALL_SDK OFF CACHE BOOL "" FORCE)
    set(NCNN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(NCNN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(NCNN_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
    set(NCNN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(NCNN_PYTHON OFF CACHE BOOL "" FORCE)
    # ncnn is linked statically; never install it or glslang
    set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(ncnn
        GIT_REPOSITORY https://github.com/Tencent/ncnn.git
        GIT_TAG 20260526
        GIT_SHALLOW TRUE
        GIT_SUBMODULES glslang
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(ncnn)
endif()

find_package(Threads REQUIRED)
find_package(OpenMP)

# ---------------------------------------------------------------- RIFE shaders
set(RIFE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rife)
set(RIFE_SHADERS
    rife_preproc rife_postproc rife_preproc_tta rife_postproc_tta
    rife_flow_tta_avg rife_v2_flow_tta_avg rife_v4_flow_tta_avg
    rife_flow_tta_temporal_avg rife_v2_flow_tta_temporal_avg rife_v4_flow_tta_temporal_avg
    rife_out_tta_temporal_avg rife_v4_timestep rife_v4_timestep_tta
    warp warp_pack4 warp_pack8)
set(RIFE_SHADER_HEADERS)
foreach(shader ${RIFE_SHADERS})
    set(header ${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader}.comp.hex.h)
    add_custom_command(
        OUTPUT ${header}
        COMMAND ${CMAKE_COMMAND} -DSHADER_SRC=${RIFE_DIR}/${shader}.comp -DSHADER_COMP_HEADER=${header}
                -P ${RIFE_DIR}/generate_shader_comp_header.cmake
        DEPENDS ${RIFE_DIR}/${shader}.comp
        VERBATIM)
    list(APPEND RIFE_SHADER_HEADERS ${header})
endforeach()
add_custom_target(rife_shaders DEPENDS ${RIFE_SHADER_HEADERS})

add_library(rife STATIC ${RIFE_DIR}/rife.cpp ${RIFE_DIR}/warp.cpp)
add_dependencies(rife rife_shaders)
target_include_directories(rife PUBLIC ${RIFE_DIR} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shaders)
target_link_libraries(rife PUBLIC ncnn Threads::Threads)
if(OpenMP_CXX_FOUND)
    target_link_libraries(rife PUBLIC OpenMP::OpenMP_CXX)
endif()
set_target_properties(rife PROPERTIES CXX_VISIBILITY_PRESET hidden)

# ---------------------------------------------------------------- MLT module
add_library(mltmorphcut MODULE
    src/factory.cpp
    src/transition_morphcut.cpp
    src/interpolator.cpp)
target_compile_definitions(mltmorphcut PRIVATE MORPHCUT_DATADIR="${MORPHCUT_DATA_DIR}")
target_link_libraries(mltmorphcut PRIVATE ${MLT_TARGET} rife)
set_target_properties(mltmorphcut PROPERTIES
    PREFIX "lib"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
# keep the statically linked ncnn/glslang symbols private
target_link_options(mltmorphcut PRIVATE -Wl,--exclude-libs,ALL -Wl,--no-undefined)

install(TARGETS mltmorphcut LIBRARY DESTINATION ${MORPHCUT_MODULE_DIR})
install(FILES data/transition_morphcut.yml DESTINATION ${MORPHCUT_DATA_DIR})
install(DIRECTORY models/ DESTINATION ${MORPHCUT_DATA_DIR}/models)
# Translations of the Kdenlive UI strings, merged into Kdenlive's catalog by install.sh
install(DIRECTORY po/ DESTINATION ${MORPHCUT_DATA_DIR}/po FILES_MATCHING PATTERN "*.po")

# ---------------------------------------------------------------- tools
option(MORPHCUT_BUILD_TOOLS "Build the command line test tool" ON)
if(MORPHCUT_BUILD_TOOLS)
    add_executable(morphcut-interp tools/morphcut_interp.cpp src/interpolator.cpp)
    target_include_directories(morphcut-interp PRIVATE src third_party/stb)
    target_link_libraries(morphcut-interp PRIVATE rife)
endif()
