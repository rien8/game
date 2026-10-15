# ASan requires the release CRT, and this directory is where that decision is
# made: single-config generators resolve per-config flags per defining
# directory, so declaring Release here lifts only this target while the parent
# directory's ecs / ecs_tests keep their own configuration (Debug by default,
# where asserts must stay alive). Touching the CMAKE_BUILD_TYPE cache variable
# instead would be sticky across reconfigures of the same build directory.
#
# Recompile the library sources into the instrumented binary rather than
# linking the uninstrumented archive: a mismatched inline definition trips
# lld-link /failifmismatch and refuses to link.
set(CMAKE_BUILD_TYPE Release)

# This target now carries BOTH test suites (ECS_ASAN_SOURCES is fed from
# ECS_TEST_SOURCES and GENETICS_TEST_SOURCES), so the old `ecs_tests_asan`
# name understated its scope. Naming matters here: the top-level build dir
# also drops uninstrumented genetics_tests.exe / ecs_tests.exe, and running
# one of those by mistake while following the docs yields a false green.
add_executable(game_tests_asan ${ECS_ASAN_SOURCES})
target_include_directories(game_tests_asan PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_options(game_tests_asan PRIVATE
    -fsanitize=address -fno-omit-frame-pointer)
target_link_options(game_tests_asan PRIVATE -fsanitize=address)
