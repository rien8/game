#include "gamev2/ecs/command_buffer.hpp"

#include <string>

namespace gamev2::ecs {

void CommandBuffer::flush(World& world)
{
    for (const auto& cmd : commands_)
    {
        // create 命令没有目标：它的目标就是待创建的新实体本身，不能套用其余
        // 命令的「目标已失效则拒绝」检查。其余命令即便误传 EntityId{} 也照样
        // 受检——它是一个已失效的目标，不是「无目标」。destroy 是唯一的
        // 豁免：目标已失效时静默跳过，与 World::destroy 的判法一致。
        if (cmd.target && !world.is_alive(*cmd.target))
        {
            if (!cmd.idempotent_if_dead)
            {
                world.report(Diagnostic{
                    DiagnosticLevel::error,
                    "ecs.entity_invalid",
                    std::string{"命令目标实体已失效，命令被拒绝"}});
            }
            continue;
        }
        cmd.apply(world);
    }
    commands_.clear();
}

}  // namespace gamev2::ecs
