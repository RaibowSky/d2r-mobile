/* Run against a real image; exercise the production HLE path resolver. */
#include "../../host/hle_fs.c"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line=%d %s errno=%d\n",__LINE__,#x,errno);return 1;}}while(0)
int main(int argc,char **argv){
    if(argc!=3)return 2;
    CHECK(!romfs_image_setup(argv[1],argv[2]));
    char canonical[1024],joined[1024],small[8];int fd=-1;int64_t size=0;ssize_t received=0;char header[32];
    CHECK(root_path(argv[2],"/voicetext/tts_single_db(D32-YAFANG).vtdb2",canonical,sizeof canonical));
    CHECK(root_path(argv[2],"///voicetext///tts_single_db(D32-YAFANG).vtdb2//",joined,sizeof joined));
    CHECK(!strcmp(canonical,joined));
    CHECK(romfs_image_open(joined,1,&fd)==1);CHECK(romfs_image_fd_size(fd,&size)==1&&size==5586986);
    CHECK(romfs_image_pread(fd,header,sizeof header,0,&received)==1&&received==sizeof header);CHECK(romfs_image_close_fd(fd)==1);
    CHECK(root_path(argv[2],"/voicetext//userdict_twn.csv",joined,sizeof joined));
    CHECK(!root_path(argv[2],"/voicetext//../userdict_twn.csv",joined,sizeof joined));
    CHECK(!root_path(argv[2],"/voicetext//./userdict_twn.csv",joined,sizeof joined));
    CHECK(!root_path(argv[2],"/voicetext\\userdict_twn.csv",joined,sizeof joined));
    CHECK(!root_path(argv[2],"/voicetext//missing.vtdb2",joined,sizeof joined));
    CHECK(!root_path(argv[2],"/voicetext//userdict_twn.csv",small,sizeof small));
    CHECK(!romfs_image_shutdown());
    puts("PASS production RomFS guest path: repeated separators, VoiceText database/dictionary, real image read, traversal/backslash/missing/overflow rejection");return 0;
}
