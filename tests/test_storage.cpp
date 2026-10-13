#include "test_framework.hpp"

#include "gamev2/ecs/storage.hpp"

#include <cstdlib>
#include <vector>

using gamev2::ecs::Column;
using gamev2::ecs::ComponentTypeInfo;
using gamev2::ecs::compute_chunk_capacity;
using gamev2::ecs::component_id_of;
using gamev2::ecs::global_component_registry;

namespace {

struct Slot
{
    int value = 0;
};

struct Tiny
{
    char raw = 0;
};

struct Wide
{
    double x = 0.0;
};

/// 局部类不允许静态数据成员，故计数器挂在命名空间作用域。
struct Counted
{
    static inline int destructions = 0;
    int value = 0;

    Counted& operator=(const Counted&) = default;
    ~Counted() { ++destructions; }
};

/// 自定义移动构造函数的自增计数组件。行迁移必须走移动构造：把字节搬进
/// RAW 存储不会调用任何构造函数，这个计数就停在 0。
///
/// 同时给出移动赋值，是为了让「实现退化成移动赋值」这种错误仍能编译，
/// 从而被断言抓住而不是变成一条编译错误。
struct MoveTracked
{
    static inline int moves = 0;
    int value = 0;

    MoveTracked() = default;
    MoveTracked(const MoveTracked& other) : value{other.value} {}
    MoveTracked(MoveTracked&& other) noexcept : value{other.value} { ++moves; }

    MoveTracked& operator=(const MoveTracked&) = default;
    MoveTracked& operator=(MoveTracked&&) = default;
};

/// 每个 Tag 一份析构计数，用 archetype 的多列场景。
template<int Tag>
struct CountedTag
{
    static inline int destructions = 0;
    int value = 0;

    CountedTag() = default;
    CountedTag(const CountedTag&) = default;
    CountedTag(CountedTag&&) noexcept = default;
    ~CountedTag() { ++destructions; }

    CountedTag& operator=(const CountedTag&) = default;
    CountedTag& operator=(CountedTag&&) = default;
};

struct TagA
{
    int a = 0;
};

struct TagB
{
    int b = 0;
};

/// 先取 id 再遍历：id 的首次求值会注册新类型并可能触发 infos_ 重新分配，
/// 若注册发生在遍历中，span 指向的旧缓冲会失效。
template<typename T>
const ComponentTypeInfo& type_info_of()
{
    const auto id = component_id_of<T>();
    for (const auto& info : global_component_registry().types())
    {
        if (info.id == id)
        {
            return info;
        }
    }
    std::abort();
}

}  // namespace

TEST(chunk_capacity_uses_largest_component)
{
    const std::vector<gamev2::ecs::ComponentId> ids{
        component_id_of<Slot>(), component_id_of<Wide>(),
        component_id_of<Tiny>()};
    // 最大者刻意置于中间：取首个或取末个都会得到别的容量，
    // 只有真正取各 id 的最大尺寸才得到 4096 / 8。
    CHECK_EQ(compute_chunk_capacity(ids), std::size_t{512});
}

TEST(chunk_capacity_never_below_one)
{
    struct Huge
    {
        char padding[8192];
    };
    const std::vector<gamev2::ecs::ComponentId> ids{component_id_of<Huge>()};
    CHECK_EQ(compute_chunk_capacity(ids), std::size_t{1});
}

TEST(column_construct_and_read_back)
{
    Column col{type_info_of<Slot>(), 4};
    col.ensure_chunk(0);
    col.construct<Slot>(0, 0, Slot{42});
    CHECK_EQ(col.at<Slot>(0, 0)->value, 42);
}

TEST(column_rows_are_independent)
{
    Column col{type_info_of<Slot>(), 4};
    col.ensure_chunk(0);
    for (std::uint32_t r = 0; r < 4; ++r)
    {
        col.construct<Slot>(0, r, Slot{static_cast<int>(r)});
    }
    for (std::uint32_t r = 0; r < 4; ++r)
    {
        CHECK_EQ(col.at<Slot>(0, r)->value, static_cast<int>(r));
    }
}

TEST(column_chunks_are_independent)
{
    Column col{type_info_of<Slot>(), 2};
    col.ensure_chunk(0);
    col.ensure_chunk(1);
    // 同行号：两块缓冲若重叠，偏移相同才会立刻串扰。
    col.construct<Slot>(0, 0, Slot{1});
    col.construct<Slot>(1, 0, Slot{2});
    CHECK_EQ(col.at<Slot>(0, 0)->value, 1);
    CHECK_EQ(col.at<Slot>(1, 0)->value, 2);
    CHECK_EQ(col.chunk_count(), std::size_t{2});
}

TEST(column_growth_preserves_existing_data)
{
    Column col{type_info_of<Slot>(), 2};
    col.ensure_chunk(0);
    col.construct<Slot>(0, 0, Slot{99});

    // 必须在扩容前取地址并留住它：扩容后重新调用 at() 拿到的会是新
    // 地址，即使数据被搬走也照样断言成功，因此验证不了指针稳定性。
    const Slot* const before = col.at<Slot>(0, 0);

    // 触发外层 vector 扩容，验证已有 chunk 的地址仍然有效。
    for (std::uint32_t c = 1; c < 32; ++c)
    {
        col.ensure_chunk(c);
    }

    CHECK_EQ(col.at<Slot>(0, 0), before);
    CHECK_EQ(before->value, 99);
}

/// 类型擦除的同列原语：目标行只分配不构造，再移动构造进去。
///
/// 断言移动构造计数而不只是读值：字节搬运同样能让读值正确，但它不会
/// 调用构造函数，两者只在这里才分得开。
TEST(column_move_construct_into_raw_transfers_value)
{
    Column col{type_info_of<MoveTracked>(), 4};
    col.ensure_chunk(0);
    col.construct<MoveTracked>(0, 1);
    col.at<MoveTracked>(0, 1)->value = 55;
    const auto before = MoveTracked::moves;

    col.move_construct_into_raw(0, 3, 0, 1);

    CHECK_EQ(MoveTracked::moves, before + 1);
    CHECK_EQ(col.at<MoveTracked>(0, 3)->value, 55);
}

/// 模板版跨列原语：源列与目标列各自解释自己的坐标。
TEST(column_move_construct_into_raw_from_transfers_value)
{
    Column src{type_info_of<Slot>(), 4};
    src.ensure_chunk(0);
    src.construct<Slot>(0, 0, Slot{33});

    Column dst{type_info_of<Slot>(), 4};
    dst.ensure_chunk(0);
    dst.move_construct_into_raw_from<Slot>(src, 0, 1, 0, 0);

    CHECK_EQ(dst.at<Slot>(0, 1)->value, 33);
}

TEST(column_destroy_row_runs_destructor)
{
    Column col{type_info_of<Counted>(), 4};
    col.ensure_chunk(0);
    // 用具名左值，避免实参的临时对象在构造后立刻析构而污染计数。
    Counted src{7};
    col.construct<Counted>(0, 0, src);
    CHECK_EQ(col.at<Counted>(0, 0)->value, 7);
    CHECK_EQ(Counted::destructions, 0);

    col.destroy_row(0, 0);

    CHECK_EQ(Counted::destructions, 1);
}

TEST(column_pop_chunk_releases_tail)
{
    Column col{type_info_of<Slot>(), 2};
    col.ensure_chunk(0);
    col.ensure_chunk(1);
    CHECK_EQ(col.chunk_count(), std::size_t{2});
    col.pop_chunk();
    CHECK_EQ(col.chunk_count(), std::size_t{1});
}

using gamev2::ecs::Archetype;
using gamev2::ecs::ComponentId;
using gamev2::ecs::EntityId;
using gamev2::ecs::Storage;
using gamev2::ecs::kNoIndex;

TEST(archetype_sorts_and_dedupes_key)
{
    const auto a = component_id_of<TagA>();
    const auto b = component_id_of<TagB>();
    Archetype arche{{b, a, b}, 4};
    const auto key = arche.key();
    CHECK_EQ(key.size(), std::size_t{2});
    CHECK(key[0].value <= key[1].value);
}

TEST(archetype_reports_chunk_count)
{
    Archetype arche{{component_id_of<TagA>()}, 4};
    CHECK_EQ(arche.chunk_count(), 0u);

    for (int i = 0; i < 3; ++i)
    {
        arche.push_back(EntityId::make(static_cast<std::uint32_t>(i), 1));
    }
    CHECK_EQ(arche.chunk_count(), 1u);
    CHECK_EQ(arche.count_in(0), std::size_t{3});
}

TEST(archetype_all_chunks_full_except_last)
{
    Archetype arche{{component_id_of<TagA>()}, 4};
    for (int i = 0; i < 10; ++i)
    {
        arche.push_back(EntityId::make(static_cast<std::uint32_t>(i), 1));
    }
    CHECK_EQ(arche.chunk_count(), 3u);
    CHECK_EQ(arche.count_in(0), std::size_t{4});
    CHECK_EQ(arche.count_in(1), std::size_t{4});
    CHECK_EQ(arche.count_in(2), std::size_t{2});
}

TEST(archetype_pop_back_drops_empty_tail_chunk)
{
    Archetype arche{{component_id_of<TagA>()}, 4};
    for (int i = 0; i < 8; ++i)
    {
        arche.push_back(EntityId::make(static_cast<std::uint32_t>(i), 1));
    }
    CHECK_EQ(arche.chunk_count(), 2u);
    for (int i = 0; i < 4; ++i)
    {
        arche.pop_back();
    }
    CHECK_EQ(arche.chunk_count(), 1u);
    CHECK_EQ(arche.entity_count(), std::size_t{4});
}

/// 补 archetype_pop_back_drops_empty_tail_chunk 的盲区：那条只查
/// chunk_count 与 entity_count，而两者都是 count_ 的纯函数，坏实现下
/// 取值与好实现完全相同——用例名承诺的「drops empty tail chunk」正是它
/// 唯一看不见的物理存储。
///
/// 直接给容量 4 而不走 compute_chunk_capacity，才能让 count_ % capacity_
/// 这个 chunk 边界真的被跨过。
TEST(archetype_pop_back_frees_only_truly_empty_chunk)
{
    Archetype arche{{component_id_of<TagA>()}, 4};
    auto& col = arche.column_at(0);
    for (int i = 0; i < 8; ++i)
    {
        arche.push_back(EntityId::make(static_cast<std::uint32_t>(i), 1));
        col.construct<TagA>(static_cast<std::uint32_t>(i / 4),
                            static_cast<std::uint32_t>(i % 4), TagA{i});
    }

    // 8 -> 5：删掉的是行 7、6、5，chunk 1 仍装存活行 4，物理上不能少。
    arche.pop_back();
    arche.pop_back();
    arche.pop_back();

    CHECK_EQ(arche.chunk_count(), 2u);
    // 回收条件若作用在自减前的 count_ 上，8 % 4 == 0 会把 chunk 1 释放，
    // 此处只剩 1 块，物理存储与逻辑视图脱节。
    CHECK_EQ(col.chunk_count(), 2u);

    // 再推两行使行 4、5、6 重新成为存活行。若上一条已经误释放了 chunk 1，
    // ensure_chunk 会新建一块全零缓冲覆盖掉它们，读数即被静默清零。
    arche.push_back(EntityId::make(90, 1));
    arche.push_back(EntityId::make(91, 1));
    CHECK_EQ(col.at<TagA>(1, 0)->a, 4);
    CHECK_EQ(col.at<TagA>(1, 1)->a, 5);
    CHECK_EQ(col.at<TagA>(1, 2)->a, 6);
}

TEST(archetype_back_points_to_last_row)
{
    Archetype arche{{component_id_of<TagA>()}, 4};
    for (int i = 0; i < 5; ++i)
    {
        arche.push_back(EntityId::make(static_cast<std::uint32_t>(i), 1));
        CHECK_EQ(arche.back_chunk(), static_cast<std::uint32_t>(i / 4));
        CHECK_EQ(arche.back_row(), static_cast<std::uint32_t>(i % 4));
    }
}

TEST(archetype_entity_at_roundtrip)
{
    Archetype arche{{component_id_of<TagA>()}, 2};
    const auto e0 = EntityId::make(10, 1);
    const auto e1 = EntityId::make(11, 1);
    const auto e2 = EntityId::make(12, 1);
    arche.push_back(e0);
    arche.push_back(e1);
    arche.push_back(e2);
    CHECK(arche.entity_at(0, 0) == e0);
    CHECK(arche.entity_at(0, 1) == e1);
    CHECK(arche.entity_at(1, 0) == e2);
}

TEST(archetype_find_returns_no_index_when_absent)
{
    Archetype arche{{component_id_of<TagA>()}, 4};
    CHECK(arche.find(component_id_of<TagB>()) == kNoIndex);
    CHECK(!arche.has(component_id_of<TagB>()));
}

TEST(storage_find_or_create_is_stable)
{
    Storage storage;
    const std::vector<ComponentId> ids{component_id_of<TagA>()};
    const auto first = storage.find_or_create(ids);
    const auto second = storage.find_or_create(ids);
    CHECK(first == second);
    CHECK(first != gamev2::ecs::kInvalidArchetype);
}

TEST(storage_with_adds_component)
{
    Storage storage;
    const auto base = storage.find_or_create(
        std::span<const ComponentId>{});
    const auto with_a = storage.with(base, component_id_of<TagA>(), true);
    CHECK(storage.at(with_a).has(component_id_of<TagA>()));
    CHECK(!storage.at(base).has(component_id_of<TagA>()));
}

TEST(storage_with_removes_component)
{
    Storage storage;
    const auto both = storage.find_or_create(
        std::vector<ComponentId>{component_id_of<TagA>(),
                                 component_id_of<TagB>()});
    const auto only_a = storage.with(both, component_id_of<TagB>(), false);
    CHECK(storage.at(only_a).has(component_id_of<TagA>()));
    CHECK(!storage.at(only_a).has(component_id_of<TagB>()));
}

/// fill_hole 的前提是空洞已经析构回 RAW —— 生产路径由 World 的迁移先做
/// 析构。这里显式构造每一行再把首行析构掉，走的就是那条路径。
TEST(storage_fill_hole_swaps_tail_entity)
{
    Storage storage;
    const auto aid =
        storage.find_or_create(std::vector<ComponentId>{component_id_of<TagA>()});
    auto& arche = storage.at(aid);

    const auto e0 = EntityId::make(0, 1);
    const auto e1 = EntityId::make(1, 1);
    const auto e2 = EntityId::make(2, 1);
    arche.push_back(e0);
    arche.push_back(e1);
    arche.push_back(e2);
    for (std::uint32_t r = 0; r < 3; ++r)
    {
        arche.column_at(0).construct<TagA>(0, r);
    }
    arche.destroy_row(0, 0);

    const auto moved = storage.fill_hole(arche, 0, 0);
    CHECK(moved == e2);
    CHECK(arche.entity_at(0, 0) == e2);
    CHECK_EQ(arche.entity_count(), std::size_t{2});
}

/// fill_hole 在源 archetype 有多个实体时：尾部实体移动构造进空洞，并析构
/// 它留在原位的那一份。漏掉析构的话，资源计数再也回不到基准值。
TEST(storage_fill_hole_moves_payload_and_destroys_tail)
{
    Storage storage;
    Archetype arche{{component_id_of<CountedTag<0>>()}, 4};
    auto& col = arche.column_at(0);
    const auto before = CountedTag<0>::destructions;
    for (int i = 0; i < 3; ++i)
    {
        const auto row = static_cast<std::uint32_t>(i);
        arche.push_back(EntityId::make(static_cast<std::uint32_t>(i), 1));
        col.construct<CountedTag<0>>(0, row);
        col.at<CountedTag<0>>(0, row)->value = i;
    }

    arche.destroy_row(0, 0);
    const auto moved = storage.fill_hole(arche, 0, 0);

    CHECK(moved == EntityId::make(2, 1));
    CHECK_EQ(col.at<CountedTag<0>>(0, 0)->value, 2);
    CHECK_EQ(arche.entity_count(), std::size_t{2});
    // 源行一次、尾行一次；尾行那份被搬走后仍需析构才不漏资源。
    CHECK_EQ(CountedTag<0>::destructions, before + 2);
}

TEST(storage_fill_hole_on_tail_row_returns_empty)
{
    Storage storage;
    const auto aid =
        storage.find_or_create(std::vector<ComponentId>{component_id_of<TagA>()});
    auto& arche = storage.at(aid);
    arche.push_back(EntityId::make(0, 1));
    arche.push_back(EntityId::make(1, 1));
    // 先构造再析构才符合生产路径：fill_hole 只搬运 RAW 行，从未构造的行
    // 建立不起这套前提，等于给一个生产中不存在的状态做断言。
    arche.column_at(0).construct<TagA>(0, 0);
    arche.column_at(0).construct<TagA>(0, 1);
    arche.destroy_row(0, 1);

    // 容量由 compute_chunk_capacity 算出（TagA 为 4 字节，容量 1024），
    // 两个实体同处 chunk 0，尾行是 (0, 1)，不是 (1, 0)。
    const auto moved = storage.fill_hole(arche, 0, 1);
    CHECK(!moved);
    CHECK_EQ(arche.entity_count(), std::size_t{1});
}

/// 覆盖 C1 的真实触达路径：fill_hole 无条件走 pop_back，因此生产迁移
/// 原语本身就会跨过 count_ % capacity_ == 0 那一刻。
///
/// 只做一次 fill_hole 就停手：再多一次就会去读已被误释放的 chunk 1，
/// 那是越界读（UB），失败方式会变得不确定。这里让失败落在
/// chunk_count 与 payload 两个确定可观测量上。
TEST(storage_fill_hole_keeps_live_chunk_at_capacity_boundary)
{
    Storage storage;
    Archetype arche{{component_id_of<TagA>()}, 4};
    auto& col = arche.column_at(0);
    for (int i = 0; i < 8; ++i)
    {
        arche.push_back(EntityId::make(static_cast<std::uint32_t>(i), 1));
        col.construct<TagA>(static_cast<std::uint32_t>(i / 4),
                            static_cast<std::uint32_t>(i % 4), TagA{i});
    }

    // fill_hole 只能对 RAW 空洞搬运，源行必须先析构。
    arche.destroy_row(0, 0);
    const auto moved = storage.fill_hole(arche, 0, 0);
    CHECK(moved == EntityId::make(7, 1));

    // 8 -> 7，chunk 1 仍装着行 4..6，物理上不能只剩一块。
    CHECK_EQ(arche.chunk_count(), 2u);
    CHECK_EQ(col.chunk_count(), 2u);

    // 若上一条已误释放，下一次 push 会让 ensure_chunk 新建全零缓冲覆盖它。
    arche.push_back(EntityId::make(90, 1));
    CHECK_EQ(col.at<TagA>(1, 0)->a, 4);
}

TEST(storage_fill_hole_on_last_entity_returns_empty)
{
    Storage storage;
    const auto aid =
        storage.find_or_create(std::vector<ComponentId>{component_id_of<TagA>()});
    auto& arche = storage.at(aid);
    arche.push_back(EntityId::make(0, 1));
    // 与 storage_fill_hole_on_tail_row_returns_empty 同理：先构造再析构，
    // 空洞必须是 RAW，否则这条用例建立的是生产里不存在的状态。
    arche.column_at(0).construct<TagA>(0, 0);
    arche.destroy_row(0, 0);

    const auto moved = storage.fill_hole(arche, 0, 0);
    CHECK(!moved);
    CHECK_EQ(arche.entity_count(), std::size_t{0});
}

/// 迁移路径靠它把源行变回 RAW：必须逐列都调用，漏一列就是漏一次析构。
///
/// 空洞必须随即按生产路径填掉再出作用域。~Archetype 假定进入析构时没有
/// RAW 空洞（行只有 RAW / LIVE 两态，它无从分辨，见 storage.hpp），留着
/// 空洞收场只会让同一行被析构两次。
TEST(archetype_destroy_row_runs_every_column_destructor)
{
    const auto id0 = component_id_of<CountedTag<0>>();
    const auto id1 = component_id_of<CountedTag<1>>();
    const auto before0 = CountedTag<0>::destructions;
    const auto before1 = CountedTag<1>::destructions;
    {
        Archetype arche{{id0, id1}, 4};
        const auto c0 = arche.find(id0);
        const auto c1 = arche.find(id1);
        // 多存一行，才能让 fill_hole 搬得动：空洞落在尾部时它只会 pop_back，
        // 那样出作用域时已无存活行，下面那两条析构计数就分不出是谁做的。
        arche.push_back(EntityId::make(1, 1));
        arche.column_at(c0).construct<CountedTag<0>>(0, 0);
        arche.column_at(c1).construct<CountedTag<1>>(0, 0);
        arche.push_back(EntityId::make(2, 1));
        arche.column_at(c0).construct<CountedTag<0>>(0, 1);
        arche.column_at(c1).construct<CountedTag<1>>(0, 1);

        arche.destroy_row(0, 0);

        CHECK_EQ(CountedTag<0>::destructions, before0 + 1);
        CHECK_EQ(CountedTag<1>::destructions, before1 + 1);

        // 与 World 的迁移同序：destroy_row 之后紧跟 fill_hole，尾行被搬进
        // 空洞再原地析构，收尾 pop_back 使行状态重新与 count_ 对齐。
        Storage storage;
        CHECK(storage.fill_hole(arche, 0, 0) == EntityId::make(2, 1));
    }
    // 源行一次、尾行一次（fill_hole 内），出作用域时 ~Archetype 一次。
    // 空洞若还留着，这里会是 before + 4。
    CHECK_EQ(CountedTag<0>::destructions, before0 + 3);
    CHECK_EQ(CountedTag<1>::destructions, before1 + 3);
}

TEST(storage_copy_shared_maps_columns_by_component_id)
{
    Storage storage;
    const auto a = component_id_of<TagA>();
    const auto b = component_id_of<TagB>();
    // 先把两个 archetype 都建出来再取引用：find_or_create 可能让
    // archetypes_ 扩容，中途取得的 Archetype& 随即失效。
    const auto src_id = storage.find_or_create(std::vector<ComponentId>{b});
    const auto dst_id =
        storage.find_or_create(std::vector<ComponentId>{a, b});
    auto& src = storage.at(src_id);
    auto& dst = storage.at(dst_id);

    const auto e = EntityId::make(7, 1);
    src.push_back(e);
    src.column_at(src.find(b)).construct<TagB>(0, 0, TagB{22});
    // 目标行只 push_back 不构造：copy_shared 移动构造进去的正是这块
    // RAW 存储，预构造会让移动构造落在已构造的对象上。
    dst.push_back(EntityId{});

    Storage::copy_shared(src, 0, 0, dst, 0, 0);

    // 共有组件 TagB 在 src 是第 0 列、在 dst 是第 1 列：若按源列号去
    // 索引目标列，值会落进 TagA，下面这条会红。
    CHECK_EQ(dst.column_at(dst.find(b)).at<TagB>(0, 0)->b, 22);
    // 非共有组件不在搬运范围内：目标行该位置仍是 RAW，随后由迁移
    // 路径自己 construct 出来。
    dst.column_at(dst.find(a)).construct<TagA>(0, 0, TagA{1});
    CHECK_EQ(dst.column_at(dst.find(a)).at<TagA>(0, 0)->a, 1);
    CHECK(dst.entity_at(0, 0) == e);
    // 搬运而非转移：源行仍是完好的对象，由调用方另行析构。
    CHECK_EQ(src.column_at(src.find(b)).at<TagB>(0, 0)->b, 22);
}
