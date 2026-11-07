#pragma once
#include <string>

// Stub — cs2internal has no sidebar notification UI. Randomizer messages are
// silently dropped rather than trying to draw them through Unboxer's renderer.
namespace Notify
{
    inline void Push(std::string /*text*/) {}
}
