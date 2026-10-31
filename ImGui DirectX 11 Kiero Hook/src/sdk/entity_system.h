#pragma once

#include <cstdint>

namespace sdk
{
    struct EntityHandle
    {
        [[nodiscard]] std::uint32_t index() const noexcept
        {
            return value & 0x7FFFu;
        }

        std::uint32_t value;
    };

    struct EntityIdentity
    {
        std::uintptr_t entity;
        std::uintptr_t entityClass;
        EntityHandle handle;
        std::uint8_t padding[92];
    };

    static_assert(sizeof(EntityIdentity) == 0x70);

    struct EntityChunk
    {
        EntityIdentity identities[512];
    };

    struct ConcreteEntityList
    {
        EntityChunk* chunks[64];
    };
}
