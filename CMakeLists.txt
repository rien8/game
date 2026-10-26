cmake_minimum_required(VERSION 3.28)

# Pin the compiler: clang++ is mandated, not the CMake platform default (cl.exe).
set(CMAKE_CXX_COMPILER clang++)

project(gamev2 LANGUAGES CXX)

# 默认构建类型：Debug。
#
# 不写这一段时，空的 CMAKE_BUILD_TYPE 会让配置结果依赖缓存里的历史状态——
# 缓存一旦被重置（换编译器、换生成器），构建口径就悄悄变了：少了 -O0、
# 少了 -g，也少了 _ITERATOR_DEBUG_LEVEL 的检查。后者在本机是真的：
# clang++ 用的是 MSVC 的 STL 头文件，Debug/Release 会改这个值——
# 手工编译单个文件时，链接器会因它与库里的值不匹配而拒绝链接。
# 这段把它固定下来。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型" FORCE)
endif()

# The VS generator selects cl.exe before this file is read, and silently
# overrides the pin above; without this guard the compiler constraint fails
# unnoticed instead of stopping the build.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^Clang")
    message(FATAL_ERROR "clang++ is mandated, got ${CMAKE_CXX_COMPILER_ID} "
        "(${CMAKE_CXX_COMPILER}); configure with -G \"Unix Makefiles\"")
endif()

# 归档工具与链接器必须与编译器同源。这台机器上别处还装着 GNU ar
# （Strawberry Perl 自带一个 2.25），CMake 探测到它时 lld-link 读不出
# 符号索引，链接会炸出上百条「符号未定义」——错在哪很难一眼看出。
#
# 这段必须在 project() 之后：之前 CMAKE_CXX_COMPILER 只是字面量
# clang++，推导不出目录。
#
# 找不到 llvm-ar / llvm-ranlib 时**退回 CMake 自带的探测**，不要硬报错：
# 换一套不带这两个工具的 clang 发行版时，configure 直接失败比构建失败
# 更难查（失败点在配置期，离真正的原因很远）。本机满足，钉住同源工具
# 仍然是首选——上面那段 GNU ar 的坑就是这么避开的。
get_filename_component(_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
if(EXISTS "${_compiler_dir}/llvm-ar.exe")
    set(CMAKE_AR "${_compiler_dir}/llvm-ar.exe")
endif()
if(EXISTS "${_compiler_dir}/llvm-ranlib.exe")
    set(CMAKE_RANLIB "${_compiler_dir}/llvm-ranlib.exe")
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

set(HISTORY_SOURCES
    src/history/ledger.cpp
    src/history/quota.cpp
)
add_library(history STATIC ${HISTORY_SOURCES})
target_include_directories(history PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(history PUBLIC worldgen)

set(HISTORY_TEST_SOURCES
    tests/main.cpp
    tests/test_history_ledger.cpp
)
add_executable(history_tests ${HISTORY_TEST_SOURCES})
target_link_libraries(history_tests PRIVATE history)

set(FACTION_SOURCES
    src/faction/setup.cpp
    src/faction/era.cpp
    src/faction/economy.cpp
    src/faction/systems.cpp
    src/faction/resolve.cpp
)
add_library(faction STATIC ${FACTION_SOURCES})
target_include_directories(faction PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(faction PUBLIC history ecs)

set(FACTION_TEST_SOURCES
    tests/main.cpp
    tests/test_faction_economy.cpp
    tests/test_faction_era.cpp
    tests/test_faction_determinism.cpp
)
add_executable(faction_tests ${FACTION_TEST_SOURCES})
target_link_libraries(faction_tests PRIVATE faction)

set(LIFE_SOURCES
    src/life/generate.cpp
    src/life/setup.cpp
    src/life/chronicle.cpp
    src/life/systems.cpp
)
add_library(life STATIC ${LIFE_SOURCES})
target_include_directories(life PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(life PUBLIC faction ecs)

set(LIFE_TEST_SOURCES
    tests/main.cpp
    tests/test_life_setup.cpp
    tests/test_life_cultivation.cpp
)
add_executable(life_tests ${LIFE_TEST_SOURCES})
target_link_libraries(life_tests PRIVATE life)

add_executable(worldgen_demo src/demo/main.cpp)
target_link_libraries(worldgen_demo PRIVATE faction)

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
