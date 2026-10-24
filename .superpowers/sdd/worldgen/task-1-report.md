# Task 1 报告：网格容器、世界容器与构建目标

状态：**DONE_WITH_CONCERNS**
基线：`fe3b978`（分支 `worldgen`）

---

## 各 Step 完成情况

| Step | 内容 | 状态 | 说明 |
|------|------|------|------|
| 1 | 写失败的测试 `tests/test_world_grid.cpp` | 完成 | 一字未改地照抄 brief，6 个用例 |
| 2 | 构建确认失败 | 完成 | 见下方「红灯证据」，确实因头文件缺失失败 |
| 3 | `include/gamev2/world/grid.hpp` | 完成 | 一字未改 |
| 4 | `include/gamev2/world/enums.hpp` | 完成 | 一字未改 |
| 5 | `include/gamev2/world/params.hpp` | 完成 | 一字未改 |
| 6 | `include/gamev2/world/world.hpp` | 完成 | 一字未改 |
| 7 | `src/world/world.cpp` | 完成 | 一字未改 |
| 8 | `CMakeLists.txt` 登记目标 | 完成 | 与 brief 代码块逐字一致；**未**加入 ASan 子目录 |
| 9 | 构建并运行测试确认通过 | 完成 | `6 test(s), 0 failed` |
| 10 | 提交 | **按指示跳过** | 未执行 `git commit`，也未执行 `git add` |

### 红灯证据（Step 2）

为让测试真正参与编译，Step 8 的 CMake 登记需要先行落地。实际做法是**两段式**：

1. 先只登记 `worldgen_tests` 可执行文件（不建 `worldgen` 库），因为
   `src/world/world.cpp` 尚不存在，登记它会让 CMake 在 *configure* 阶段就因
   「Cannot find source file」失败，那样看到的是配置错误而非缺头文件错误；
2. 构建 → 红灯：
   ```
   E:/gamev2/tests/test_world_grid.cpp:1:10: fatal error: 'gamev2/world/world.hpp' file not found
       1 | #include "gamev2/world/world.hpp"
   ```
3. 写完全部实现后，把 CMakeLists 替换为 brief Step 8 的完整代码块。

最终 `CMakeLists.txt` 与 brief Step 8 逐字一致，`git diff` 只增 13 行，
且不触碰 `ecs` / `ecs_tests` / ASan 任何一行。

---

## 测试最终输出

```
$ ./build/worldgen_tests.exe
[ RUN  ] grid_indexes_row_major_and_supports_linear_access
[  OK  ] grid_indexes_row_major_and_supports_linear_access
[ RUN  ] world_sizes_grids_and_layers
[  OK  ] world_sizes_grids_and_layers
[ RUN  ] world_sea_level_starts_at_zero
[  OK  ] world_sea_level_starts_at_zero
[ RUN  ] params_reject_degenerate_sizes
[  OK  ] params_reject_degenerate_sizes
[ RUN  ] celsius_scale_round_trips
[  OK  ] celsius_scale_round_trips
[ RUN  ] biome_enum_matches_declared_count
[  OK  ] biome_enum_matches_declared_count
6 test(s), 0 failed
```

基线未受影响：`./build/ecs_tests.exe` → `111 test(s), 0 failed`。

构建无任何 warning。

---

## 约束自查

| 约束 | 结果 |
|------|------|
| C++23 | 是；`std::expected` 留待后续阶段，本任务无错误路径 |
| 固定 clang++ | 是；沿用 CMakeLists 顶部的既有 pin |
| 无第三方库 | 是；测试框架复用 `tests/test_framework.hpp` |
| 无 `shared_ptr` / `unique_ptr` | 是 |
| 头文件无 `using namespace` | 是 |
| doxygen `///` 注释，只讲为什么 | 是；照抄 brief，未增删任何注释 |
| 行长 ≤ 90 | 是（见下） |
| 未执行 `git commit` | 是 |
| 临时文件在 `agent-temp/` | 是，探针已删除 |
| 文档在 `doc/` / 报告在指定路径 | 是 |
| 未改动 ecs 相关代码或目标 | 是 |

**行宽核查**：`awk` 一开始把 8 行标成超限，那是按 **UTF-8 字节** 数的，CJK
注释每个汉字 3 字节。改用字符数与终端显示宽度（东亚全角计 2 列）复测：
**6 个新文件全部 0 行超 90**。

**偏离 brief**：无。7 个文件的代码块全部逐字照抄。

**brief 之外多写的东西**：无。临时用于验证红灯/绿灯的探针已删，未留任何
brief 未要求的文件、函数或注释。

---

## 疑虑

### 1（重要，会阻塞后续任务）：`Grid<bool>` 的 `vector<bool>` 特化陷阱

`Grid<T>` 的成员是 `std::vector<T> cells_`。当 `T = bool` 时，`std::vector<bool>`
命中标准库的**特化**，其 `operator[]` 返回代理对象 `_Vb_reference`，而不是
`bool&`。于是 `Grid<bool>` 里两个返回 `T&` 的访问器一旦被真正调用就编译不过。

本任务**不受影响**——`World` 只构造 `cavern` / `aquifer`，从不索引它们；
类模板的成员函数只有被使用时才实例化，所以链接期、编译期都干净。

但任务 9（洞穴）与任务 11（含水层）只要写下一行：

```cpp
w.cavern[z](x, y) = true;
```

就会炸。实测证据（在 `agent-temp/` 里写了最小复现，已删除）：

```
include/gamev2/world/grid.hpp:27:16: error: non-const lvalue reference to type
  'bool' cannot bind to a temporary of type 'reference'
  (aka '_Vb_reference<_Wrap_alloc<allocator<unsigned int>>>')
   27 |         return cells_[index_of(x, y)];
```

```
include/gamev2/world/grid.hpp:40:16: error: ... (同上)
   40 |         return cells_[linear];
```

我**没有**擅自修——brief 明确要求逐字照抄，且这是跨任务的设计决策，应由你拍板。
三个候选修法（按侵入性从小到大）：

- **(a)** `World` 里把 `cavern` / `aquifer` 改成 `std::vector<Grid<std::uint8_t>>`
  ——只动 `world.hpp` 两行与 `world.cpp` 两个 fill，语义不变；
- **(b)** 给 `Grid<bool>` 显式特化，底层存 `std::vector<std::uint8_t>`；
- **(c)** 让访问器返回 `decltype(auto)`，接受 `Grid<bool>` 返回代理
  ——会把代理泄漏给调用方，最容易埋坑，不推荐。

我倾向 **(a)**：代价最小，且布尔网格本来也只存 0/1。

### 2（轻微）：`World` 构造并不校验参数

`WorldParams::is_valid()` 已实现并被测试覆盖，但 `World::World` 直接信任入参，
`WorldError::invalid_params` 目前无人产出。构造一个 `width = 0` 的 `World` 不会
失败，只会得到一排空网格。看起来后续某个阶段（本应是阶段 1 的入口）才负责
校验并用 `std::expected` 报出来。若那个阶段一直没来，这条就悬空了。
**本轮不动**，仅标记为待确认的设计缺口。

### 3（轻微）：`Grid::size()` 返回 `uint32_t`，尺寸无上界

`is_valid()` 不约束 `width * height`，而 `Grid::size()` 把 `std::size_t` 收窄成
`uint32_t`。默认 256×256 无碍，但 `width = 65536` 之类会让 `index_of` 溢出。
同样属于参数校验的缺口，归入第 2 点一起处理更合适。

### 4（预期内，非缺陷）：大量字段当前只写不读

`soil_depth`、`aquifer_top`、`flow`、`river_width`、`lake_depth`、全部枚举与常量
（`kNoDownstream`、`kMinCelsius`、`kCelsiusSpan`、`kFreezingCelsius`、
`kTropicalCelsius`、`kGlacierDrainage`、`ElevClass`、`Stone`、`Deposit`、
`WorldError`）目前无人读取。这是设计意图，后续 13 个任务会逐步填满，
已按指示**原样保留、未删未加访问器**。

同理，`world.hpp` 里的 `friend void make_elevation(World&);` 声明了一个尚不存在
的函数。这是合法 C++（友元声明即在最近的外层命名空间声明了它），编译链接均通过；
留待任务 2 定义。

---

## 改动文件清单（未提交，交给用户决定）

新增：
- `include/gamev2/world/grid.hpp`
- `include/gamev2/world/enums.hpp`
- `include/gamev2/world/params.hpp`
- `include/gamev2/world/world.hpp`
- `src/world/world.cpp`
- `tests/test_world_grid.cpp`

修改：
- `CMakeLists.txt`（+13 行，纯追加，`ecs` 与 ASan 段未动）

---

# 缺陷修复：`Grid<bool>` 的 `std::vector<bool>` 位压缩冲突

## 缺陷

`Grid<T>` 的底层存储是 `std::vector<T>`。`T = bool` 时命中 `std::vector<bool>`
的位压缩特化，其 `operator[]` 返回代理对象而非 `bool&`，因此 `at()` 与
`operator()` 无法满足 `T&` 的返回契约——一旦被调用就编译失败。

Task 1 本身不触发（`cavern` / `aquifer` 只构造、不索引），但计划的任务 11
（洞穴）与任务 12（矿脉）要写 `world.cavern[z](x, y) = true` 与
`world.cavern[zz].at(i)`，届时必然炸。属于"迟早会炸"，现在修。

## 改了什么

1. `include/gamev2/world/grid.hpp`
   - 新增 `namespace detail { struct BoolCell { bool value = false; }; }`。
   - 新增 `template<> class Grid<bool>` 显式特化，接口与主模板逐字一致
     （`operator()` 非常量/常量两个重载、`at`、`index_of`、`width`、
     `height`、`size`、默认构造、带 fill 的构造），只有底层存储换成
     `std::vector<detail::BoolCell>`。
   - 位置：主模板 `Grid<T>` 之后、`}  // namespace gamev2::world` 之前。

2. `tests/test_world_grid.cpp`
   - 追加 `TEST(grid_of_bool_supports_assignment_through_both_accessors)`，
     覆盖两个访问器的写入/读回、fill 构造与 `size()`。

未改 `CMakeLists.txt`、`world.hpp`、`world.cpp`：特化是纯头文件内的，
不需要新目标；`World` 侧不碰。

## 为什么是显式特化而不是改成 `Grid<std::uint8_t>`

改成 `std::uint8_t` 会把 `!= 0` 推进到二十多处调用点与断言里（洞穴、矿脉、
地下水层、可视化过滤……），总复杂度更高。包装体保住了"一格一 `bool` 语义"，
调用点零改动。

## 临时验证怎么做

在 `agent-temp/` 下写了一个最小翻译单元，只做三件事：

```cpp
gamev2::world::Grid<bool> g(2, 2, false);
g(0, 0) = true;
bool& r = g.at(0);
```

用 `clang++ -std=c++23 -fsyntax-only -I include agent-temp/grid_bool_probe.cpp`
编译，`exit=0`。

为了确认这个探针真的能抓到原缺陷（而不是"怎么都过"），另跑了一个**反向对照**：
一个复刻主模板原样（`std::vector<T> cells_` + `T& at()`），不做特化。同样的
`bool& r = g.at(0);` 立刻报错：

```
error: non-const lvalue reference to type 'bool' cannot bind to a temporary
of type 'reference' (aka '_Vb_reference<_Wrap_alloc<allocator<unsigned int>>>')
note: in instantiation of member function 'Grid<bool>::at' requested here
```

即：探针确实是缺陷的探针，修复后它通过、修复前它必然失败。

验证完两个临时文件都已删除。

## 测试结果

`cmake --build build` 全绿（`ecs` / `ecs_tests` / `worldgen` / `worldgen_tests`
全部 Built，无 warning）。

`worldgen_tests` 尾部：

```
[ RUN  ] grid_of_bool_supports_assignment_through_both_accessors
[  OK  ] grid_of_bool_supports_assignment_through_both_accessors
7 test(s), 0 failed
```

`ecs_tests` 尾部：

```
[ RUN  ] integration_conflicting_systems_serialize_in_batch_order
[  OK  ] integration_conflicting_systems_serialize_in_batch_order
111 test(s), 0 failed
```

`ecs_tests` 111 个用例与修改前一致，未受影响。

## 未提交

按项目约束，未 `git add`、未 `git commit`。
