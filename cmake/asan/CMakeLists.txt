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

# Windows-MSVC 目标下，只要任何一个 TU include 了标准库头文件，链接就会去找
# `stl_asan.lib`——那是 MSVC 工具链自带的插桩版 STL，不是 LLVM 自带的
# （LLVM 那边只有 clang_rt.asan_*.lib）。CMake 把 clang++ 当普通编译器处理、
# 不继承 MSVC 的 LIB 环境，于是这个目录不在搜索路径上，链接报
# `could not open 'stl_asan.lib'`。实测补一条 /LIBPATH 即可。
#
# 用 glob 而不是写死版本号：MSVC 工具集目录名带版本（14.51.x），写死会在
# 升级后静默失效——而失效的表现是链接错误，不是测试变红。
file(GLOB _msvc_asan_stl_dirs
     "C:/Program Files (x86)/Microsoft Visual Studio/*/BuildTools/VC/Tools/MSVC/*/lib/x64")
foreach(dir IN LISTS _msvc_asan_stl_dirs)
    if(EXISTS "${dir}/stl_asan.lib")
        target_link_options(game_tests_asan PRIVATE
            "-Xlinker" "/LIBPATH:${dir}")
        break()
    endif()
endforeach()

