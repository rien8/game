#include "gamev2/genetics/survival.hpp"

#include "test_framework.hpp"

using namespace gamev2::genetics;

namespace {

Phenotype make_phenotype(float size, float speed, float metabolism,
                         float sense, float fecundity, float resilience)
{
    Phenotype p{};
    p.traits[trait_index(TraitKind::BodySize)] = size;
    p.traits[trait_index(TraitKind::Speed)] = speed;
    p.traits[trait_index(TraitKind::Strength)] = 1.0f;
    p.traits[trait_index(TraitKind::Metabolism)] = metabolism;
    p.traits[trait_index(TraitKind::Lifespan)] = 10.0f;
    p.traits[trait_index(TraitKind::SenseRange)] = sense;
    p.traits[trait_index(TraitKind::Fecundity)] = fecundity;
    p.traits[trait_index(TraitKind::Resilience)] = resilience;
    return p;
}

}  // namespace

TEST(survival_is_a_probability)
{
    const auto p = make_phenotype(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    const auto env = EnvironmentSnapshot{};
    const auto r = survival_rate(SurvivalInput{p, env, 1.0f, 0.0f},
                                 GeneticsConfig{});
    CHECK(r >= 0.0f);
    CHECK(r <= 1.0f);
}

TEST(survival_drops_as_metabolism_rises)
{
    const auto env = EnvironmentSnapshot{};
    const auto lean = make_phenotype(1.0f, 1.0f, 0.5f, 1.0f, 1.0f, 1.0f);
    const auto hungry = make_phenotype(1.0f, 1.0f, 4.0f, 1.0f, 1.0f, 1.0f);
    const auto config = GeneticsConfig{};
    CHECK(survival_rate(SurvivalInput{lean, env, 1.0f, 0.0f}, config)
          > survival_rate(SurvivalInput{hungry, env, 1.0f, 0.0f}, config));
}

TEST(survival_drops_as_temperature_moves_away)
{
    auto config = GeneticsConfig{};
    config.thermal_base_width = 1.0f;
    config.thermal_resilience_gain = 1.0f;
    const auto p = make_phenotype(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);

    const auto mild = EnvironmentSnapshot{0.5f, 1.0f, 1.0f};
    const auto harsh = EnvironmentSnapshot{20.0f, 1.0f, 1.0f};
    CHECK(survival_rate(SurvivalInput{p, mild, 1.0f, 0.0f}, config)
          > survival_rate(SurvivalInput{p, harsh, 1.0f, 0.0f}, config));
}

TEST(survival_resilience_widens_thermal_tolerance)
{
    auto config = GeneticsConfig{};
    config.thermal_base_width = 1.0f;
    config.thermal_resilience_gain = 1.0f;
    const auto fragile = make_phenotype(1.0f, 1.0f, 0.2f, 2.0f, 2.0f, 0.0f);
    const auto tough = make_phenotype(1.0f, 1.0f, 0.2f, 2.0f, 2.0f, 5.0f);
    const auto env = EnvironmentSnapshot{4.0f, 1.0f, 1.0f};
    CHECK(survival_rate(SurvivalInput{fragile, env, 1.0f, 0.0f}, config)
          < survival_rate(SurvivalInput{tough, env, 1.0f, 0.0f}, config));
}

TEST(survival_drops_under_predation)
{
    const auto env = EnvironmentSnapshot{};
    const auto config = GeneticsConfig{};
    const auto slow = make_phenotype(1.0f, 0.2f, 0.2f, 2.0f, 2.0f, 5.0f);
    const auto fast = make_phenotype(1.0f, 4.0f, 0.2f, 2.0f, 2.0f, 5.0f);
    const float low = survival_rate(SurvivalInput{slow, env, 1.0f, 0.5f},
                                    config);
    const float high = survival_rate(SurvivalInput{fast, env, 1.0f, 0.5f},
                                     config);
    CHECK(high > low);
}

TEST(survival_is_monotone_in_feeding)
{
    const auto env = EnvironmentSnapshot{};
    const auto config = GeneticsConfig{};
    const auto p = make_phenotype(1.0f, 1.0f, 2.0f, 1.0f, 1.0f, 1.0f);
    const auto starving = survival_rate(SurvivalInput{p, env, 0.2f, 0.0f},
                                        config);
    const auto fed = survival_rate(SurvivalInput{p, env, 1.0f, 0.0f}, config);
    CHECK(fed > starving);
}

TEST(fitness_normalizes_against_population_max)
{
    const auto p = make_phenotype(1.0f, 1.0f, 1.0f, 1.0f, 4.0f, 1.0f);
    CHECK(fitness(0.5f, p, 4.0f) == 0.5f);
    CHECK(fitness(1.0f, p, 4.0f) == 1.0f);
}

TEST(fitness_clamps_an_understated_population_max)
{
    const auto p = make_phenotype(1.0f, 1.0f, 1.0f, 1.0f, 4.0f, 1.0f);

    // max_fecundity 按契约是种群的最大 Fecundity，所以 Fecundity / max_fecundity
    // <= 1，正常输入永远碰不到上界，clamp 形同虚设。这里故意传一个偏小的
    // 分母（调用方算错种群最大值），使比值变成 4，锁住 clamp 的防御性契约。
    CHECK(fitness(1.0f, p, 1.0f) <= 1.0f);
}

TEST(fitness_falls_back_to_survival_when_population_max_is_zero)
{
    const auto p = make_phenotype(1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f);
    CHECK(fitness(0.7f, p, 0.0f) == 0.7f);
}

TEST(outlived_is_a_hard_death_condition)
{
    auto p = make_phenotype(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    p.traits[trait_index(TraitKind::Lifespan)] = 5.0f;
    CHECK(!outlived(p, 4));
    CHECK(outlived(p, 5));
}

TEST(survival_stays_a_probability_with_zero_metabolic_burn)
{
    const auto p = make_phenotype(1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    const auto env = EnvironmentSnapshot{};
    // 摄食与消耗同为 0：不再烧能的个体不该因断食而死，但 0/0 会产出 NaN
    // 并顺着乘积污染整个存活率。
    const auto r = survival_rate(SurvivalInput{p, env, 0.0f, 0.0f},
                                 GeneticsConfig{});
    CHECK(r == 1.0f);
}

TEST(survival_is_zero_when_the_thermal_width_collapses)
{
    auto config = GeneticsConfig{};
    config.thermal_base_width = -1.0f;
    config.thermal_resilience_gain = 0.0f;
    const auto p = make_phenotype(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);

    // 负半宽若不做早退，1 - |T| / width 会反过来变大，任何温度都判定为适生。
    const auto r = survival_rate(
        SurvivalInput{p, EnvironmentSnapshot{}, 1.0f, 0.0f}, config);
    CHECK(r == 0.0f);
}

TEST(survival_does_not_reward_temperature_below_zero)
{
    auto config = GeneticsConfig{};
    config.thermal_base_width = 1.0f;
    config.thermal_resilience_gain = 1.0f;
    const auto p = make_phenotype(1.0f, 1.0f, 0.2f, 2.0f, 2.0f, 0.0f);

    // 约束是 |temperature| <= width：严寒与酷热一样致死。若漏掉绝对值，
    // 负温度会被当成负的偏移距离，抵消掉热量惩罚。
    const auto freezing = EnvironmentSnapshot{-4.0f, 1.0f, 1.0f};
    const auto scorching = EnvironmentSnapshot{4.0f, 1.0f, 1.0f};
    CHECK(survival_rate(SurvivalInput{p, freezing, 1.0f, 0.0f}, config)
          == survival_rate(SurvivalInput{p, scorching, 1.0f, 0.0f}, config));
}

TEST(escape_responsiveness_scales_the_escape_margin)
{
    auto config = GeneticsConfig{};
    config.escape_responsiveness = 0.5f;
    const auto p = make_phenotype(1.0f, 1.0f, 0.2f, 2.0f, 2.0f, 5.0f);

    // 逃脱余量是连续量，转成概率的快慢必须由配置决定：系数丢失后
    // 任何余量都会被直接判成满值逃脱。
    const auto r = survival_rate(
        SurvivalInput{p, EnvironmentSnapshot{}, 1.0f, 0.0f}, config);
    CHECK(r == 0.5f);
}

TEST(escape_survival_cannot_exceed_one)
{
    const auto p = make_phenotype(1.0f, 4.0f, 0.01f, 1.0f, 1.0f, 1.0f);
    const auto env = EnvironmentSnapshot{};

    // headroom = Speed - threat * BodySize = 4 - 0 = 4，escape_responsiveness
    // 默认为 1，未截断时逃脱项就是 4。能量项（消耗 0.05 < 摄入 1）与热项
    // （温度 0）都已饱和到 1，三者之积因此直接等于 4，漏掉 clamp 必然越界。
    const auto r = survival_rate(SurvivalInput{p, env, 1.0f, 0.0f},
                                 GeneticsConfig{});
    CHECK(r <= 1.0f);
}
