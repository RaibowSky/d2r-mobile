#include "../tex/nvn_deswizzle.h"
#include "../nvn/include/nvn.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
    nvn_texdesc d={.target=NVN_TEXTURE_TARGET_2D,.format=NVN_FORMAT_RGBA8,.flags=NVN_TEXTURE_FLAGS_LINEAR,.width=3,.height=2,.depth=1,.levels=1,.stride=32};
    unsigned char storage[64],linear[32],back[64];memset(storage,0xee,sizeof storage);memset(linear,0xcc,sizeof linear);memset(back,0xdd,sizeof back);
    for(unsigned y=0;y<2;y++)for(unsigned x=0;x<12;x++)storage[y*32+x]=(unsigned char)(1+y*20+x);
    CHECK(nvn_deswizzle(&d,0,0,storage,linear,16)==0);
    CHECK(linear[0]==1&&linear[11]==12&&linear[16]==21&&linear[27]==32);
    CHECK(linear[12]==0xcc&&linear[28]==0xcc);
    CHECK(nvn_swizzle(&d,0,0,linear,16,back)==0);
    CHECK(back[0]==1&&back[11]==12&&back[32]==21&&back[43]==32&&back[12]==0xdd);
    CHECK(nvn_deswizzle(&d,0,0,storage,linear,11)==-1);
    CHECK(nvn_deswizzle(&d,1,0,storage,linear,16)==-1);
    d.stride=8;CHECK(nvn_deswizzle(&d,0,0,storage,linear,16)==-1);
    d.stride=0;CHECK(nvn_deswizzle(&d,0,0,storage,linear,16)==0);
    puts("PASS linear texture: padded rows, roundtrip, preserved padding, bounds");return 0;
}
