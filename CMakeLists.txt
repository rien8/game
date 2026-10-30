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
    tests/test_faction_tribulation.cpp
)
add_executable(faction_tests ${FACTION_TEST_SOURCES})
target_link_libraries(faction_tests PRIVATE faction)

set(LIFE_SOURCES
    src/life/generate.cpp
    src/life/setup.cpp
    src/life/chronicle.cpp
    src/life/systems.cpp
    src/life/economy.cpp
)
add_library(life STATIC ${LIFE_SOURCES})
target_include_directories(life PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(life PUBLIC faction ecs)

set(LIFE_TEST_SOURCES
    tests/main.cpp
    tests/test_life_setup.cpp
    tests/test_life_cultivation.cpp
    tests/test_life_birth.cpp
    tests/test_life_economy.cpp
    tests/test_life_decisions.cpp
    tests/test_life_conflict.cpp
    tests/test_life_politics.cpp
    tests/test_life_emergence.cpp
)
add_executable(life_tests ${LIFE_TEST_SOURCES})
target_link_libraries(life_tests PRIVATE life)

add_executable(worldgen_demo src/demo/main.cpp)
target_link_libraries(worldgen_demo PRIVATE faction)
set(GENETICS_SOURCES
    src/genetics/locus.cpp
    src/genetics/species.cpp
    src/genetics/expression.cpp
    src/genetics/breeding.cpp
    src/genetics/survival.cpp
    src/genetics/statistics.cpp
    src/genetics/population.cpp
    src/ecs/adapt/genetics_systems.cpp
)
add_library(genetics STATIC ${GENETICS_SOURCES})
target_include_directories(genetics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(genetics PUBLIC ecs)

set(GENETICS_TEST_SOURCES
    tests/main.cpp
    tests/test_genetics_smoke.cpp
    tests/test_genetics_locus.cpp
    tests/test_genetics_species.cpp
    tests/test_genetics_expression.cpp
    tests/test_genetics_random.cpp
    tests/test_genetics_breeding.cpp
    tests/test_genetics_survival.cpp
    tests/test_genetics_statistics.cpp
    tests/test_genetics_population.cpp
    tests/test_genetics_determinism.cpp
    tests/test_genetics_performance.cpp
    tests/test_genetics_systems.cpp
)
add_executable(genetics_tests ${GENETICS_TEST_SOURCES})
# environment 只挂在可执行目标上，不挂 genetics——见下面那条不变量。链路
# genetics_tests -> environment -> genetics -> ecs 无环；本轮起 genetics_tests
# 要直接构造 EnvironmentWorld / EnvironmentSystem，而它们的 out-of-line 符号在
# src/ecs/adapt/environment_systems.cpp，属 environment 目标。
target_link_libraries(genetics_tests PRIVATE genetics environment)

set(ENVIRONMENT_SOURCES
    src/environment/climate.cpp
    src/environment/resource.cpp
    src/environment/sites.cpp
    src/ecs/adapt/environment_systems.cpp
    src/ecs/adapt/movement_system.cpp
)
# 不变量：genetics 的任何 TU 都不得引用 src/environment/*.cpp 定义的符号。
# 今天成立是因为 genetics 只用到纯聚合体 EnvironmentSnapshot（零 out-of-line
# 符号），从不碰 ClimateField / CellIndex / kGridSide，因此没有链接期需求。
# 源头硬约束：src/environment/ 不得 include 任何 genetics/ 或 ecs/ 头文件。
# 将来 genetics 若真要调用 ClimateField 的 out-of-line 函数，正确做法是拆出
# 独立目标，或显式拆掉上面那条硬约束；不是给 genetics 加
# target_link_libraries(... environment)——两边都是 STATIC 时 CMake 允许依赖
# 环、不会报错，只会在链接期以 undefined reference 的形式爆出来。
add_library(environment STATIC ${ENVIRONMENT_SOURCES})
target_include_directories(environment PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(environment PUBLIC genetics)

set(ENVIRONMENT_TEST_SOURCES
    tests/main.cpp
    tests/test_environment_smoke.cpp
    tests/test_environment_climate.cpp
    tests/test_environment_resource.cpp
    tests/test_environment_sites.cpp
    tests/test_environment_systems.cpp
)
add_executable(environment_tests ${ENVIRONMENT_TEST_SOURCES})
target_link_libraries(environment_tests PRIVATE environment)

set(ECOLOGY_SOURCES
    src/ecology/ecology.cpp
)
add_library(ecology STATIC ${ECOLOGY_SOURCES})
target_include_directories(ecology PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ecology PUBLIC environment)

set(ECOLOGY_TEST_SOURCES
    tests/main.cpp
    tests/test_ecology_module.cpp
)
add_executable(ecology_tests ${ECOLOGY_TEST_SOURCES})
target_link_libraries(ecology_tests PRIVATE ecology)

# 演示场景配置：位点蓝图、遗传与环境参数、初始种群。
# 抽成库是因为 sim 与 demo_gui 要跑同一个世界——两份实现会让那些实测出来的
# 调平衡数字各走各的，界面上的种群曲线就与 sim 打印的对不上了。
add_library(demo_scenario STATIC src/demo/scenario.cpp)
target_include_directories(demo_scenario PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(demo_scenario PUBLIC ecology)

# 演示入口：ecology 模块的调用方，自带 main。刻意不进下面的 ASan foreach ——
# 那条列表把所有 *_TEST_SOURCES 连同 tests/main.cpp 编成单个可执行目标，而本
# 目标自带 main，合并会撞 duplicate symbol。ecology 设计文档 §7 警告过「ASan
# 列表漏掉整个模块」，因此这里显式记一笔：这是排除，不是遗漏。
add_executable(sim src/sim/main.cpp)
target_link_libraries(sim PRIVATE demo_scenario)

# Qt 演示程序。默认不建：Qt 是可选依赖，不装 Qt 的人不该被 configure 卡住。
# 打开开关时用 -DCMAKE_PREFIX_PATH=<Qt 6.12 的 msvc2022_64 目录> 指路。
option(GAMEV2_BUILD_GUI "构建 Qt 演示程序 demo_gui" OFF)
if(GAMEV2_BUILD_GUI)
    # CanvasPainter：生态活体图用它画到 GPU 上（QCanvasPainterWidget）。
    find_package(Qt6 REQUIRED COMPONENTS Widgets CanvasPainter)
    # AUTOMOC 必须在目标创建之前打开：views.hpp 里的 Q_OBJECT 类靠它生成 moc。
    set(CMAKE_AUTOMOC ON)
    add_executable(demo_gui
        src/gui/main.cpp
        src/gui/simulation.cpp
        src/gui/sprites.cpp
        src/gui/views.cpp
    )
    target_link_libraries(demo_gui PRIVATE demo_scenario worldgen
                          Qt6::Widgets Qt6::CanvasPainter)
    # 角色精灵打进可执行文件：界面从 :/assets/... 读，不依赖运行目录，
    # 免得换个地方启动 exe 就找不到素材。
    file(GLOB_RECURSE GAMEV2_CHARACTER_SPRITES CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/assets/sprites/characters/individual/*.png"
         "${CMAKE_CURRENT_SOURCE_DIR}/assets/sprites/characters/anim/*/atlas/atlas.png")
    qt_add_resources(demo_gui "character_sprites"
        PREFIX "/assets/sprites/characters"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/sprites/characters"
        FILES ${GAMEV2_CHARACTER_SPRITES})
    # WIN32_EXECUTABLE：GUI 程序不该带一个黑控制台窗口。
    set_target_properties(demo_gui PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

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
    foreach(f IN LISTS ECS_SOURCES ECS_TEST_SOURCES
                  GENETICS_SOURCES GENETICS_TEST_SOURCES
                  ENVIRONMENT_SOURCES ENVIRONMENT_TEST_SOURCES
                  ECOLOGY_SOURCES ECOLOGY_TEST_SOURCES
                  WORLDGEN_SOURCES WORLDGEN_TEST_SOURCES
                  HISTORY_SOURCES HISTORY_TEST_SOURCES
                  FACTION_SOURCES FACTION_TEST_SOURCES
                  LIFE_SOURCES LIFE_TEST_SOURCES)
        list(APPEND ECS_ASAN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${f}")
    endforeach()
    # 每个可执行目标都自带 tests/main.cpp（它定义了 main 与 test::run_all），
    # 合并源列表后同一份 TU 会进数组两次，直接编译成可执行文件会在链接期
    # 撞上 duplicate symbol。
    list(REMOVE_DUPLICATES ECS_ASAN_SOURCES)
    add_subdirectory(cmake/asan)
endif()
