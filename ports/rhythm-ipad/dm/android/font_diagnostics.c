// Observe the original SDK renderer; do not replace its font/glyph algorithms.
#include "../host/hle.h"
#include <stdbool.h>
static void *font_original(const char *name) {
    for(int i=0;i<g_nmod;i++)if(!strcmp(g_mod[i].name,"sdk")){
        void *fn=module_sym(&g_mod[i],name);if(fn)return fn;
    }
    die("missing original font function %s",name);
}
HLE_FORCE("_ZN2nn6fontll18ScalableFontEngine7SetFontEPc","observe original font selection",int,
          font_set,void *self,char *font) {
    int (*fn)(void *,char *)=font_original("_ZN2nn6fontll18ScalableFontEngine7SetFontEPc");
    int result=fn(self,font);
    static unsigned count;if(count++<8)LOG("FONT_SET engine=%p font=%p header=%02x%02x%02x%02x result=%d",self,font,
        font?(unsigned char)font[0]:0,font?(unsigned char)font[1]:0,font?(unsigned char)font[2]:0,font?(unsigned char)font[3]:0,result);
    return result;
}
HLE_FORCE("_ZN2nn6fontll18ScalableFontEngine15AcquireGlyphmapEjt","observe original glyph rendering failure",void *,
          font_glyph,void *self,unsigned code,unsigned flags) {
    void *(*fn)(void *,unsigned,unsigned)=font_original("_ZN2nn6fontll18ScalableFontEngine15AcquireGlyphmapEjt");
    void *glyph=fn(self,code,flags&0xffff);
    static unsigned private_count;
    if(code>=0xe000&&code<=0xf8ff&&private_count++<24)LOG("FONT_PRIVATE_GLYPH engine=%p code=%#x glyph=%p",self,code,glyph);
    if(!glyph){int (*get_error)(void *)=font_original("_ZN2nn6fontll18ScalableFontEngine8GetErrorEv");
        LOG("FONT_GLYPH_NULL engine=%p code=%#x flags=%#x error=%d",self,code,flags&0xffff,get_error(self));}
    return glyph;
}
HLE_FORCE("_ZN2nn6fontll18ScalableFontEngine8LoadFontEPcPKvjj","observe original font loading",int,
          font_load,void *self,char *name,const void *data,unsigned index,unsigned flags) {
    int (*fn)(void *,char *,const void *,unsigned,unsigned)=font_original("_ZN2nn6fontll18ScalableFontEngine8LoadFontEPcPKvjj");
    int result=fn(self,name,data,index,flags);
    LOG("FONT_LOAD name=%.48s data=%p index=%u flags=%u result=%d",name?name:"(null)",data,index,flags,result);
    return result;
}
HLE_FORCE("_ZN2nn6fontll18ScalableFontEngine15CheckGlyphExistEj","observe original glyph coverage",bool,
          font_exists,void *self,unsigned code) {
    bool (*fn)(void *,unsigned)=font_original("_ZN2nn6fontll18ScalableFontEngine15CheckGlyphExistEj");
    bool result=fn(self,code);static unsigned count;
    if(count++<16)LOG("FONT_COVERAGE engine=%p code=%#x exists=%d",self,code,result);
    static unsigned char private_seen[0x1900];
    if(code>=0xe000&&code<=0xf8ff&&!private_seen[code-0xe000]){private_seen[code-0xe000]=1;LOG("FONT_PRIVATE_COVERAGE engine=%p code=%#x exists=%d",self,code,result);}
    return result;
}
