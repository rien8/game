# 世界生成模块设计文档

- 日期：2026-10-19
- 状态：待实现
- 目标平台：Windows / clang++ 23 / C++23
- 基线分支：`worldgen`（从 `8e43fcc ecs实现` 切出，丢弃其后的全部提交）

## 1. 背景与目标

为 gamev2 提供世界生成器：给定 `seed` 与尺寸，生成一个内部自洽的
Dwarf Fortress 式世界——地表自然地理（高程 / 降水 / 温度 / 群系 / 水文）
加地下结构（岩层 / 矿脉 / 洞穴 / 含水层）。

生成完成即为终点。本轮不进入游戏，不产出任何玩法。

### 1.1 设计目标

- 同 `(seed, params)` 必须产出逐字节相同的世界，否则无法调试
- 阶段之间单向依赖，编排顺序收敛到唯一入口
- 零第三方依赖，仅用 C++23 标准库
- 生成结果可被肉眼校验（PPM 图像导出），而非只能靠断言数值

### 1.2 非目标

本轮明确不做以下内容：

- 文明、聚落、历史、生物种、种族参数
- 植被与结构物（树木、草丛、巨石）
- 渲染、图形界面、游戏循环、存档
- ECS 接入（见 [8.1](#81-世界不进-ecs)）
- 与 DF 逐像素一致的噪声（见 [10.1](#101-噪声与-df-不逐像素一致)）

## 2. 总体架构

```
                 ┌────────────────────┐
                 │  generate_world()  │  唯一编排入口
                 └─────────┬──────────┘
                           │ 顺序调用 10 个阶段
   ┌───────────┬───────────┼───────────┬───────────┐
   ▼           ▼           ▼           ▼           ▼
 terrain   hydrology    strata    caverns    deposits
 1-4 节     5 节        6-7 节     8 节       9-10 节
   │           │           │           │           │
   └───────────┴───────────┴───────────┴───────────┘
                           │ 就地改写
                 ┌─────────▼──────────┐
                 │       World        │  纯数据容器
                 └────────────────────┘
```

### 2.1 阶段顺序

就地改写的代价是阶段依赖是隐式的：调错顺序不报错，只静默产出错误结果。
因此编排必须收敛到唯一入口，顺序写死在一处。

| # | 阶段 | 读取 | 写入 |
|---|---|---|---|
| 1 | `make_elevation` | — | `elevation`, `sea_level_` |
| 2 | `make_rainfall` | `elevation` | `rainfall` |
| 3 | `make_temperature` | `elevation`, `rainfall` | `temperature` |
| 4 | `make_hydrology` | `elevation` | 下游指针 / 汇流量 / 河宽 / 湖深 / 排水量 |
| 5 | `make_biomes` | `elevation`, `rainfall`, `temperature`, `drainage` | `biome` |
| 6 | `make_soils` | `biome`, `rainfall` | `soil_depth` |
| 7 | `make_strata` | `biome`, `rainfall` | `stone`, `aquifer_top` |
| 8 | `make_caverns` | — | `cavern` |
| 9 | `make_deposits` | `stone`, `cavern` | `deposit` |
| 10 | `finalize_aquifers` | `aquifer_top`, `stone`, `cavern` | `aquifer` |

水文排在群系之前，因为群系判定要用它产出的排水量（见 5.4）。这个方向是
单向的：水文只读高程，不读群系，因此对调这两步没有副作用。

10 与 9 的顺序不可交换：含水层不得出现在洞室里，而洞室必须在放置矿脉之前
定型。反过来同样，矿脉不得落进洞室。

### 2.2 文件划分

```
include/gamev2/world/
  grid.hpp          Grid<T>（纯模板，无 .cpp）
  params.hpp        WorldParams / WorldError
  enums.hpp         Biome / Stone / Deposit / ElevClass
  tables.hpp        群系查表 + 矿脉定义表（inline constexpr，测试可见）
  world.hpp         World 容器
  rng.hpp           Pcg32 + 无状态 value noise + stage_seed
  generate.hpp      generate_world + 10 个阶段函数声明
src/world/
  terrain.cpp       阶段 1-3：高程 / 降水 / 温度
  hydrology.cpp     阶段 4：水文
  biomes.cpp        阶段 5：群系
  strata.cpp        阶段 6-7：土壤 / 岩层
  caverns.cpp       阶段 8：洞穴
  deposits.cpp      阶段 9-10：矿脉 / 含水层
  generate.cpp      generate_world 编排
src/demo/main.cpp   worldgen_demo：统计 + PPM 导出
tests/
  test_world_grid.cpp
  test_world_rng.cpp
  test_world_elevation.cpp
  test_world_rainfall.cpp
  test_world_temperature.cpp
  test_world_hydrology.cpp
  test_world_biomes.cpp
  test_world_strata.cpp
  test_world_caverns.cpp
  test_world_deposits.cpp
  test_world_determinism.cpp
```

## 3. 数据结构

### 3.1 Grid

三维数据表示为 `std::vector<Grid<T>>`（每 z 层一份同构数组），因此只需一个
二维容器。

```cpp
/// 稠密二维网格，行主序。
template<typename T>
class Grid
{
public:
    Grid() = default;
    Grid(std::uint32_t width, std::uint32_t height, T fill = T{});

    T& operator()(std::uint32_t x, std::uint32_t y) noexcept;
    const T& operator()(std::uint32_t x, std::uint32_t y) const noexcept;

    /// 线性寻址。downstream 这类「存格子索引」的字段靠它跳转，
    /// 省去一次 x/y 到索引的重复换算。
    T& at(std::uint32_t linear) noexcept;

    [[nodiscard]] std::uint32_t index_of(std::uint32_t x,
                                         std::uint32_t y) const noexcept;

private:
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::vector<T> cells_;
};
```

### 3.2 WorldParams

```cpp
struct WorldParams
{
    std::uint32_t width = 256;
    std::uint32_t height = 256;
    /// z 层数：0 为地表，1..z_levels-1 为地下。
    std::uint32_t z_levels = 6;
    /// 每 z 层厚度（以格计）。
    std::uint32_t z_cell_depth = 16;
    std::uint64_t seed = 0;
    /// 目标海洋面积占比（0..100）。海平面由高程分位数反解，
    /// 因此不同 seed 之间海陆比不会大幅漂移。
    std::uint8_t ocean_percent = 60;
};
```

预设取 `256 × 256`，`z_levels = 6`（地表 1 + 地下 5），`z_cell_depth = 16`
（总深 80 格），与 DF 默认 continental 规模一致。尺寸与层数全部可配，
但本轮只实现这一套预设。

### 3.3 World

纯数据容器，无行为。所有生成阶段以 `World&` 为唯一操作对象。

```cpp
class World
{
public:
    explicit World(WorldParams params);

    [[nodiscard]] const WorldParams& params() const noexcept;
    /// 总格深 = z_levels * z_cell_depth。
    [[nodiscard]] std::uint32_t depth() const noexcept;
    /// 阶段 1 反解出的实际海平面。
    [[nodiscard]] std::uint8_t sea_level() const noexcept;
    [[nodiscard]] bool is_ocean(std::uint32_t x, std::uint32_t y) const noexcept;

    // ── 地表量，每 (x, y) 一个值 ──
    Grid<std::uint8_t>  elevation;
    Grid<std::uint8_t>  rainfall;
    Grid<std::uint8_t>  temperature;
    Grid<Biome>         biome;
    Grid<std::uint32_t> downstream;
    Grid<std::uint32_t> flow;
    Grid<std::uint8_t>  river_width;
    Grid<std::uint8_t>  lake_depth;
    Grid<std::uint8_t>  drainage;     ///< 0..100，越大排水越好
    Grid<std::uint8_t>  soil_depth;
    Grid<std::uint8_t>  aquifer_top;

    // ── 地下量，每 z 层一份 ──
    std::vector<Grid<Stone>>   stone;
    std::vector<Grid<Deposit>> deposit;
    std::vector<Grid<bool>>    cavern;
    std::vector<Grid<bool>>    aquifer;

private:
    friend void make_elevation(World&);
    std::uint8_t sea_level_ = 0;
};
```

**海洋掩码不入库**：`is_ocean()` 由 `elevation` 与 `sea_level_` 现算。多存
一个 bool 数组换一次减法比较，不划算。

**`downstream` 与 `flow` 留在 World 上**：它们是分水岭，后续的侵蚀、聚落选址、
植被分布都要用。阶段 5 之后它们只被读，不再被写。

### 3.4 枚举

```cpp
enum class ElevClass : std::uint8_t
{
    ocean, lowland, hills, mountain,
};

enum class Biome : std::uint8_t
{
    ocean, lake, mountain,
    marshland, swamp,
    temperate_coniferous, temperate_broadleaf,
    tropical_coniferous, tropical_dry_broadleaf, tropical_moist_broadleaf,
    grassland, meadowland, savanna, shrubland, pine_barrens,
    sand_desert, rocky_wasteland, badlands,
    tundra, glacier,
};

enum class Stone : std::uint8_t
{
    granite, basalt, limestone, sandstone, shale, slate, gravel, marble,
    clay, ice, igneous, igneous_intrusive,
};

enum class Deposit : std::uint8_t
{
    none, copper, iron, gold, silver, tin, lead, zinc, coal,
    diamond, sapphire, emerald, adamantine,
};
```

`Deposit::none` 而非 `std::optional<Deposit>`：地下量是三维稠密数组，
`optional` 会让整个数组体积翻倍，而「这一格没有矿」不需要额外状态即可表达。

`downstream` 的哨兵值：

```cpp
/// 无下游：该格是海洋，或落在洼地底部（湖泊）。
inline constexpr std::uint32_t kNoDownstream =
    std::numeric_limits<std::uint32_t>::max();
```

## 4. 随机与噪声

### 4.1 Pcg32

不使用 `<random>` 的分布——它们是实现相关的，跨标准库不稳定，会破坏确定性。
取值一律经自己定义的接口。

```cpp
class Pcg32
{
public:
    explicit Pcg32(std::uint64_t seed) noexcept;
    std::uint32_t next() noexcept;
    /// [0, n) 无偏。
    [[nodiscard]] std::uint32_t below(std::uint32_t n) noexcept;
    /// [0, 1)。
    [[nodiscard]] double unit() noexcept;
};

/// 阶段之间的随机流互不串扰：各阶段用自己的派生 seed 自建 Pcg32。
/// stage 编号全局唯一：同一个编号只能被一条随机流占用，否则两个阶段会从
/// 同一条流上取数，派生 seed 相同。
[[nodiscard]] std::uint64_t stage_seed(std::uint64_t seed,
                                      std::uint32_t stage) noexcept;
```

流号清单在 `include/gamev2/world/enums.hpp` 的 `Stream` 枚举里：名字是**流名**
不是阶段号（region / offset / intrusion 三条流同属阶段 7，cavern 是阶段 8 却
用 3 号流）。「全局唯一」由同一处的 `static_assert` 钉住——枚举本身允许重复
取值，所以这条断言才是契约的唯一保证：两个阶段共用一个流号会编译失败。

### 4.2 无状态 value noise

```cpp
[[nodiscard]] double value_noise(std::uint64_t seed, double x, double y);
[[nodiscard]] double value_noise3(std::uint64_t seed,
                                  double x, double y, double z);
[[nodiscard]] double fbm(std::uint64_t seed, double x, double y,
                        int octaves, double lacunarity, double gain);
[[nodiscard]] double fbm3(std::uint64_t seed, double x, double y, double z,
                         int octaves, double lacunarity, double gain);
```

**无状态是硬约束**：噪声按「坐标 + 种子」直接求值，不依赖遍历顺序。否则一旦
阶段内的遍历顺序调整（哪怕只是为了并行），同一个世界就会变成另一个世界，
而这种改动在代码审查中看不出问题。

## 5. 生成规则

### 5.1 高程 `make_elevation`

1. `fbm(seed, x / freq, y / freq, 3, 2.0, 0.5)` → 归一化
   到 `0..100` 的 byte，其中 `freq = max(width, height) / kElevationFeatures`，
   `kElevationFeatures` 初值 4（**特征横跨全图的个数**，不是每格多少）
2. 海平面反解：全图高程计数排序，取第 `ocean_percent` 分位写入 `sea_level_`
3. `is_ocean(x, y) == (elevation(x, y) < sea_level_)`

计数排序：高程只有 101 个取值桶，O(n)。

频率按图幅缩放而非固定格距，是为了让「世界尺度」在任何图幅上都指同一件事：
特征随图幅一起放大，汇水盆地才随图幅变大。固定格距会把盆地面积钉死在特征尺寸
的平方上（8 格特征 → 几十格的盆地），水文阶段因此攒不出超过 `kRiverThreshold`
的汇流量。初值 4 由扫描 {4, 5, 6} 定出，在当前水文算法下（seed 3）128×128 的
最大汇流量分别为 238 / 152 / 227，256×256 为 883 / 590 / 1044。

### 5.2 降水 `make_rainfall`

1. `fbm` → `0..255` 基础降水
2. **距海衰减**：从全部海洋格做多源 BFS，按曼哈顿距离 `d` 乘一个随 `d`
   递减的系数
3. **山脉雨影**：沿固定主导风向（常量 `kPrevailingWind`，默认西→东）逐行
   扫描，累积沿途高程抬升，越过越高地形后后续格降水越少

固定风向是简化：DF 的风向随季节变化，本轮不做。

### 5.3 温度 `make_temperature`

四项相加，输出 `0..255`：

| 项 | 规则 |
|---|---|
| 纬度基线 | `y` 映射到纬度带，两极低、赤道高 |
| 高度递减 | `-= lapse * max(0, elevation - sea_level)` |
| 海洋调节 | 海洋格向该纬度均值靠拢 |
| 湿度修正 | `rainfall` 越低修正越大（干燥区更冷） |

海洋调节是为了避免「内陆极端 / 海洋酷热」这种明显失真的结果。

**温度刻度**：`temperature` 为 `0..255` 的 byte，线性映射到 `-40°C..+50°C`
（`celsius(t) = -40 + t * 90 / 255`）。群系的冻结与热带阈值都写在这条刻度上，
而不是裸 byte 值：冻结阈值取 DF 原文的 `-5°C`（对应 byte ≤ 99），
热带阈值取 `+30°C`（对应 byte ≥ 199）。

### 5.4 群系 `make_biomes`

严格照 DF 的三步判定，不做简化：

```
1. 高程先判   ElevClass::ocean   → ocean
              ElevClass::mountain → mountain
2. 基础群系   = kBaseBiome[降水 8 档][排水 8 档]
3. 温度覆写   temp <= 冻结阈值  → 排水 < 75 ? tundra : glacier
              temp >= 热带阈值  → 针叶林转热带针叶林，阔叶林按干湿转
                                  热带干/湿阔叶林
```

```cpp
[[nodiscard]] Biome select_biome(ElevClass, std::uint8_t drainage,
                                 std::uint8_t rain, std::uint8_t temp);
```

`kBaseBiome` 是 `include/gamev2/world/tables.hpp` 里的
`inline constexpr std::array<Biome, 64>`，行 = 降水档（0 最干 .. 7 最湿），
列 = 排水档（0 排水最差 .. 7 排水最好）。放在头文件里用 `inline constexpr`
而非 `.cpp` 的匿名静态数组，是为了让测试能直接引用同一份数据核对。

**为什么必须引入排水量**：DF 里 grassland / shrubland / savanna / swamp /
marsh 这一整组的区分靠的是排水量，不是降水量。只用降水的话沼泽只会在雨最多
的地方出现，山坡上的灌丛也长不出来，整组地形会失真。排水量是河流与泛滥平原
计算的副产物，在阶段 4 产出。

分档：`rain_band = min(7, rain / 32)`（`rain` 为 0..255），
`drainage_band = min(7, drainage / 13)`（`drainage` 为 0..100）。

### 5.5 水文 `make_hydrology`

DF 的 flow-basin 思路，本轮最照抄的一块。

```
1. 洼地填平   priority-flood，算出每格填平高度（无洼 DEM，见下段关于 4 邻域的限定）
2. 下游指针   每格指向 8 邻域中填平高度最低者；落空时退回 parent，深洼截断成湖
3. 汇流量     按 priority-flood 的**弹出次序逆序**处理；flow[格] 初值 1，加给下游
4. 河道       flow 超阈值者为河（含海洋格，即入海河口）；宽度按 flow 分档 → 1..5
5. 湖泊       终点非海洋且落在洼地 → 成湖，深度 = 填平高度 - 原始高程
6. 排水量     由水体向外扩散得到 drainage（见下）
```

第 1 步的洪泛只走 4 邻域，保证的是「每格都能走到某个种子」，**不是**「没有
洼地」：唯一出口落在对角线上的洼永远填不平，下游指针因此需要 parent 兜底。

第 2 步择取分两级。主规则取**填平高度严格更低**的 8 邻域；主规则落空时退回
**parent** —— priority-flood 里把本格推入堆的那一格，即通向出口的方向。

两级规则缺一不可：priority-flood 会把洼地内部抬到溢流高度，于是填平后的
整片洼底与溢流格**同级**，只靠严格小于的洼内格无处可去，链条会在此断成残桩。

回退**不能**改用「原始高程更低者」：洼地内部原始高程最低处在**洼底**，
按它下降等于把水引进坑里，而水该沿溢流方向走出去。parent 正是那个方向。
注意种子格（边界与海洋）初入堆时没有 parent，但若它与某个先弹出的格
4-邻接，那一格照样会推它并被记为父；论证里不能说「种子没有父」。

第 5 步的端内流靠**截断指针**实现：填平深度超过 `kLakeMinDepth` 的洼不再
退回 parent，直接截断，于是成湖。**不能**改在洪泛里加「最多抬 k 层」的上限
来做同一件事：那样推入值会低于当前高度，堆的弹出序列不再按高度非降，
第 3 步赖以成立的拓扑序随之失效，指针会成环（实测 cap=3/4 各有 3/2 处
逆序）。截断只删边不添边，因此无环性不受影响。

无环性用一把钥匙论证：**弹出次序沿指针严格递减**。两类边都如此 —— 主规则的
目标填平高度严格更低，最小堆必让它先弹出；parent 是把本格推入堆的那一格，
本格只在它弹出之后才可能被推入。于是指针走一步，弹出次序必退一格，
而严格递减的次序不可能成环。∎

第 3 步因此只需把弹出次序**逆序**遍历，无需再排序：本格一定排在自己的
所有下游之前被处理，交出去的流量必然已经算完。注意填平高度在这里**不能**
当排序键 —— 弹出次序才是那个对两类边都成立的关键。

第 4 步的河宽**包括海洋格**：河必然入海，入海口那一格是终点，会攒下整条河的
流量，渲染时海面上的非零河宽即河口。

**排水量的推导**：以湖泊格与河道格为源点，初始 `wetness` 分别为 100 与
`50 * river_width / 5`；每向外一格 `wetness` 衰减一档，下限 0，
`drainage = 100 - wetness`。

扩散**按湿量从大到小传播，不是普通的多源 BFS**。湿量每步只减一，值域因此
就是 1..100，按值分桶后从 100 抽到 1 即可，等价于单位边权的 Dijkstra，
首次写入即最大可达湿量。顺序在这里不是无关的：源点若按栅格序入队，弱源
（河，10..50）会抢先占住邻格，而入湖的河往往就紧邻湖，于是每个湖口外都围出
一圈排水量 ≈ 91 的「好干」地 —— 那圈地离水只有一步。

这是 DF 排水量的等价近似而非复刻——DF 的 `drainage` 来自其泛洪平原计算，
算法未公开。

阈值与分档的具体数值是调参项，初值由 demo 导出 PPM 后对着图调。

### 5.6 土壤 `make_soils`

`soil_depth ∈ 0..3` 格。群系表给基值，降水做修正：雨林土厚、沙漠土薄、
冰盖无土。

### 5.7 岩层 `make_strata`

每 `(x, y, z)` 的岩性由三层叠加决定：

1. **基岩区域**：低频噪声给每格一个区域岩性
2. **深度层序**：全局层序模板（表层沉积 → 砂岩 → 页岩 → 石灰岩 → 深层火成），
   按该格区域岩性做替换，并按低频噪声做层位偏移（层界不平）
3. **岩浆侵入体**：撒若干个各向异性 3D 椭球，范围内强制为
   `igneous` / `igneous_intrusive`

`aquifer_top` 也在本阶段确定（读 `biome`），但不填充——填充推迟到阶段 10，
因为那时洞室已定型。

### 5.8 洞穴 `make_caverns`

1. `z ∈ [1, z_levels)` 范围随机初始化占据率 ~0.45
2. 6 邻域 cellular automata，迭代 4 次：`>= 5` 邻居实心则转实心，
   `<= 2` 则转空，否则不变
3. 洪泛标记空腔连通域，剔除体素数低于阈值的碎屑
4. 不挖穿 `z = 0`

边界约定：**世界外的邻居不计入实心邻居**，等价于把有限图幅当作更大世界的一扇
窗口。因此贴边格子与最深层的天花板邻居数天然偏少，更容易被判成空腔——实测
（96×96×6，seed 33，环宽 1）最外一圈实心率 15.2%（288/1900）、内部 35.7%
（15766/44180）；环宽 2 为 21.6% / 36.0%；256×256×6 seed 1 环宽 1 为 14.1% /
36.4%。z=1 层 41.6% 空一路升到 z=5 层 86.3% 空（96×96×6，seed 33）。
若改成「世界外算实心」，产出会变成地图四周被岩体封死；本轮取前者。
Task 14 的 demo 已按观感复核：外圈比内部更空，「四周被岩体封死」没有发生。

### 5.9 矿脉 `make_deposits`

```cpp
struct DepositSpec
{
    Deposit kind;
    std::uint16_t vein_count;       ///< 每张地图的撒点尝试次数，不是概率
    std::uint8_t max_z;            ///< 只到该 z 层
    bool allow_large;              ///< 是否允许大矿脉
    std::span<const Stone> hosts;  ///< 可出现的母岩
};
```

`DepositSpec` 表同样在 `include/gamev2/world/tables.hpp` 里 `inline constexpr`。

三个全局尺寸档（小 3–6 / 中 10–20 / 大 30–60 体素），按权重随中心点，
用各向异性 3D blob 加随机游走填充。避开洞室；允许落在含水层中（DF 就是这样，
含水层与矿脉是两个独立字段，读取方自行决定优先级）。

尺寸档是**逐条矿脉**的体素数：相邻的同种矿脉会并块，因此团块尺寸会大于
单脉尺寸，统计「达标率」必须用逐矿脉口径，团块口径只能当下界代理用。
实测（96×96×6，seed 77，702 次撒点，逐矿脉口径）：在档率 40.6%，但「至少 3
格的矿脉」在档率 83.3%；一格没落的撒点占 41.9%，根因是区域岩性的空间尺度
（约 24 格）与 7×7 种子扫描 footprint 的关系——沉积系母岩成片，扫描框可能
整框落在母岩不匹配的岩区里。因此岩性区域的空间尺度必须细到足以容纳大档
矿脉，否则沉积系矿种的大档在几何上不可达。该约束跨阶段，牵涉岩层与矿脉
两个阶段。

### 5.10 含水层 `finalize_aquifers`

`aquifer_top[x][y]` 由降水 / 群系 / 地表类型推出，`0` 表示无含水层。
海洋、沙漠、冰川无含水层（冰川下是冰而非水）。

从 `aquifer_top` 到最底层，凡是石头且非洞室的格即为含水层。

## 6. 错误处理

参数非法是唯一可预期的失败。按 C++23 惯例返回 `std::expected`：

```cpp
enum class WorldError
{
    invalid_params,
};

[[nodiscard]] std::expected<World, WorldError>
generate_world(const WorldParams& params);
```

`width` / `height` / `z_levels` / `z_cell_depth` 为 0 时返回
`WorldError::invalid_params`。其余字段不校验——给它们自由。`bad_alloc`
走标准异常，不包进 `std::expected`。

阶段函数本身不返回错误：它们的失败模式全是不该发生的内部不一致，
正常代码路径上不可达。

## 7. 测试策略

零依赖，`tests/main.cpp` 与 `tests/test_framework.hpp` 与 ECS 无关，
直接复用于 `worldgen_tests` 目标。

每条测试对应一个可验证目标：

| # | 目标 | 验证方式 |
|---|---|---|
| 1 | 确定性 | 同 seed 生成两次，全部 grid 逐字段相等 |
| 2 | 高程合法 | 全部值 ∈ 0..100；海洋面积落在 60% ± 5% |
| 3 | 无环 | 任意格沿 `downstream` 走，步数 < width * height |
| 4 | 水文自洽 | 河道格非海洋；终点格必在海洋或 `lake_depth > 0`；终点 `flow` 不小于其上游 |
| 5 | 河宽单调 | 抽 200 个河道格，`river_width` 不小于同 flow 档的中位数 |
| 6 | 土壤 | 全部 ∈ 0..3 |
| 7 | 层序 | `z=0` 层除冰盖外全为沉积类岩石 |
| 8 | 洞穴 | 洞室体素数 > 0；全在 `z >= 1`；`z=0` 无洞 |
| 9 | 矿脉 | 矿脉体素数 > 0；不与洞室重叠；母岩在 `deposit_specs()` 对应项的 `hosts` 内 |
| 10 | 含水层 | 全部在 `aquifer_top` 以下；洞室内无含水层 |
| 11 | 群系 | 海洋格恒为 `Biome::ocean`，山岳格恒为 `Biome::mountain` |
| 12 | 群系覆写 | `temperature` 低于冻结阈值的格，按 `drainage < 75` 分别为 `tundra` / `glacier` |

第 1 条与第 3 条是关键项：确定性缺了它没法调试，环会让水文阶段静默死循环。

测试 4 没有断言「下游格高程不高于自身」——那条性质成立于填平后的 DEM，而填平
结果是阶段 5 的内部量，刻意未存入 World（见 [11.6](#116-填平-dem-未入库)）。

## 8. 关键设计决策

### 8.1 世界不进 ECS

**决策**：世界是普通 C++ 数据结构，ECS 模块保持 `8e43fcc` 原样，本轮不改。

**理由**：

1. ECS 优化的是「按实体集合遍历 + 随机访问」，而地图不是实体集合，是规则
   结构。生成过程是对整层跑噪声、对每格查 8 邻域、对源点做洪泛，全是坐标
   规则访问，没有 archetype 聚簇可利用。
2. ECS 的加组件 = 换 archetype + 搬移行，是为「运行期反复增删组件」付的
   代价。世界生成是一次性全图写入，纯属白付。
3. ECS 的组件契约要求 `default_constructible` + `nothrow move`，而「这一层
   可能是空的」用 `std::vector<std::optional<T>>` 表达最自然。
4. **地表会被高频改写**。挖开一个格子在数组里是一行赋值，在 ECS 里是换
   archetype 加搬移全部组件，且成本随挖掘进度线性增长。这是决定性的。
5. ECS 模块自述定位是「单场景几万级实体」。默认预设下格子数是几十万到
   几百万，它是数据，不是逻辑层对象。

**地表环境影响生物 / 物品 / 事件不构成反例**：那是「实体 -> 世界」的单向
读取。实体持坐标向世界查询环境，O(1)，世界完全不知道实体存在；事件则是
扫描网格找条件再往 ECS 里创建实体。依赖方向永远是单向的，这恰恰是两者
解耦的前提，也是数组可维护的原因。

### 8.2 就地改写而非阶段函数链

阶段函数链（纯函数串联）能显式表达阶段间依赖，代价是中间结构要作为参数
层层传递。就地改写的代价是依赖隐式，收益是代码显著更短。

选就地改写，并用两条约束补偿：

- 编排收敛到唯一入口 `generate_world()`，顺序写死在一处
- 每个阶段函数的 doxygen 写明它读哪些字段

### 8.3 就地改写而非命名中间类型管线

为每个阶段定义专属类型（`ElevationMap` / `ClimateMap` / ...）能把依赖关系
写进类型签名，但需要大量零成本的转换代码。本轮的阶段之间本来就在同一个
`World` 上读写同构字段，引入中间类型没有对应收益。

## 9. 构建

三个新目标：

```cmake
add_library(worldgen STATIC ${WORLDGEN_SOURCES})
target_include_directories(worldgen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(worldgen_tests ${WORLDGEN_TEST_SOURCES})
target_link_libraries(worldgen_tests PRIVATE worldgen)

add_executable(worldgen_demo src/demo/main.cpp)
target_link_libraries(worldgen_demo PRIVATE worldgen)
```

`worldgen` 不链接 `ecs`：本轮两者没有交互。

`worldgen_tests` 复用 `tests/main.cpp` 与 `tests/test_framework.hpp`，
两者都是 ECS 无关的通用代码。`main()` 因此会被编译进两个独立可执行文件，
互不冲突。

已验证工具链：CMake 4.4.2 + Unix Makefiles + clang++ 23.1.1 +
MSVC STL 14.51.36231 + Windows SDK 10.0.28000.0。

```powershell
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

全文件 90 列换行，中文 doxygen 注释，沿用 ECS 模块现有风格。

## 10. 与 DF 的差异

| 项 | DF | 本设计 | 原因 |
|---|---|---|---|
| 高程噪声 | 自研 fractal，未公开 | 自研 value-noise fbm | 无法逐像素复现 |
| 海平面 | 按目标海洋面积反解 | 同 | 采用 |
| 风向 | 随季节变化 | 固定常量 | 简化 |
| 矿脉尺寸 | 每矿种各自的尺寸表 | 三个全局尺寸档 | 缺 DF 原始数据，凭空编 36 条不如统一 |
| 洞穴 | 内部实现为随机化 + 平滑 | 随机化 + cellular automata + 连通域清理 | 思路一致 |
| 降水雨影 | 有 | 有（固定风向） | 简化 |
| 河流宽度 | 按汇流量分档 | 同（tiny..huge → 1..5） | 采用 |
| 群系判定 | 高程先判 → 基础群系 = f(排水量, 降水量) → 温度覆写 | 同 | 采用 |
| 排水量 | 泛洪平原计算的产物 | 由水体按湿量降序扩散导出 | 算法未公开，近似 |
| 温度刻度 | DF 自有刻度 | 线性映射 -40°C..+50°C 到 byte 0..255 | 阈值按 DF 原文的 -5°C 换算 |

## 11. 已知限制与风险

### 11.1 噪声与 DF 不逐像素一致

DF 的 fractal 生成器未公开，本模块自研噪声，只能保证形态相近（同倍频结构、
同海陆比、同雨影与水系形态），不能保证与 DF 某个具体世界长得一样。

**影响**：若目标是「像 DF」而非「就是 DF」，无影响。若后续要做像素级对拍，
需要 DF 的 raws 作为基准数据。

### 11.2 阈值与分档是经验值

河流阈值、汇流量分档、占据率、连通域体积下限、尺寸档范围，全是经验初值。
它们无法从原理推出，只能看着 demo 导出的 PPM 调。

**缓解**：demo 程序的存在就是为了让这一步有反馈回路。调参集中在
`include/gamev2/world/tables.hpp` 与各阶段文件顶部的常量区。

### 11.3 无端侧盆地处理

内流盆地（endoreic）是真实存在的地形：本模块允许河流终止于湖泊而非海洋，
因此「全图所有水流最终入海」这条性质不成立，测试 3 相应只断言「无环」而不
断言「全部入海」。

**影响**：世界可能整片都是内流盆地。这符合真实地理，不是缺陷。

### 11.4 就地改写的顺序无编译期保障

阶段 1..10 的顺序只有 `generate_world()` 一处写死，doxygen 写了前置条件，
但编译器不检查。若将来新增阶段，顺序写错会静默产出错误世界。

**缓解**：`generate_world()` 是唯一入口，审查时只需看这一个函数。

### 11.5 单线程生成

256 x 256 x 6 层的完整生成在单线程下是秒级。阶段 5 的洪泛与阶段 8 的
cellular automata 天然可并行，但本轮不并行。

**影响**：生成耗时随面积线性增长。要支持大世界时，先并行阶段 5 与阶段 8；
噪声无状态（见 4.2）保证了并行化不会改变结果。

### 11.6 填平 DEM 未入库

阶段 5 的「下游不高于自身」性质成立于 priority-flood 填平后的 DEM，而填平结果
是阶段 5 的内部量，刻意未存入 `World`。因此该性质无法从外部直接断言，
测试 4 改为断言可从 `World` 观测的等价性质。

**影响**：若后续要做侵蚀或地形可视化，届时填平 DEM 会成为必需的输入，届时
再把它加进 `World` 不迟——加字段是廉价的，而现在加则是无用负担。
