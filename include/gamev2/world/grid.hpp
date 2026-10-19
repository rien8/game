/// @file grid.hpp
/// @brief 稠密二维网格容器。世界的全部地表量与每层地下量都存放在这里。
#pragma once

#include <cstdint>
#include <vector>

namespace gamev2::world {

/// 行主序稠密二维网格。默认构造得到空网格，任何访问都是未定义行为，
/// 边界检查留给调用方：网格尺寸在生成期固定，热路径上再查一遍是纯开销。
template<typename T>
class Grid
{
public:
    Grid() = default;

    Grid(std::uint32_t width, std::uint32_t height, T fill = T{})
        : width_(width),
          height_(height),
          cells_(static_cast<std::size_t>(width) * height, fill)
    {
    }

    [[nodiscard]] T& operator()(std::uint32_t x, std::uint32_t y) noexcept
    {
        return cells_[index_of(x, y)];
    }

    [[nodiscard]] const T& operator()(std::uint32_t x,
                                      std::uint32_t y) const noexcept
    {
        return cells_[index_of(x, y)];
    }

    /// 线性寻址。downstream 这类存格子索引的字段靠它跳转，省掉一次重复
    /// 的 x/y 到索引换算。
    [[nodiscard]] T& at(std::uint32_t linear) noexcept
    {
        return cells_[linear];
    }

    [[nodiscard]] std::uint32_t index_of(std::uint32_t x,
                                         std::uint32_t y) const noexcept
    {
        return y * width_ + x;
    }

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::uint32_t size() const noexcept
    {
        return static_cast<std::uint32_t>(cells_.size());
    }

private:
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::vector<T> cells_;
};

namespace detail {

/// std::vector<bool> 是位压缩特化，其 operator[] 返回代理而非 bool&，
/// 存进 Grid 之后无法满足 T& 的返回契约。用一个可平凡移动的包装体把存储
/// 换回普通 std::vector：语义不变，仍然一格一字节。
struct BoolCell
{
    bool value = false;
};

}  // namespace detail

/// bool 特化。接口与主模板逐字一致，只有底层存储不同。
///
/// 之所以单独特化而不是让 cavern / aquifer 改用 std::uint8_t 网格：那样
/// 会把 `!= 0` 推进到二十多处调用点与断言里，总复杂度反而更高。
template<>
class Grid<bool>
{
public:
    Grid() = default;

    Grid(std::uint32_t width, std::uint32_t height, bool fill = false)
        : width_(width),
          height_(height),
          cells_(static_cast<std::size_t>(width) * height,
                 detail::BoolCell{fill})
    {
    }

    [[nodiscard]] bool& operator()(std::uint32_t x,
                                   std::uint32_t y) noexcept
    {
        return cells_[index_of(x, y)].value;
    }

    [[nodiscard]] const bool& operator()(std::uint32_t x,
                                         std::uint32_t y) const noexcept
    {
        return cells_[index_of(x, y)].value;
    }

    [[nodiscard]] bool& at(std::uint32_t linear) noexcept
    {
        return cells_[linear].value;
    }

    [[nodiscard]] std::uint32_t index_of(std::uint32_t x,
                                         std::uint32_t y) const noexcept
    {
        return y * width_ + x;
    }

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::uint32_t size() const noexcept
    {
        return static_cast<std::uint32_t>(cells_.size());
    }

private:
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::vector<detail::BoolCell> cells_;
};

}  // namespace gamev2::world
