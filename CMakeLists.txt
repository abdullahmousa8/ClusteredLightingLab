cmake_minimum_required(VERSION 3.20)
project(ClusteredLightingLab CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  add_compile_options(-Wall -Wextra)
  add_compile_definitions(NOMINMAX)
endif()

add_executable(ClusteredLightingLab
  src/main.cpp
  src/gl/gl_loader.cpp
  src/scene/SoALights.cpp
  src/renderer/ClusteredRenderer.cpp
  src/renderer/ClusterMath.cpp
)

# Correctness tests for the cluster culling math. No GPU required, so these can
# run in CI; they mirror the GLSL in src/renderer/Shaders.h.
add_executable(ClusterMathTests
  src/tests/ClusterMathTests.cpp
  src/renderer/ClusterMath.cpp
)

enable_testing()
add_test(NAME ClusterMath COMMAND ClusterMathTests)

target_include_directories(ClusteredLightingLab PRIVATE src)
target_include_directories(ClusterMathTests PRIVATE src)

# The test reads Shaders.h and ClusterMath.cpp from the source tree to check for
# GLSL/host drift, so it needs to know where those files live regardless of the
# working directory it is launched from.
target_compile_definitions(ClusterMathTests PRIVATE
  LAB_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src"
)
