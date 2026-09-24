cmake_minimum_required(VERSION 3.25)
project(gamev2 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(/W4 /WX /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SFML 3 COMPONENTS Graphics Window Audio Network REQUIRED)

add_executable(gamev2
    src/main.cpp
    src/core/gene.cpp
    src/core/headless_runner.cpp
    src/core/creature.cpp
    src/core/evolution.cpp
    src/core/simulation.cpp
    src/world/world.cpp
    src/world/world_generator.cpp
    src/render/tile_renderer.cpp
    src/render/creature_assembler.cpp
    src/core/ecs/registry.cpp
    src/core/ecs/systems.cpp
)
target_include_directories(gamev2 PRIVATE
    include
    third_party/FastNoiseLite
)
target_link_libraries(gamev2 PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    nlohmann_json::nlohmann_json
    SFML::Graphics
    SFML::Window
    SFML::Audio
    SFML::Network
)