cmake_minimum_required(VERSION 3.25)

project(VoxelCube VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors." OFF)
option(VC_USE_SPDLOG "Use spdlog when available." ON)
option(VC_USE_ENTT "Use EnTT when available." ON)

set(VC_ENGINE_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/engine/include")

if(VC_USE_SPDLOG)
    find_package(spdlog CONFIG QUIET)
endif()

if(VC_USE_ENTT)
    find_package(EnTT CONFIG QUIET)
endif()

add_library(VoxelCube
    engine/src/core/Application.cpp
    engine/src/core/ApplicationConfig.cpp
    engine/src/core/Config.cpp
    engine/src/core/FileSystem.cpp
    engine/src/core/Input.cpp
    engine/src/core/JobSystem.cpp
    engine/src/core/Log.cpp
    engine/src/core/Memory.cpp
    engine/src/core/Window.cpp
    engine/src/renderer/DX11Buffer.cpp
    engine/src/renderer/DX11DepthPrePass.cpp
    engine/src/renderer/DX11GBuffer.cpp
    engine/src/renderer/DX11GeometryBuffer.cpp
    engine/src/renderer/DX11LightingPass.cpp
    engine/src/renderer/DX11PipelineState.cpp
    engine/src/renderer/DX11ShaderCompiler.cpp
    engine/src/renderer/DX11ContextSync.cpp
    engine/src/renderer/DX11Device.cpp
    engine/src/renderer/DX11RenderTargets.cpp
    engine/src/renderer/DX11ShadowMap.cpp
    engine/src/renderer/DX11SwapChain.cpp
    engine/src/renderer/DX11TransparentPass.cpp
    engine/src/renderer/DX11TextureAtlas.cpp
    engine/src/renderer/DX11Texture2D.cpp
)

target_include_directories(VoxelCube
    PUBLIC
        "${VC_ENGINE_PUBLIC_INCLUDE_DIR}"
)

if(TARGET spdlog::spdlog_header_only)
    target_link_libraries(VoxelCube PRIVATE spdlog::spdlog_header_only)
    target_compile_definitions(VoxelCube PRIVATE VC_HAS_SPDLOG)
elseif(TARGET spdlog::spdlog)
    target_link_libraries(VoxelCube PRIVATE spdlog::spdlog)
    target_compile_definitions(VoxelCube PRIVATE VC_HAS_SPDLOG)
endif()

if(TARGET EnTT::EnTT)
    target_link_libraries(VoxelCube PUBLIC EnTT::EnTT)
    target_compile_definitions(VoxelCube PUBLIC VC_HAS_ENTT)
elseif(TARGET EnTT::EnTTHeaderOnly)
    target_link_libraries(VoxelCube PUBLIC EnTT::EnTTHeaderOnly)
    target_compile_definitions(VoxelCube PUBLIC VC_HAS_ENTT)
endif()

if(WIN32)
    target_link_libraries(VoxelCube PRIVATE d3d11 dxgi d3dcompiler)
endif()

target_compile_definitions(VoxelCube
    PUBLIC
        $<$<PLATFORM_ID:Windows>:VC_PLATFORM_WINDOWS>
        $<$<CXX_COMPILER_ID:MSVC>:NOMINMAX>
        $<$<CXX_COMPILER_ID:MSVC>:WIN32_LEAN_AND_MEAN>
)

if(MSVC)
    target_compile_options(VoxelCube PRIVATE /W4 /permissive- /EHsc)
    if(VC_WARNINGS_AS_ERRORS)
        target_compile_options(VoxelCube PRIVATE /WX)
    endif()
else()
    target_compile_options(VoxelCube PRIVATE -Wall -Wextra -Wpedantic)
    if(VC_WARNINGS_AS_ERRORS)
        target_compile_options(VoxelCube PRIVATE -Werror)
    endif()
endif()

add_executable(VoxelCubeSandbox
    sandbox/src/Main.cpp
)

target_link_libraries(VoxelCubeSandbox
    PRIVATE
        VoxelCube
)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT VoxelCubeSandbox)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    engine/src/core/Application.cpp
    engine/src/core/ApplicationConfig.cpp
    engine/src/core/Config.cpp
    engine/src/core/FileSystem.cpp
    engine/src/core/Input.cpp
    engine/src/core/JobSystem.cpp
    engine/src/core/Log.cpp
    engine/src/core/Memory.cpp
    engine/src/core/Window.cpp
    engine/src/renderer/DX11Buffer.cpp
    engine/src/renderer/DX11DepthPrePass.cpp
    engine/src/renderer/DX11GBuffer.cpp
    engine/src/renderer/DX11GeometryBuffer.cpp
    engine/src/renderer/DX11LightingPass.cpp
    engine/src/renderer/DX11PipelineState.cpp
    engine/src/renderer/DX11ShaderCompiler.cpp
    engine/src/renderer/DX11ContextSync.cpp
    engine/src/renderer/DX11Device.cpp
    engine/src/renderer/DX11RenderTargets.cpp
    engine/src/renderer/DX11ShadowMap.cpp
    engine/src/renderer/DX11SwapChain.cpp
    engine/src/renderer/DX11TransparentPass.cpp
    engine/src/renderer/DX11TextureAtlas.cpp
    engine/src/renderer/DX11Texture2D.cpp
    sandbox/src/Main.cpp
)
