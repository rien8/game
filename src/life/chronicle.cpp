/// @file chronicle.cpp
/// @brief 众生层：名册的文本导出。
#include "gamev2/life/interface.hpp"

#include <fstream>
#include <string_view>

namespace gamev2::life {
namespace {

std::string_view sect_name(history::Sect sect)
{
    switch (sect)
    {
        case history::Sect::taixuan: return "太玄宗";
        case history::Sect::daxia: return "丹霞谷";
        case history::Sect::hanjiang: return "寒江剑府";
        case history::Sect::wanshou: return "万兽山";
        case history::Sect::qiangnang: return "青囊谷";
        case history::Sect::tianji: return "天机阁";
        case history::Sect::jiuyou: return "九幽宫";
        case history::Sect::unowned: return "散修";
        default: return "?";
    }
}

std::string_view office_name(OfficeKind office)
{
    switch (office)
    {
        case OfficeKind::sect_master: return "宗主";
        case OfficeKind::elder: return "长老";
        case OfficeKind::inner: return "内门";
        case OfficeKind::outer: return "外门";
        case OfficeKind::none: return "无职";
        default: return "?";
    }
}

std::string_view sex_name(Sex sex)
{
    switch (sex)
    {
        case Sex::male: return "男";
        case Sex::female: return "女";
        default: return "?";
    }
}

std::string_view realm_name(Realm realm)
{
    switch (realm)
    {
        case Realm::yinqi: return "引气";
        case Realm::lianqi: return "炼气";
        case Realm::zhuji: return "筑基";
        case Realm::jindan: return "金丹";
        case Realm::yuanying: return "元婴";
        case Realm::huashen: return "化神";
        case Realm::lianxu: return "炼虚";
        case Realm::heti: return "合体";
        case Realm::dacheng: return "大乘";
        default: return "?";
    }
}

}  // namespace

bool export_roster(const Roster& roster, const std::filesystem::path& path)
{
    std::ofstream out(path);
    if (!out)
    {
        return false;
    }

    out << "众生名册 · 灵历 " << roster.year << " · 共 "
        << roster.entries.size() << " 人\n";
    for (const auto& e : roster.entries)
    {
        // 限定查名：e.sect 的 ADL 会把 history::sect_name 也拉进候选，
        // 二者同名重载又都收 Sect，不加限定就是二义。
        out << e.soul << ' ' << e.name << ' ' << sex_name(e.sex) << ' '
            << life::sect_name(e.sect) << ' ' << office_name(e.office) << ' ' << realm_name(e.realm)
            << ' ' << e.age << ' '
            << static_cast<unsigned>(e.temper.greed) << ' '
            << static_cast<unsigned>(e.temper.valor) << ' '
            << static_cast<unsigned>(e.temper.order) << ' '
            << static_cast<unsigned>(e.temper.mercy) << ' '
            << static_cast<unsigned>(e.temper.vigor) << ' '
            << static_cast<unsigned>(e.aptitude.comprehension) << ' '
            << static_cast<unsigned>(e.aptitude.sense) << ' '
            << static_cast<unsigned>(e.aptitude.body) << ' '
            << static_cast<unsigned>(e.aptitude.fortitude) << '\n';
    }
    return out.good();
}

}  // namespace gamev2::life
