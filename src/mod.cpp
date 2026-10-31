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

#include <unordered_set>

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

    // Diagnostics: report each Ganondorf once, with the params the spawner gave him.
    static std::unordered_set<fpc_ProcID> s_seen;
    if (s_seen.insert(fopAcM_GetID(gnd)).second) {
        mods::log::info("Ganondorf spawned (proc {}): params={:08X} marker={:02X}",
                        fopAcM_GetID(gnd), fopAcM_GetParam(gnd), gnd->field_0x5be);
    }

    if (fopAcM_GetParam(gnd) != kOnFootParam || gnd->field_0x5be != kPendingMarker) {
        return HOOK_CONTINUE;  // normal Ganondorf, or already switched
    }

    // The on-foot code reads the cape actor every frame, so wait until it has spawned.
    mant_class* mant = (mant_class*)fopAcM_SearchByID(gnd->mMantChildID);
    if (mant == nullptr) {
        static fpc_ProcID s_waitLogged = 0xFFFFFFFF;
        if (s_waitLogged != fopAcM_GetID(gnd)) {
            s_waitLogged = fopAcM_GetID(gnd);
            mods::log::info("Ganondorf (proc {}): waiting for cape to spawn", fopAcM_GetID(gnd));
        }
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

// Diagnostics: after each frame of an on-foot Ganondorf, log whenever his state or the
// game's event/cutscene state changes.
struct Trace {
    fpc_ProcID id = 0xFFFFFFFF;
    s16 action = -100, move = -100, demo = -100;
    int event = -1, drawHorse = -1;
};
Trace g_trace;

void on_gnd_execute_post(ModContext*, void* args, void*, void*) {
    b_gnd_class* gnd = mods::arg<b_gnd_class*>(args, 0);
    if (gnd == nullptr || fopAcM_GetParam(gnd) != kOnFootParam) {
        return;
    }
    Trace now;
    now.id = fopAcM_GetID(gnd);
    now.action = gnd->mActionMode;
    now.move = gnd->mMoveMode;
    now.demo = gnd->mDemoCamMode;
    now.event = dComIfGp_event_runCheck() ? 1 : 0;
    now.drawHorse = gnd->mDrawHorse ? 1 : 0;
    if (now.id != g_trace.id || now.action != g_trace.action || now.move != g_trace.move ||
        now.demo != g_trace.demo || now.event != g_trace.event || now.drawHorse != g_trace.drawHorse)
    {
        mods::log::info("GND trace proc {}: action={} move={} demoCam={} eventRunning={} horse={} "
                        "hp={} playerDist={:.0f}",
                        now.id, now.action, now.move, now.demo, now.event, now.drawHorse,
                        (int)gnd->health, fopAcM_searchPlayerDistanceXZ(gnd));
        g_trace = now;
    }
}

}  // namespace

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError*) {
    ModResult result = mods::hook::add_pre<GndExecute>(on_gnd_execute_pre);
    if (result != MOD_OK) {
        mods::log::error("failed to hook daB_GND_Execute ({})", static_cast<int>(result));
        return result;
    }

    result = mods::hook::add_post<GndExecute>(on_gnd_execute_post);
    if (result != MOD_OK) {
        mods::log::warn("failed to install trace post-hook ({})", static_cast<int>(result));
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
