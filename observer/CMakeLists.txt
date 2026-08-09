cmake_minimum_required(VERSION 3.24)
project(TheObserver VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(OBSERVER_WITH_WINDOW "Build with GLFW windowed presentation" ON)
option(OBSERVER_VALIDATION  "Enable Vulkan validation layers in debug" OFF)

# ------------------------------------------------------------------ deps ----
include(FetchContent)

# Vulkan headers: system if present, otherwise fetch (no SDK required; volk
# loads the driver at runtime, so we never link against a Vulkan loader).
find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h)
if(NOT VULKAN_INCLUDE_DIR)
  FetchContent_Declare(vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG v1.3.290 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(vulkan_headers)
  set(VULKAN_INCLUDE_DIR ${vulkan_headers_SOURCE_DIR}/include)
endif()

if(OBSERVER_WITH_WINDOW)
  find_package(glfw3 3.3 QUIET)
  if(NOT glfw3_FOUND)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
      GIT_REPOSITORY https://github.com/glfw/glfw.git
      GIT_TAG 3.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glfw)
  endif()
endif()

# ---------------------------------------------------------------- shaders ---
# GLSL -> SPIR-V. If glslangValidator exists we rebuild; otherwise we use the
# committed .spv files in engine/shaders/spv so end users need no shader tools.
find_program(GLSLANG glslangValidator)
set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders)
set(SHADER_SPV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders/spv)
file(GLOB SHADER_SOURCES ${SHADER_SRC_DIR}/*.vert ${SHADER_SRC_DIR}/*.frag)
set(SPV_OUTPUTS "")
if(GLSLANG)
  foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(FN ${SHADER} NAME)
    set(SPV ${SHADER_SPV_DIR}/${FN}.spv)
    add_custom_command(OUTPUT ${SPV}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_SPV_DIR}
      COMMAND ${GLSLANG} -V ${SHADER} -o ${SPV}
      DEPENDS ${SHADER} COMMENT "GLSL->SPIR-V ${FN}")
    list(APPEND SPV_OUTPUTS ${SPV})
  endforeach()
  add_custom_target(shaders ALL DEPENDS ${SPV_OUTPUTS})
endif()

# ------------------------------------------------------------------ IRIS ----
add_library(iris STATIC
  engine/iris_vk.cpp
  engine/iris_gfx.cpp
  engine/iris_text.cpp
  engine/iris_audio.cpp
  engine/iris_platform.cpp
  third_party/volk/volk.c)
target_include_directories(iris PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/engine
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
  ${VULKAN_INCLUDE_DIR})
target_compile_definitions(iris PUBLIC VK_NO_PROTOTYPES)
if(WIN32)
  target_compile_definitions(iris PRIVATE VK_USE_PLATFORM_WIN32_KHR NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(OBSERVER_WITH_WINDOW)
  target_compile_definitions(iris PUBLIC IRIS_WITH_GLFW=1)
  target_link_libraries(iris PUBLIC glfw)
endif()
if(OBSERVER_VALIDATION)
  target_compile_definitions(iris PRIVATE IRIS_VALIDATION=1)
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(iris PUBLIC dl pthread m)
endif()
if(GLSLANG)
  add_dependencies(iris shaders)
endif()

# ------------------------------------------------------------ THE OBSERVER --
add_executable(observer
  game/main.cpp
  game/content.cpp
  game/world.cpp
  game/systems.cpp
  game/script.cpp
  game/ui.cpp)
target_include_directories(observer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/game)
target_link_libraries(observer PRIVATE iris)
if(WIN32)
  set_target_properties(observer PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
endif()

# ------------------------------------------------------------------ tests ---
enable_testing()
add_executable(observer_tests
  tests/test_systems.cpp
  game/content.cpp
  game/world.cpp
  game/systems.cpp)
target_include_directories(observer_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/game)
target_link_libraries(observer_tests PRIVATE iris)
target_compile_definitions(observer_tests PRIVATE OBSERVER_UNIT_TESTS=1)
add_test(NAME systems COMMAND observer_tests)

# ---------------------------------------------------------------- install ---
install(TARGETS observer RUNTIME DESTINATION .)
install(DIRECTORY assets DESTINATION .)
install(DIRECTORY content DESTINATION .)
install(DIRECTORY engine/shaders/spv DESTINATION shaders)

add_executable(iris_smoke tests/engine_smoke.cpp)
target_link_libraries(iris_smoke PRIVATE iris)
