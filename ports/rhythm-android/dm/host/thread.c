// 摰Ｖ?蝥輻??臬?嚗瑽?= 摰蹂蜓?典???x18 瑽賣??塚?tpidr_el0 瑽?= ELF TLS ??tpidrro_el0 瑽?= Horizon ThreadLocalRegion嚗?x200嚗?
// SDK ???其葩?隞?TLR[0x1f8] ???瑪蝔笆鞊⊥???餈?蝏?蝥輻???銝銝芷???笆鞊∪僎???
// Guest thread environment: gate slot = host gate entry; x18 slot cleared; tpidr_el0 slot = ELF TLS block; tpidrro_el0 slot = Horizon ThreadLocalRegion (0x200).
// The SDK's internal critical section takes the current thread object pointer from TLR[0x1f8]; here each thread gets a zero-initialized object allocated and written in.
#include "dm.h"
#define TLS_SIZE 8192
#define TLR_SIZE 0x200
#define THREAD_OBJ_SIZE 0x400
extern void dm_gate(void);            // entry.S嚗?砍?亙 | entry.S: common gate entry
#include "front.h"
// 【移植】dm_game_res：宣告本來在 dm_uiscreen.h（D2 的檔，這棵樹沒有）；Android 上由 diagnostics.c 定義
// （uint64_t dm_game_res(int docked)）。宣告放這裡，兩個平台都成立（Apple 那邊 dm_uiscreen.h 也有一份，相同型別）。
// [port] dm_game_res used to be declared in dm_uiscreen.h (a D2 file, not in this tree); on Android it is defined by
// diagnostics.c. The declaration lives here for both platforms (the identical one is in dm_uiscreen.h on Apple).
uint64_t dm_game_res(int docked);
#ifndef __ANDROID__
static pthread_key_t g_keyown;   // ?祉瑪蝔霂瑕??TLS ??/ 蝥輻??砍?箇?????嚗??賣?怠偏嚗?| raw pointers of the TLS block / thread-local region allocated by this thread (see the end of the function)
static pthread_once_t g_keyown_once = PTHREAD_ONCE_INIT;
static void own_free(void *p) { void **own = p; free(own[0]); free(own[1]); free(own); }
static void own_key_init(void) { if (pthread_key_create(&g_keyown, own_free)) die("guest TLS ownership key allocation failed"); }
#endif
void guest_thread_init(void) {
    pthread_setspecific(g_keygate, (void *)dm_gate);
    for (int i = 0; i < 2; i++) if (g_keyres[i]) pthread_setspecific(g_keyres[i], (void *)dm_game_res(i));   // ?儘???唬?嚗u32 摰? u32 擃 ?? | resolution parameter slot: {u32 width, u32 height} packed
    if (g_keyaspect || g_keyaspectui) {   // 摰賡?瘥????唬?嚗ax(19/9, 銝斤?璅∪?颲摰?/ 擃?嚗loat 雿芋撘雿?32 雿?| aspect ratio limit parameter slot: max(19/9, output width / height of the two modes), float bit pattern in the low 32 bits
        float a = 19.0f / 9.0f; int wide = 0;
        for (int i = 0; i < 2; i++) { uint64_t r = front_game_res(i); float w = (float)(uint32_t)r, h = (float)(uint32_t)(r >> 32); if (h > 0 && w / h > a) { a = w / h; wide = 1; } }
        // ?颲寞??脣????撅?摰賭? 19:9 ?嗅??0.15嚗?颲寞????颲寞?箏?撟?16:9 銝???
        // ?典紡嚗器?勗?蝻?x = 撅? ? 銝? / 2嚗??????嚗?蝻?靘批?蝘?撅? ? ? / 2?ac 1952x900 摰??器摰賢漲 ??30 ??
        // 嚗???= 撅?瘥??嗅蝻洵 0??0 ?暺?鈭殷?銝??典 2.4 ?單?憭梧???? ? ??2 ? 30 / 900 ??0.067嚗? 0.15嚗?靘批?蝘餌漲 67 ??嚗??箔??誑銝???
        // 銝擳嚗?擃?憭扳雿???靘???? ?舐撖孵?擃???
        // Limit of the UI side-pillar shader: when the screen is wider than 19:9 add another 0.15, pushing the pillars' inward soft edge off the screen; 16:9 is unchanged.
        // Derivation: pillar inner edge x = screen height * limit / 2, so for every delta added to the limit the inner edge moves outward by screen height * delta / 2 on each side. Measured on Mac 1952x900, the soft edge is <= 30 pixels wide
        // (with limit = screen aspect ratio, columns 0??0 at the right edge fade from black to bright, and it disappears once the limit is pushed to 2.4) => need delta >= 2 * 30 / 900 ~ 0.067; 0.15 is taken (about 67 pixels outward per side), leaving more than double the margin.
        // Not a magic number: when the screen height grows the margin stays proportional (delta is relative to screen height)
        float ui = wide ? a + 0.15f : a;
        uint32_t bits;
        if (g_keyaspect) { memcpy(&bits, &a, 4); pthread_setspecific(g_keyaspect, (void *)(uintptr_t)bits); }
        if (g_keyaspectui) { memcpy(&bits, &ui, 4); pthread_setspecific(g_keyaspectui, (void *)(uintptr_t)bits); }
    }
    pthread_setspecific(g_key18, 0);
#ifdef __ANDROID__
    /* thread_slots.c already owns all three allocations and releases them on
     * explicit detach or pthread exit. Do not register a second destructor,
     * or merge TLR/thread storage while that owner frees them separately. */
    pthread_setspecific(g_keytls, calloc(1, TLS_SIZE));
    uint8_t *tlr = calloc(1, TLR_SIZE);
    *(void **)(tlr + 0x1f8) = calloc(1, THREAD_OBJ_SIZE);
    pthread_setspecific(g_keytlr, tlr);
    dm_android_slots_sync();
#else
    // 餈舅??蝥輻???粹??整恥雿??孵? TLS / 蝥輻??砍?箄?銝斗??桃?瑽踝?摰撌梯挽蝥輻???嚗??隞乩??賣??桅????澆 free嚗?
    // ?唾窈?啁??????西扇?冽?辣?芸楛? g_keyown 銝??勗????嚗????pthread_exit ?嗉??剁??縑?瑕??冽? crash.c altstack_free嚗?
    // ?????free嚗恥雿瑪蝔?撠?銝?摰嚗????橘?皜豢?憸?韏瑞?賜瑪蝔?瘥瞍?8192 + 0x200 + 0x400 = 9728 摮???
    // 蝥輻??砍?箔?敶?蝥輻?撖寡情撟嗆?銝??TLR[0x1f8] ?????SDK 銋?賣??
    // These two blocks are freed when the thread exits. The guest overwrites the slots of the TLS / thread-local-region keys (it sets its own thread pointer), so they cannot be freed by the current value in the key:
    // the raw pointers that were allocated are recorded separately on this file's own key g_keyown and freed by its destructor (key destructors are called at pthread_exit, same as the signal alternate stack, crash.c altstack_free;
    // the destructor only calls free, which is safe on the small stack of a guest thread as well). They used not to be freed: the game starts short-lived threads frequently, and each leaked 8192 + 0x200 + 0x400 = 9728 bytes.
    // The thread-local region and the current thread object are merged into one block (the slot TLR[0x1f8] may also be overwritten by the SDK)
    void **own = calloc(2, sizeof *own); uint8_t *tlr = calloc(1, TLR_SIZE + THREAD_OBJ_SIZE);
    own[0] = calloc(1, TLS_SIZE); own[1] = tlr;
    *(void **)(tlr + 0x1f8) = tlr + TLR_SIZE;               // 敶?蝥輻?撖寡情 | current thread object
    pthread_once(&g_keyown_once, own_key_init); pthread_setspecific(g_keytls, own[0]); pthread_setspecific(g_keytlr, tlr); pthread_setspecific(g_keyown, own);
#endif
}
