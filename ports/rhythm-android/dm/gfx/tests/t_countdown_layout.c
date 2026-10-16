#include "../nvn/nvn_tex.h"
#include "../tex/nvn_deswizzle.h"
#include "../tex/nvn_bcdec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char **argv){
    CHECK(argc==4);
    nvn_texdesc d={.target=1,.format=0x4d,.flags=4,.width=1232,.height=160,.depth=1,.levels=1};
    nvn_texlayout L;nvn_tex_layout(&d,&L);
    CHECK(L.tileH==2&&L.size==315392);
    unsigned h,z;nvn_tex_level_tiles(&d,0,&h,&z);CHECK(h==2&&z==0);
    /* External original atlas fixture and Pillow BC7 oracle; no generated
     * swizzle/deswizzle round trip can detect this choice of block height. */
    FILE *f=fopen(argv[1],"rb");CHECK(f);unsigned char *raw=malloc(319488);CHECK(fread(raw,1,319488,f)==319488);fclose(f);
    size_t size=1232*160*4;unsigned char *linear=malloc(1232*160),*pixels=malloc(size),*ref=malloc(size);
    CHECK(linear&&pixels&&ref);CHECK(!nvn_deswizzle(&d,0,0,raw,linear,1232*4));
    CHECK(!nvn_bc_decode(d.format,linear,1232*4,1232,160,pixels,1232*4));
    f=fopen(argv[2],"rb");CHECK(f);CHECK(fread(ref,1,size,f)==size);fclose(f);
    unsigned max_error=0;size_t mismatches=0;for(size_t i=0;i<size;i++){unsigned e=abs((int)pixels[i]-ref[i]);if(e>max_error)max_error=e;if(e>1)mismatches++;}
    CHECK(!mismatches);f=fopen(argv[3],"wb");CHECK(f);CHECK(fwrite(pixels,1,size,f)==size);fclose(f);
    d.pkg_block_h=4;nvn_tex_layout(&d,&L);CHECK(L.tileH==3); /* explicit override */
    printf("PASS countdown atlas: 1232x160 BC7, four-GOB layout, 197120 pixels, Pillow oracle max_error=%u, explicit layout preserved\n",max_error);
    free(raw);free(linear);free(pixels);free(ref);return 0;
}
