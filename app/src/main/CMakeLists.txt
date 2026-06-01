cmake_minimum_required(VERSION 3.22.1)
project(whisper_android)

set(NATIVE_APP_GLUE ${ANDROID_NDK}/sources/android/native_app_glue)

set(VCE_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/../../../vulkan_canvas_engine)
set(VCE_CPP  ${VCE_DIR}/app/src/main/cpp)
set(VFE_MAIN ${VCE_DIR}/vulkan_font_engine/app/src/main)
set(VFE_CPP  ${VFE_MAIN}/cpp)

# FreeType — bundled inside vulkan_font_engine submodule
set(FT_DISABLE_ZLIB      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG       ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ  ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI    ON CACHE BOOL "" FORCE)
add_subdirectory(${VFE_MAIN}/freetype ${CMAKE_BINARY_DIR}/freetype)

# whisper.cpp — CPU only, minimal build
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP            OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN            OFF CACHE BOOL "" FORCE)
set(GGML_METAL             OFF CACHE BOOL "" FORCE)
set(GGML_CUDA              OFF CACHE BOOL "" FORCE)
add_subdirectory(cpp/whisper.cpp whisper_build)

# Copy pre-compiled SPIR-V shaders from font engine assets into this app's assets
set(SHADER_NAMES composite_vert composite_frag tiling coverage msdf_vert msdf_frag)
set(SHADER_OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})
foreach(SHADER ${SHADER_NAMES})
    configure_file(
        ${VFE_MAIN}/assets/shaders/${SHADER}.spv
        ${SHADER_OUT_DIR}/${SHADER}.spv
        COPYONLY
    )
endforeach()

add_library(whisper_android SHARED
    cpp/main.cc
    cpp/app.cc
    cpp/ui.cc
    cpp/audio.cc
    cpp/transcribe.cc
    ${VCE_CPP}/canvas.cc
    ${VCE_CPP}/textarea.cc
    ${VFE_CPP}/renderer.cc
    ${VFE_CPP}/font.cc
    ${VFE_CPP}/glyphs.cc
    ${VFE_CPP}/msdf.cc
    ${NATIVE_APP_GLUE}/android_native_app_glue.c
)

target_include_directories(whisper_android PRIVATE
    ${NATIVE_APP_GLUE}
    cpp/
    ${VFE_CPP}
    ${VCE_CPP}
    ${VFE_MAIN}/freetype/include
    cpp/whisper.cpp/
    cpp/whisper.cpp/include/
)

target_compile_options(whisper_android PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++17>
    -O3
    -ffast-math
    -flto
)

target_link_libraries(whisper_android
    whisper
    freetype
    android
    log
    vulkan
    aaudio
)
