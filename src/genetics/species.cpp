// 许可证注释：模块说明在 doc/2026-10-14-genetics-design.md
#include "gamev2/genetics/species.hpp"

namespace gamev2::genetics {

namespace {
/// 上百个物种时全量逐位点统计不可行，注册数上限把这条路径挡住。
constexpr std::size_t kMaxSpecies = 4096;
}  // namespace

std::expected<SpeciesId, SpeciesError>
SpeciesRegistry::register_species(SpeciesMask mask)
{
    if (mask.active.none())
    {
        return std::unexpected(SpeciesError::EmptyMask);
    }
    if (masks_.size() >= kMaxSpecies)
    {
        return std::unexpected(SpeciesError::TooManySpecies);
    }
    masks_.push_back(mask);
    return SpeciesId{static_cast<std::uint32_t>(masks_.size())};
}

const SpeciesMask& SpeciesRegistry::mask(SpeciesId id) const
{
    // 取舍与 population.cpp 里 Parent::index 的越界守卫一致：宁可丢掉这条
    // 信息也不越界读。登记失败只会得到一张全空掩码——调用方（express /
    // summarize）读到的是「该物种没有激活任何位点」，与注册表拒绝空掩码时
    // 的答案相同，而不是一次栈上越界读。
    static const SpeciesMask empty{};
    if (id.value == 0 || id.value > masks_.size())
    {
        return empty;
    }
    return masks_[id.value - 1];
}

}  // namespace gamev2::genetics
