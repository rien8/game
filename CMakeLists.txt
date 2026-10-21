cmake_minimum_required(VERSION 3.28)

# Pin the compiler: clang++ is mandated, not the CMake platform default (cl.exe).
set(CMAKE_CXX_COMPILER clang++)

project(gamev2 LANGUAGES CXX)

# The VS generator selects cl.exe before this file is read, and silently
# overrides the pin above; without this guard the compiler constraint fails
# unnoticed instead of stopping the build.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^Clang")
    message(FATAL_ERROR "clang++ is mandated, got ${CMAKE_CXX_COMPILER_ID} "
        "(${CMAKE_CXX_COMPILER}); configure with -G \"Unix Makefiles\"")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ECS_SOURCES
    src/ecs.cpp
    src/ecs/diagnostic.cpp
    src/ecs/component.cpp
    src/ecs/storage.cpp
    src/ecs/thread_pool.cpp
    src/ecs/world.cpp
    src/ecs/command_buffer.cpp
    src/ecs/scheduler.cpp
)
add_library(ecs STATIC ${ECS_SOURCES})
target_include_directories(ecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(ECS_TEST_SOURCES
    tests/main.cpp
    tests/test_smoke.cpp
    tests/test_entity.cpp
    tests/test_diagnostic.cpp
    tests/test_component.cpp
    tests/test_storage.cpp
    tests/test_world.cpp
    tests/test_view.cpp
    tests/test_command_buffer.cpp
    tests/test_scheduler.cpp
    tests/test_integration.cpp
)
add_executable(ecs_tests ${ECS_TEST_SOURCES})
target_link_libraries(ecs_tests PRIVATE ecs)

set(WORLDGEN_SOURCES
    src/world/world.cpp
    src/world/rng.cpp
    src/world/terrain.cpp
    src/world/generate.cpp
    src/world/hydrology.cpp
    src/world/biomes.cpp
    src/world/strata.cpp
    src/world/caverns.cpp
    src/world/deposits.cpp
    src/world/ley.cpp
    src/world/veins.cpp
    src/world/domains.cpp
    src/world/land.cpp
    src/world/heavens.cpp
    src/world/tribulation.cpp
)
add_library(worldgen STATIC ${WORLDGEN_SOURCES})
target_include_directories(worldgen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(WORLDGEN_TEST_SOURCES
    tests/main.cpp
    tests/test_world_grid.cpp
    tests/test_world_rng.cpp
    tests/test_world_elevation.cpp
    tests/test_world_rainfall.cpp
    tests/test_world_temperature.cpp
    tests/test_world_hydrology.cpp
    tests/test_world_biomes.cpp
    tests/test_world_strata.cpp
    tests/test_world_caverns.cpp
    tests/test_world_deposits.cpp
    tests/test_world_determinism.cpp
    tests/test_world_spirit_tide.cpp
    tests/test_world_ley.cpp
    tests/test_world_veins.cpp
    tests/test_world_domains.cpp
    tests/test_world_land.cpp
    tests/test_world_heavens.cpp
    tests/test_world_tribulation.cpp
    tests/test_world_pipeline.cpp
)
add_executable(worldgen_tests ${WORLDGEN_TEST_SOURCES})
target_link_libraries(worldgen_tests PRIVATE worldgen)

add_executable(worldgen_demo src/demo/main.cpp)
target_link_libraries(worldgen_demo PRIVATE worldgen)

# Opt-in sanitizer build. Off by default so the pinned build stays untouched;
# the `active_` invariant in ThreadPool is a lifetime race that no ordinary
# assertion can observe, and reproducing it needs a UAF detector.
option(GAMEV2_ENABLE_ASAN "Also build the test suite with AddressSanitizer" OFF)
if(GAMEV2_ENABLE_ASAN)
    # Release 不是改目录级的 CMAKE_BUILD_TYPE 缓存变量，而是由子目录自己声明：
    # 单配置生成器按「目标定义所在的目录」解析 per-config 标志，所以只有把目标
    # 建在子目录里才能让它单独走 Release，而 ecs / ecs_tests 仍是本目录的
    # 配置。改缓存变量的旧写法有粘性：目录被 ASan 配置碰过一次后，即使随后用
    # -DGAMEV2_ENABLE_ASAN=OFF 重配，CMAKE_BUILD_TYPE 也仍是 Release，主目标
    # 于是跟着变成 -O3 -DNDEBUG，assert 全灭且无人察觉。
    #
    # 不能靠给该目标追加 Release 等效标志绕过：clang 只认第一个
    # -Xclang --dependent-lib=，per-config 标志总是排在 target_compile_options
    # 之前，追加的 msvcrt 会被前面的 msvcrtd 盖掉（实测）。
    #
    # 源文件路径必须绝对化：相对路径按消费它的目录解析，放在子目录里就找不到了。
    set(ECS_ASAN_SOURCES)
    foreach(f IN LISTS ECS_SOURCES ECS_TEST_SOURCES)
        list(APPEND ECS_ASAN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
    endforeach()
    add_subdirectory(cmake/asan)
endif()
