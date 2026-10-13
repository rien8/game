#include "gamev2/ecs/command_buffer.hpp"

#include <string>

namespace gamev2::ecs {

void CommandBuffer::flush(World& world)
{
    for (const auto& cmd : commands_)
    {
        if (!world.is_alive(cmd.target))
        {
            world.report(Diagnostic{
                DiagnosticLevel::error,
                "ecs.entity_invalid",
                std::string{"命令目标实体已失效，命令被拒绝"}});
            continue;
        }
        cmd.apply(world);
    }
    commands_.clear();
}

}  // namespace gamev2::ecs
