// Ganondorf On Foot
//
// Spawn Ganondorf (actor ID 526 / profile B_gnd) with Params = 0000F007 and he skips the
// horseback phase, starting straight in the on-foot sword duel. Any other Params value leaves
// him completely untouched, so the real final boss is unaffected.
//
// How it works: b_gnd is a single actor that handles both phases. It always spawns on the horse
// (ACTION_HWAIT_1). The game's own debug build has a button combo that jumps to the sword duel;
// this mod applies the same state change once, on the first frame his cape (mant) exists, but
// without the debug code's weather/switch/save-flag side effects.

#include "mods/service.hpp"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.hpp"

// Game includes (d_com_inf_game / m_Do_ext / c_math first: the actor headers rely on them)
#include "d/d_com_inf_game.h"
#include "m_Do/m_Do_ext.h"
#include "SSystem/SComponent/c_math.h"
#include "f_op/f_op_actor_mng.h"
#include "d/actor/d_a_b_gnd.h"
#include "d/actor/d_a_mant.h"
#include "Z2AudioLib/Z2Instances.h"

DEFINE_MOD();

IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(HookService, svc_hook);

// daB_GND_Execute is file-local (static) in d_a_b_gnd.cpp, so hook it by its
// translation-unit alias. It runs once per frame for each Ganondorf.
DEFINE_HOOK_SYMBOL("src/d/actor/d_a_b_gnd.cpp#daB_GND_Execute", int(b_gnd_class*), GndExecute);

namespace {

// Params value that requests the on-foot duel. "F007" = "FOOT".
constexpr u32 kOnFootParam = 0x0000F007;

// b_gnd stores the low byte of its params in field_0x5be and never reads it again,
// so we use it as a "still needs switching" flag and clear it once done.
constexpr u8 kPendingMarker = static_cast<u8>(kOnFootParam & 0xFF);

// Action IDs from the anonymous enum in d_a_b_gnd.cpp.
constexpr s16 kActionWait = 10;  // ACTION_WAIT: on-foot idle, picks attacks from here

HookAction on_gnd_execute_pre(ModContext*, void* args, void*, void*) {
    b_gnd_class* gnd = mods::arg<b_gnd_class*>(args, 0);
    if (gnd == nullptr) {
        return HOOK_CONTINUE;
    }

    if (fopAcM_GetParam(gnd) != kOnFootParam || gnd->field_0x5be != kPendingMarker) {
        return HOOK_CONTINUE;  // normal Ganondorf, or already switched
    }

    // The on-foot code reads the cape actor every frame, so wait until it has spawned.
    mant_class* mant = (mant_class*)fopAcM_SearchByID(gnd->mMantChildID);
    if (mant == nullptr) {
        return HOOK_CONTINUE;
    }

    // Same state change as the debug "skip to sword duel" in d_a_b_gnd.cpp action().
    gnd->mActionMode = kActionWait;
    gnd->mMoveMode = 0;
    gnd->mDrawHorse = FALSE;
    gnd->speedF = 0.0f;
    mant->field_0x3969 = 1;

    gnd->health = 100;
    gnd->field_0x1e0a = 0;
    gnd->shape_angle.x = 0;
    gnd->shape_angle.z = 0;
    gnd->current.angle.x = 0;
    gnd->current.angle.z = 0;

    Z2GetAudioMgr()->bgmStart(Z2BGM_VS_GANON_04, 0, 0);

    gnd->field_0x5be = 0;  // done; never apply again for this actor
    mods::log::info("Ganondorf (proc {}) switched to on-foot duel", fopAcM_GetID(gnd));
    return HOOK_CONTINUE;
}

}  // namespace

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError*) {
    ModResult result = mods::hook::add_pre<GndExecute>(on_gnd_execute_pre);
    if (result != MOD_OK) {
        mods::log::error("failed to hook daB_GND_Execute ({})", static_cast<int>(result));
        return result;
    }

    mods::log::info("Ganondorf On Foot loaded: spawn actor 526 with Params 0000F007");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    return MOD_OK;
}
}
