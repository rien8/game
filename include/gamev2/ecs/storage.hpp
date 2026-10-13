#pragma once

#include "gamev2/ecs/component.hpp"
#include "gamev2/ecs/entity.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gamev2::ecs {

/// 单 chunk 目标字节数，取页大小，让并行的每个 chunk 尽量落在
/// 独立缓存行组上，降低 false sharing。
inline constexpr std::size_t kChunkTargetBytes = 4096;

/// 按该 archetype 中最大的组件尺寸计算每 chunk 可容纳的实体数。
[[nodiscard]] std::size_t
compute_chunk_capacity(std::span<const ComponentId> ids);

/// 一个 archetype 内某种组件的存储。每个 chunk 一段定长数据。
///
/// 内层用 std::vector<std::byte> 而非裸分配：外层 vector 扩容时移动的是
/// 内层 vector，而移动不搬运数据，因此 chunk 数据地址在扩容后依然有效。
///
/// 代价是每块缓冲的对齐只能依赖 operator new 的基本对齐保证，因此组件
/// 对齐不得超过 alignof(std::max_align_t)，由 register_component 把关。
class Column
{
public:
    Column(const ComponentTypeInfo& info, std::size_t chunk_capacity);

    Column(Column&&) noexcept = default;
    Column& operator=(Column&&) noexcept = default;
    Column(const Column&) = delete;
    Column& operator=(const Column&) = delete;

    [[nodiscard]] std::size_t chunk_count() const noexcept
    {
        return chunks_.size();
    }

    void ensure_chunk(std::uint32_t chunk);
    void pop_chunk();

    /// 取块缓冲的唯一入口，chunk 下界检查因此只需做一次：at / construct /
    /// destroy_row / move_construct_* 全部经由它，Debug 下越界的 chunk 号
    /// 当场被拦下，而不是变成悬垂指针或越界读。
    [[nodiscard]] std::byte* chunk_data(std::uint32_t chunk) noexcept
    {
        assert(chunk < chunks_.size());
        return chunks_[chunk].data();
    }

    [[nodiscard]] const std::byte* chunk_data(std::uint32_t chunk) const noexcept
    {
        assert(chunk < chunks_.size());
        return chunks_[chunk].data();
    }

    template<typename T>
    [[nodiscard]] T* at(std::uint32_t chunk, std::uint32_t row) noexcept
    {
        assert(info_.id == component_id_of<T>());
        assert(row < capacity_);
        return std::launder(
            reinterpret_cast<T*>(chunk_data(chunk) + row * sizeof(T)));
    }

    template<typename T>
    [[nodiscard]] const T* at(std::uint32_t chunk,
                              std::uint32_t row) const noexcept
    {
        assert(info_.id == component_id_of<T>());
        assert(row < capacity_);
        return std::launder(reinterpret_cast<const T*>(
            chunk_data(chunk) + row * sizeof(T)));
    }

    /// 在 (chunk, row) 处构造一个 T，返回其地址。
    template<typename T, typename... Args>
    T* construct(std::uint32_t chunk, std::uint32_t row, Args&&... args)
    {
        assert(info_.id == component_id_of<T>());
        assert(row < capacity_);
        auto* p = chunk_data(chunk) + row * sizeof(T);
        return std::construct_at(reinterpret_cast<T*>(p),
                                 std::forward<Args>(args)...);
    }

    /// 把源行的 LIVE 对象移动构造到本列 (dst_chunk, dst_row) 的 RAW 存储。
    /// 目标行必须是 RAW —— 这是该原语成立的前提：对从未构造的存储做移动
    /// 赋值是未定义行为，见设计文档 4.6。源行与目标行也不可能重合，
    /// 一行只有 RAW 与 LIVE 两种状态。
    void move_construct_into_raw(std::uint32_t dst_chunk,
                                 std::uint32_t dst_row,
                                 std::uint32_t src_chunk,
                                 std::uint32_t src_row);

    /// 跨列版本：把 src 列 (src_chunk, src_row) 的 LIVE 对象移动构造到
    /// 本列的 RAW 存储。两对坐标各自在所属列内解释，所以源列必须显式
    /// 给出——move_construct_into_raw 的两对坐标同属本列。
    ///
    /// 非模板重载服务于 Storage::copy_shared：它是跨 archetype 搬运，
    /// 调用点不知道 T，只能走类型擦除入口。
    void move_construct_into_raw_from(const Column& src, std::uint32_t dst_chunk,
                                     std::uint32_t dst_row,
                                     std::uint32_t src_chunk,
                                     std::uint32_t src_row);

    template<typename T>
    void move_construct_into_raw_from(const Column& src,
                                      std::uint32_t dst_chunk,
                                      std::uint32_t dst_row,
                                      std::uint32_t src_chunk,
                                      std::uint32_t src_row)
    {
        assert(info_.id == component_id_of<T>());
        assert(src.info_.id == info_.id);
        std::construct_at(at<T>(dst_chunk, dst_row),
                          std::move(*std::launder(reinterpret_cast<T*>(
                              raw_ptr(src, src_chunk, src_row)))));
    }

    /// 销毁 (chunk, row) 处的对象。Column 不记录哪些行已构造，析构时
    /// 无从分辨，因此必须由上层为每个已构造行配对调用，它不会自行遍历。
    void destroy_row(std::uint32_t chunk, std::uint32_t row);

private:
    /// 源行的字节地址。两个跨列入口的源参数都是 void*，而搬运只读源行，
    /// 就地还原可写性比改 ComponentTypeInfo 已定型的签名小。
    [[nodiscard]] static std::byte* raw_ptr(const Column& src,
                                            std::uint32_t chunk,
                                            std::uint32_t row)
    {
        return const_cast<std::byte*>(
            src.chunk_data(chunk) + row * src.info_.size);
    }

    ComponentTypeInfo info_{};
    std::size_t capacity_ = 1;
    std::vector<std::vector<std::byte>> chunks_;
};

using ArchetypeId = std::uint32_t;

inline constexpr ArchetypeId kInvalidArchetype =
    std::numeric_limits<ArchetypeId>::max();

/// 组件查找失败时的返回值，避免与合法下标混淆。
inline constexpr std::size_t kNoIndex =
    std::numeric_limits<std::size_t>::max();

/// archetype 索引。上界为同时存在的不同组件组合数。
///
/// 不变式：除最后一个 chunk 外，所有 chunk 永远满载。这样并行遍历时
/// 每个 worker 拿到的都是连续区间，无需处理空洞。
class Archetype
{
public:
    Archetype(std::vector<ComponentId> ids, std::size_t chunk_capacity);

    /// 析构各列中仍处于 LIVE 的行，把它们全部变回 RAW。
    ///
    /// 设计 11.4 明确保留了对持有资源组件的支持，API 表面也在邀请这类组件，
    /// 因此收尾责任必须落到这里：迁移路径只处理「行离开 archetype」的那些，
    /// 随 World 一起消失的行再无任何显式调用会碰到，漏掉就是每关泄一份资源
    /// 且无任何诊断。
    ///
    /// 前置条件：进入析构时不存在 RAW 空洞，即 count_in(c) 覆盖的每一行都
    /// 仍是 LIVE。行只有 RAW / LIVE 两态，Column 没有存活位图、无从分辨
    /// （前提同 Column::destroy_row），本函数因此无法为调用方留下的空洞
    /// 免罪：上层若调 destroy_row 而不随即填补，同一行会被这里再析构一次，
    /// 是未定义行为。生产路径每处 destroy_row 之后都紧跟 fill_hole，前提成立。
    ~Archetype();

    /// 必须显式声明：一旦有了用户提供的析构函数，隐式移动构造/赋值不再生成，
    /// std::vector<Archetype> 扩容就会退化成拷贝，而 Column 的拷贝构造是
    /// deleted，会直接编译失败。
    ///
    /// 移动后源对象不得再析构行。默认移动把各列的缓冲整体交给新对象、源对象
    /// 的 chunks_ 随之为空，而标量 count_ 是逐值复制的旧值；析构函数因此以
    /// 列自身的 chunk_count 为准，不看 count_，被移空的源对象自然无事可做。
    Archetype(Archetype&&) noexcept = default;
    Archetype& operator=(Archetype&&) noexcept = default;

    [[nodiscard]] std::span<const ComponentId> key() const noexcept
    {
        return key_;
    }

    [[nodiscard]] bool has(ComponentId id) const noexcept
    {
        return find(id) != kNoIndex;
    }

    [[nodiscard]] std::size_t find(ComponentId id) const noexcept
    {
        const auto it = std::ranges::find(key_, id);
        if (it == key_.end())
        {
            return kNoIndex;
        }
        return static_cast<std::size_t>(std::distance(key_.begin(), it));
    }

    [[nodiscard]] Column& column_at(std::size_t index) noexcept
    {
        return columns_[index];
    }

    [[nodiscard]] const Column& column_at(std::size_t index) const noexcept
    {
        return columns_[index];
    }

    [[nodiscard]] std::size_t entity_count() const noexcept
    {
        return count_;
    }

    [[nodiscard]] std::size_t chunk_capacity() const noexcept
    {
        return capacity_;
    }

    [[nodiscard]] std::uint32_t chunk_count() const noexcept;

    [[nodiscard]] std::size_t count_in(std::uint32_t chunk) const noexcept;

    void push_back(EntityId e);

    /// 回收尾部一行，并连带回收刚好空了的那块 chunk 缓冲。
    ///
    /// 前置条件：调用方必须已经先把被删行析构回 RAW（destroy_row）。本函数
    /// 不析构行——直接调用之后再 push_back，分回来的是一块仍为 LIVE 的存储，
    /// 随后的 move_construct 就构造在已构造的对象上（UB）。模块自有的调用点
    /// 只有 fill_hole，而所有生产迁移路径都是先析构后填洞。
    void pop_back();

    /// 最后一个已占用行的位置，push_back 之后即刚写入的那一行。
    /// 空 archetype 上无意义。
    [[nodiscard]] std::uint32_t back_chunk() const noexcept;
    [[nodiscard]] std::uint32_t back_row() const noexcept;

    [[nodiscard]] EntityId entity_at(std::uint32_t chunk,
                                     std::uint32_t row) const noexcept
    {
        return entities_[global_row(chunk, row)];
    }

    void set_entity(std::uint32_t chunk, std::uint32_t row, EntityId e)
    {
        entities_[global_row(chunk, row)] = e;
    }

    /// 析构 (chunk, row) 行的全部组件，把 LIVE 行变回 RAW。
    /// 迁移路径靠这一步把源行变回 RAW，填补空洞才能对 RAW 存储做移动构造。
    void destroy_row(std::uint32_t chunk, std::uint32_t row);

private:
    [[nodiscard]] std::size_t global_row(std::uint32_t chunk,
                                         std::uint32_t row) const noexcept
    {
        return static_cast<std::size_t>(chunk) * capacity_ + row;
    }

    std::vector<ComponentId> key_;
    std::size_t capacity_ = 1;
    std::vector<Column> columns_;
    std::vector<EntityId> entities_;
    std::size_t count_ = 0;
};

using ComponentKey = std::vector<ComponentId>;

/// 组件集合键的哈希，FNV-1a。
struct ComponentKeyHash
{
    [[nodiscard]] std::size_t operator()(
        std::span<const ComponentId> ids) const noexcept
    {
        std::size_t h = 14695981039346656037ull;
        for (const auto id : ids)
        {
            h ^= static_cast<std::size_t>(id.value);
            h *= 1099511628211ull;
        }
        return h;
    }
};

/// 以 span 形参接收键，使调用方能直接拿 span 查询而不必先物化
/// 一个 vector 去喂 unordered_map。
struct ComponentKeyEqual
{
    [[nodiscard]] bool operator()(std::span<const ComponentId> a,
                                  std::span<const ComponentId> b) const noexcept
    {
        return std::ranges::equal(a, b);
    }
};

class Storage
{
public:
    [[nodiscard]] ArchetypeId find_or_create(
        std::span<const ComponentId> ids);

    /// 在 current 的组件集上增删一个组件后的 archetype，按需创建。
    [[nodiscard]] ArchetypeId with(ArchetypeId current, ComponentId id,
                                   bool add);

    [[nodiscard]] Archetype& at(ArchetypeId id) noexcept
    {
        return archetypes_[id];
    }

    [[nodiscard]] const Archetype& at(ArchetypeId id) const noexcept
    {
        return archetypes_[id];
    }

    [[nodiscard]] std::span<const Archetype> archetypes() const noexcept
    {
        return archetypes_;
    }

    /// 把源行中与目标共有的组件移动构造进目标行的 RAW 存储，实体句柄一并
    /// 写入。目标行必须处于 RAW 状态，即刚由 push_back 分配出来。
    static void copy_shared(const Archetype& src, std::uint32_t src_chunk,
                            std::uint32_t src_row, Archetype& dst,
                            std::uint32_t dst_chunk, std::uint32_t dst_row);

    /// 用尾部实体填补 src 中已被析构成 RAW 的空洞 (chunk, row)：尾部行移动
    /// 构造进空洞，随即析构原位并回收。返回被填补实体的句柄；空洞本来就在
    /// 尾部时无需填补，返回 EntityId{}。
    [[nodiscard]] EntityId fill_hole(Archetype& src, std::uint32_t chunk,
                                     std::uint32_t row);

private:
    std::vector<Archetype> archetypes_;
    std::unordered_map<ComponentKey, ArchetypeId, ComponentKeyHash,
                       ComponentKeyEqual>
        index_;
};

}  // namespace gamev2::ecs
