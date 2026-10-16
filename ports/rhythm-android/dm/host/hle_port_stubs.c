// 【骨架】中性实现层：D2R 里这些接口由 diag.c / dm_uiscreen.c / dm_custom.c / dm_i18n.c 提供，那些文件属于 D2 的层，已从骨架删除。
// 这里给「宿主其余部分仍在调用」的每一个接口一个中性实现，让骨架能链接、能启动；将来按本作需要逐个替换成真实现。
//
// [skeleton] Neutral implementations. In D2R these interfaces came from diag.c / dm_uiscreen.c / dm_custom.c / dm_i18n.c,
// all of which belong to D2's layer and were deleted from the skeleton. Every interface the rest of the host still calls
// gets a neutral implementation here so the skeleton links and boots; replace them one by one as this game needs them.
//
// 这个文件按 Makefile 的 hle_*.c 通配自动编入，不需要改 Makefile。
// This file is picked up by the Makefile's hle_*.c wildcard, so the Makefile needs no change for it.
#include "dm.h"
#include "hle.h"
#include <stdint.h>

// （原 diag.c）启动分段的分项计时统计。骨架不收集任何统计。
// Android 上这套统计的家是 fork 的 diagnostics.c —— 这里只在 Apple 树里提供，避免重复符号。
// [port] on Android the fork's diagnostics.c owns this counter, so it is Apple-only here to avoid a duplicate symbol.
#ifndef __ANDROID__
void dm_prof_add(int id, uint64_t ns, uint64_t bytes) { (void)id; (void)ns; (void)bytes; }
#endif

// （原 diag.c）局内服务端拍号。骨架里前端还不知道本作的进局状态，恒返回 UINT64_MAX（= 不在局内），与 hle.h 的约定一致。
// (was diag.c) In-run server tick. The skeleton's front end does not know this game's run state yet, so it always returns
// UINT64_MAX (= not in a run), matching the contract documented in hle.h.
uint64_t dm_server_tick(void) { return UINT64_MAX; }

// （原 diag.c）是否在局内。同上，骨架恒 0。
// (was diag.c) Whether we are in a run. Same as above: always 0 in the skeleton.
int dm_ingame_now(void) { return 0; }

// （原 diag.c）秘境局里拦原版的难度提示（观测门 15）。本作没有观测门，空实现即可。
// (was diag.c) Suppressed the original difficulty notice during a rift run (observation gate 15). No observation gates here, so no-op.
void dm_quiet_difficulty_msg(int on) { (void)on; }

// （原 dm_uiscreen.c）出图分辨率参数位：{u32 宽, u32 高} 打包成一个 u64，写进 key page 的槽。
// 骨架给一个固定的 1280x720，只为让槽里有合法值；本作真正的分辨率来源（屏幕尺寸 / 游戏设置）以后再补。
// (was dm_uiscreen.c) The render-resolution parameter slots: {u32 width, u32 height} packed into one u64 and written into a key-page slot.
// The skeleton uses a fixed 1280x720 so the slot holds a legal value; this game's real source (screen size / game settings) comes later.
uint64_t port_game_res(int i) { (void)i; return (uint64_t)1280 | ((uint64_t)720 << 32); }
