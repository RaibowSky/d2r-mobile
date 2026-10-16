/* Run against the exact built Android shared library, on a real Vulkan GPU.
 * No shaders or game assets are needed: compare every scaled output texel. */
#define _GNU_SOURCE
#include "../nvn/nvn_obj.h"
#include "../nvn/nvn_mtl.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
typedef struct {const char *name;void *fn;} Entry;
static Entry *first,*last;
static void *lookup(const char *name){for(Entry *e=first;e<last;e++)if(!strcmp(e->name,name))return e->fn;fprintf(stderr,"missing %s\n",name);exit(2);}
/* API names are mixed case; typedefs in nvn.h are uppercase. */
#define API(type,name) ((PFNNVN##type##PROC)lookup("nvn" #name))
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char **argv){
    CHECK(argc==2);void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){fprintf(stderr,"%s\n",dlerror());return 2;}
    first=dlsym(lib,"__start_swnvn");last=dlsym(lib,"__stop_swnvn");CHECK(first&&last);
    void (*begin)(void)=dlsym(lib,"mtl_sub_begin");void (*exec)(const uint8_t *)=dlsym(lib,"mtl_exec");
    uint64_t (*commit)(void)=dlsym(lib,"mtl_sub_commit");
    void (*readtex)(const tex_t *,unsigned,unsigned,const NVNcopyRegion *,uint8_t *,size_t,size_t)=dlsym(lib,"mtl_read_texels");
    CHECK(begin&&exec&&commit&&readtex);
    size_t bytes=1<<20;void *memory=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(memory!=MAP_FAILED);
    NVNdeviceBuilder db;NVNdevice dev;API(DEVICEBUILDERSETDEFAULTS,DeviceBuilderSetDefaults)(&db);CHECK(API(DEVICEINITIALIZE,DeviceInitialize)(&dev,&db));
    NVNmemoryPoolBuilder pb;NVNmemoryPool pool;API(MEMORYPOOLBUILDERSETDEFAULTS,MemoryPoolBuilderSetDefaults)(&pb);
    API(MEMORYPOOLBUILDERSETSTORAGE,MemoryPoolBuilderSetStorage)(&pb,memory,bytes);API(MEMORYPOOLBUILDERSETFLAGS,MemoryPoolBuilderSetFlags)(&pb,0x24);
    CHECK(API(MEMORYPOOLINITIALIZE,MemoryPoolInitialize)(&pool,&pb));
    NVNtexture src,dst;NVNtextureBuilder tb;API(TEXTUREBUILDERSETDEVICE,TextureBuilderSetDevice)(&tb,&dev);API(TEXTUREBUILDERSETDEFAULTS,TextureBuilderSetDefaults)(&tb);
    API(TEXTUREBUILDERSETTARGET,TextureBuilderSetTarget)(&tb,NVN_TEXTURE_TARGET_2D);API(TEXTUREBUILDERSETFORMAT,TextureBuilderSetFormat)(&tb,NVN_FORMAT_RGBA8);
    API(TEXTUREBUILDERSETWIDTH,TextureBuilderSetWidth)(&tb,2);API(TEXTUREBUILDERSETHEIGHT,TextureBuilderSetHeight)(&tb,2);
    API(TEXTUREBUILDERSETSTORAGE,TextureBuilderSetStorage)(&tb,&pool,65536);CHECK(API(TEXTUREINITIALIZE,TextureInitialize)(&src,&tb));
    API(TEXTUREBUILDERSETWIDTH,TextureBuilderSetWidth)(&tb,4);API(TEXTUREBUILDERSETHEIGHT,TextureBuilderSetHeight)(&tb,4);
    API(TEXTUREBUILDERSETSTORAGE,TextureBuilderSetStorage)(&tb,&pool,131072);CHECK(API(TEXTUREINITIALIZE,TextureInitialize)(&dst,&tb));
    const uint8_t pixels[]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
    NVNcopyRegion sr={0,0,0,2,2,1},dr={0,0,0,4,4,1};
    int (*swizzle)(const nvn_texdesc *,unsigned,unsigned,const void *,size_t,void *)=dlsym(lib,"nvn_swizzle");CHECK(swizzle);
    tex_t *source=(tex_t *)&src;CHECK(!swizzle(&source->d,0,0,pixels,8,source->mem));
    NVNcommandBuffer cb;CHECK(API(COMMANDBUFFERINITIALIZE,CommandBufferInitialize)(&cb,&dev));
    for(unsigned test=0;test<2;test++){
        API(COMMANDBUFFERADDCOMMANDMEMORY,CommandBufferAddCommandMemory)(&cb,&pool,0,16384);
        API(COMMANDBUFFERBEGINRECORDING,CommandBufferBeginRecording)(&cb);
        API(COMMANDBUFFERCOPYTEXTURETOTEXTURE,CommandBufferCopyTextureToTexture)(&cb,&src,NULL,&sr,&dst,NULL,&dr,test?NVN_COPY_FLAGS_ENGINE_2D_BIT:0);
        NVNcommandHandle handle=API(COMMANDBUFFERENDRECORDING,CommandBufferEndRecording)(&cb);
        begin();exec((const uint8_t *)(uintptr_t)handle);commit();
        uint8_t out[64];readtex((tex_t *)&dst,0,0,&dr,out,16,64);
        for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)for(unsigned c=0;c<4;c++)CHECK(out[(y*4+x)*4+c]==pixels[((y/2)*2+x/2)*4+c]);
    }
    puts("PASS scaled GPU copy: flags=0 and ENGINE_2D, all 128 channels, four distinct quadrants");
    return 0;
}
