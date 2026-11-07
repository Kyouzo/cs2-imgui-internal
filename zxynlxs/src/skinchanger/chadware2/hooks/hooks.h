#pragma once
// Stub — cs2internal has its own hooks; the skinchanger subsystem only
// includes this for forward declarations it does not actually call.

class C_CSWeaponBase;
class WeaponGraphController;

namespace H
{
    // Intentionally empty stub. knife_animation.cpp includes this header but
    // calls none of the H:: hook functions in cs2internal's integration.
}
