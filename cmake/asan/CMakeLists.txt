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

add_executable(ecs_tests_asan ${ECS_ASAN_SOURCES})
target_include_directories(ecs_tests_asan PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_options(ecs_tests_asan PRIVATE
    -fsanitize=address -fno-omit-frame-pointer)
target_link_options(ecs_tests_asan PRIVATE -fsanitize=address)
