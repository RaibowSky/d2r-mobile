// 【骨架第三批】剩下那些"确实要走系统服务、而宿主没有该服务"的接管，集中放在这里（不摊进各自的 hle_*.c，
// 好让"移植新增的部分"一处可见）。
//
// 哪些**不做**接管，是根据实测判定的（工具：`F:\ning\.tools\svc_scan.py`，它扫每个函数的地址范围里有没有
// 指向 PLT 导入的 `bl`，从而判断这个子系统是否真的碰系统服务）：
//   · nn::fontll（13 个符号）：58 个定义符号里只有 30 次 PLT 调用，全部指向 fontll 自己或 FSS_*/map_char 回调
//     → **纯库代码**（字体数据由 LoadFont(data,…) 自己喂进来），不接管，SDK 自己的实现就能跑。
//   · nn::swkbd 的那 5 个（MakePreset / SetGuideTextUtf8 / SetInitialTextUtf8 / GetRequired*BufferSize）：
//     只调 memset 与 nn::util 的字符串转换 → 纯配置填写，不接管（真正走服务的是 ShowKeyboard，已经接管了）。
//   · nn::prepo 的 25 个（PlayReport/Array/Struct 的构造、Add、Clear、SetBuffer）：都是本地缓冲管理，只调
//     nn::srepo 的生成器 → 不接管，否则就把 SDK 自己的缓冲格式搞坏了。只有 Save 两个重载会真的去送数据。
//   · nn::os / mem / util / diag / ro：SDK 内部库，不接管。
//
// [Skeleton, third batch] The remaining "this really goes to a system service and the host has no such service"
// takeovers, kept together here so that everything the port adds is visible in one place.
//
// What is deliberately NOT taken over was decided from measurement (`F:\ning\.tools\svc_scan.py` scans each function's
// address range for `bl` to a PLT import, i.e. whether the subsystem really touches a service):
//   · nn::fontll (13 symbols): 58 defined symbols, only 30 PLT calls, all to fontll itself or FSS_*/map_char callbacks
//     -> pure library code (font data is fed in through LoadFont(data,...)); the SDK's own implementation runs fine.
//   · the 5 nn::swkbd helpers: they only call memset and nn::util string conversion -> pure config filling, no takeover
//     (the one that does IPC, ShowKeyboard, is already taken over).
//   · 25 of the 27 nn::prepo symbols: local buffer management (constructors, Add, Clear, SetBuffer) -> no takeover, or the
//     SDK's own buffer format would be broken. Only the two Save overloads actually send anything.
//   · nn::os / mem / util / diag / ro: SDK-internal libraries, no takeover.
#include "hle.h"
#include <stdlib.h>
#include <stdbool.h>
#ifdef DM_GAME_PORT
#include <sys/stat.h>
#include "../android/generated/rhythm_button_font.h"
typedef struct { uint32_t type, reserved; const void *data; uint64_t size; } android_shared_font;
_Static_assert(sizeof(android_shared_font)==24,"SDK SharedFontData ABI");
static android_shared_font android_fonts[3];
static int android_font_count;
static pthread_once_t android_font_once=PTHREAD_ONCE_INIT;
static void android_load_fonts(void) {
    // Private controller glyphs are project-authored platform adaptations.
    // Supply real font bytes to the SDK instead of replacing glyph APIs.
    android_fonts[android_font_count++]=(android_shared_font){.type=1,.data=rhythm_button_font,.size=sizeof rhythm_button_font};
    LOG("ANDROID_SHARED_FONT provider=Rhythm_Button_Symbols bytes=%zu",sizeof rhythm_button_font);
    // The original SDK rejects this device's variable CFF2 CJK collection
    // (LoadFont error 405). Use static TrueType text/symbol faces instead;
    // the game's bundled fonts remain responsible for CJK text.
    char normal[4096],symbols[4096];
    const char *root=getenv("DM_FONT_DIR");if(!root)die("Missing platform font directory");
    snprintf(normal,sizeof normal,"%s/text.ttf",root);snprintf(symbols,sizeof symbols,"%s/symbols.ttf",root);
    const char *paths[]={normal,symbols};
    for(unsigned i=0;i<2;i++){
        FILE *f=fopen(paths[i],"rb");if(!f)continue;
        struct stat st;if(fstat(fileno(f),&st)||st.st_size<12||st.st_size>64*1024*1024){fclose(f);continue;}
        uint8_t *data=malloc((size_t)st.st_size);if(!data){fclose(f);die("Android shared font allocation");}
        int ok=fread(data,1,(size_t)st.st_size,f)==(size_t)st.st_size;fclose(f);
        if(!ok || (memcmp(data,"\0\1\0\0",4)&&memcmp(data,"OTTO",4)&&memcmp(data,"ttcf",4))){free(data);continue;}
        android_fonts[android_font_count++]=(android_shared_font){.type=i?2:0,.data=data,.size=(uint64_t)st.st_size};
        LOG("ANDROID_SHARED_FONT path=%s bytes=%lld header=%02x%02x%02x%02x",paths[i],(long long)st.st_size,data[0],data[1],data[2],data[3]);
    }
}
#endif

// ---------- nn::oe：应用运行环境（真机经 am 服务） ----------
// 宿主没有 applet/am 服务：这些"区段"与查询一律按最宽松的方式回答（不阻止自动休眠、不拦截退出请求）。
// nn::oe: application operating environment (the am service on a real device). The host has no applet/am service, so the
// sections and queries all answer the most permissive way (do not block auto sleep, do not intercept exit requests).
HLE_FORCE("_ZN2nn2oe19IsAutoSleepDisabledEv", "是否禁止自动休眠（bool）；宿主恒 0", unsigned, h_oe_AutoSleepOff, void) { return 0u; }
HLE_FORCE("_ZN2nn2oe29BeginAutoSleepDisabledSectionEv", "进入禁止自动休眠区段；宿主空实现", Result, h_oe_BeginSleep, void) { return RES_OK; }
HLE_FORCE("_ZN2nn2oe27EndAutoSleepDisabledSectionEv", "离开禁止自动休眠区段；宿主空实现", Result, h_oe_EndSleep, void) { return RES_OK; }
HLE_FORCE("_ZN2nn2oe31EnterExitRequestHandlingSectionEv", "进入退出请求处理区段；宿主空实现", Result, h_oe_EnterExit, void) { return RES_OK; }
HLE_FORCE("_ZN2nn2oe31LeaveExitRequestHandlingSectionEv", "离开退出请求处理区段；宿主空实现", Result, h_oe_LeaveExit, void) { return RES_OK; }
// bool 参数：Switch 调用方不扩展，声明成 unsigned 只取低 8 位（hle.h 的约定）
// bool argument: the Switch caller does not extend it, so it is declared unsigned and only the low 8 bits are used.
HLE_FORCE("_ZN2nn2oe38SetUserInactivityDetectionTimeExtendedEb", "延长无操作检测时间；宿主空实现（不接 am 服务）", Result, h_oe_Inactivity, unsigned on) {
    (void)(on & 0xffu); LOG("oe::SetUserInactivityDetectionTimeExtended(%u)：宿主不接 am 服务，忽略", on & 0xffu); return RES_OK;
}

// ---------- nn::pl：平台（共享字体 / 机型查询） ----------
// Android adapts platform fonts to the SDK's 24-byte SharedFontData entries.
// These are Android system fonts, not the original console platform typefaces.
HLE_FORCE("_ZN2nn2pl13GetSharedFontEPiPNS0_14SharedFontDataEiNS_8settings12LanguageCodeE",
          "enumerate real host platform font data", bool, h_pl_GetSharedFont, int *out_n, void *fonts, int max, uint64_t lang) {
    (void)lang;
    if (!out_n) die("GetSharedFont null count output");
#ifdef DM_GAME_PORT
    if(max<0 || (max&&!fonts))die("GetSharedFont invalid output capacity");
    if(pthread_once(&android_font_once,android_load_fonts))die("Android shared font initialization");
    if(!android_font_count)die("No readable Android platform fonts");
    *out_n=android_font_count;
    int count=max<android_font_count?max:android_font_count;
    if(count)memcpy(fonts,android_fonts,(size_t)count*sizeof(android_fonts[0]));
    LOG("pl::GetSharedFont ready=true count=%d copied=%d provider=Android_system_fonts",*out_n,count);
#else
    (void)fonts;(void)max;
    *out_n = 0;
#endif
    return true;
}
HLE_FORCE("_ZN2nn2pl16IsRunningOnOunceEv", "是否 Ounce 机型（bool）；宿主恒 0", unsigned, h_pl_Ounce, void) { return 0u; }

// ---------- nn::prepo：游玩报告（遥测） ----------
// 只接管两个 Save：它们才会真的往 prepo 服务送数据。其余 25 个（构造 / Add / Clear / SetBuffer）是本地缓冲管理，
// 交给 SDK 自己的实现（接管了反而会破坏它的缓冲格式，见文件头）。
// Only the two Save overloads are taken over: they are the ones that would actually send to the prepo service. The other
// 25 (constructors / Add / Clear / SetBuffer) are local buffer management and stay with the SDK's implementation.
HLE_FORCE("_ZN2nn5prepo10PlayReport4SaveEv", "游玩报告上传；宿主无 prepo 服务，丢弃并当成功", Result, h_pg_Save, void *self) {
    (void)self; static int said; if (!said) { said = 1; LOG("prepo::PlayReport::Save：遥测丢弃（宿主无 prepo 服务）"); } return RES_OK;
}
HLE_FORCE("_ZN2nn5prepo10PlayReport4SaveERKNS_7account3UidE", "游玩报告上传（带用户）；同上", Result, h_pg_SaveUid, void *self, const void *uid) {
    (void)self; (void)uid; return RES_OK;
}

// ---------- nn::vi：显示层（补 1 个） ----------
// 默认显示的面板标识：真机从 vi 的 DisplayInfo 里取；宿主没有面板，给 0（首个默认值）并记一行，首次真机跑时核对。
// Default display panel identifier: on a real device it comes from vi's DisplayInfo; the host has no panel, so 0 (the
// first/default value) is returned with a log line, to be checked on the first real run.
HLE_FORCE("_ZN2nn2vi32GetDefaultDisplayPanelIdentifierEPNS0_15PanelIdentifierE", "默认显示的面板标识；宿主给 0", Result, h_vi_PanelId, uint32_t *out) {
    if (out) *out = 0;
    static int said; if (!said) { said = 1; LOG("vi::GetDefaultDisplayPanelIdentifier → 0（宿主无面板，首次跑核对游戏是否在意）"); }
    return RES_OK;
}
