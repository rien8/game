# 项目规则

## 平台
- Windows-only，不要求跨平台。

## 终端
- 使用 **pwsh**（PowerShell Core）作为终端工具，默认在所有 `Bash` / 脚本调用前先 `pwsh -NoProfile -Command "..."`。
- 避免依赖 `cmd.exe` 的特殊行为；路径含空格时用双引号包裹。
- 路径传递用 `/c/...` 形式；文件读写真实路径用 `E:\...`。

## 编译器
- C/C++ 编译器：clang-cl（`C:\Program Files\LLVM\bin\`）
- 链接器：lld-link

## C++ 标准
- C++23（`CMAKE_CXX_STANDARD 23`、`CMAKE_CXX_STANDARD_REQUIRED ON`、`CMAKE_CXX_EXTENSIONS OFF`）

## 构建
- CMake 3.25+，Ninja generator。
- 编译器标志：`-Wall -Wextra -Wpedantic`（Debug 加 `-g -O0`，Release 加 `-O2 -DNDEBUG`）。
- 默认开启 ASan + UBSan（`-fsanitize=address,undefined`）。

## 包管理
- vcpkg：`C:\Users\lenovo\vcpkg\vcpkg.exe`
- toolchain file：`C:/Users/lenovo/vcpkg/scripts/buildsystems/vcpkg.cmake`
- triplet：`x64-windows`（clang-cl 与 MSVC ABI 兼容）。

## 代码风格
- 头文件：`#pragma once`，公共头放 `include/`，实现放 `src/`。
- 命名：类型 PascalCase，函数 camelCase，变量 snake_case，私有成员 `_` 后缀，无类函数CamlCase。
- 内存：智能指针（`std::unique_ptr` / `std::shared_ptr`），禁止裸 `new/delete`；RAII 管理所有资源。
- 字符串：`std::string_view`（只读）/ `std::string`（拥有），禁止 C 字符串函数。
- 错误处理：优先 `std::expected`（C++23）；模板用 concepts，禁止裸 `typename`。
- 不在头文件中使用 `using namespace`。