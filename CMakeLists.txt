cmake_minimum_required(VERSION 3.20)
project(SkeletalViewer C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output to build/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ── Dependencies ─────────────────────────────────────────────────────────────

# GLFW
find_package(glfw3 3.3 REQUIRED)

# GLM (header-only)
find_package(glm REQUIRED)

# OpenGL
find_package(OpenGL REQUIRED)

# glad (vendored — generated for OpenGL 4.1 Core)
add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)

# tinygltf (header-only — just needs include path)
add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE third_party/tinygltf)

# stb (header-only)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE third_party/stb)

# ── Sources ───────────────────────────────────────────────────────────────────

set(SOURCES
    src/main.cpp
    src/App.cpp
    src/Camera.cpp
    src/Shader.cpp
    src/GltfLoader.cpp
    src/Animator.cpp
    src/Renderer.cpp
)

add_executable(SkeletalViewer ${SOURCES})

target_include_directories(SkeletalViewer PRIVATE src)
target_compile_definitions(SkeletalViewer PRIVATE GLM_ENABLE_EXPERIMENTAL)

target_link_libraries(SkeletalViewer
    glad
    tinygltf
    stb
    glfw
    glm::glm
    OpenGL::GL
)

# Copy shaders next to executable so relative paths work
add_custom_command(TARGET SkeletalViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:SkeletalViewer>/shaders
)

# Copy assets next to executable
add_custom_command(TARGET SkeletalViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:SkeletalViewer>/assets
)
