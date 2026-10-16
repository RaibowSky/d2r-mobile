/* Exercise the actual Darwin filesystem implementation, using synthetic saves. */
#include "../dm/android/services_fs.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

static void write_save(const char *directory,const char *value) {
    char path[1024];snprintf(path,sizeof path,"%s/game_save.bin",directory);
    FILE *f=fopen(path,"wb");assert(f);
    size_t size=strlen(value);assert(fwrite(value,1,size,f)==size);assert(!fclose(f));
}
static void check_save(const char *directory,const char *value) {
    char path[1024],bytes[128]={0};snprintf(path,sizeof path,"%s/game_save.bin",directory);
    FILE *f=fopen(path,"rb");assert(f);
    size_t got=fread(bytes,1,sizeof bytes,f);assert(!ferror(f));assert(!fclose(f));
    assert(got==strlen(value) && !memcmp(bytes,value,got));
}
int main(void) {
    char parent[]="/tmp/rhythm-save-test-XXXXXX";assert(mkdtemp(parent));
    char committed[1024],work[1024],reopened[1024],file[1100],link[1100];
    snprintf(committed,sizeof committed,"%s/save",parent);
    snprintf(work,sizeof work,"%s/save.work",parent);
    snprintf(reopened,sizeof reopened,"%s/reopened",parent);
    assert(!dm_android_fs_ensure_directory(committed));
    /* Fresh directories have no guest-name xattr: the reported iPad failure. */
    errno=0;assert(getxattr(committed,DM_ANDROID_GUEST_NAME_XATTR,NULL,0,0,0)<0);
    assert(errno==ENOATTR);
    assert(!dm_android_fs_clone_tree(committed,work));
    write_save(work,"generation-one");
    assert(!dm_android_fs_commit_snapshot(work,committed));
    check_save(committed,"generation-one");
    snprintf(file,sizeof file,"%s/game_save.bin",work);
    assert(!setxattr(file,DM_ANDROID_GUEST_NAME_XATTR,"guest-name",10,0,0));
    assert(!setxattr(work,DM_ANDROID_GUEST_NAME_XATTR,"guest-directory",15,0,0));
    write_save(work,"generation-two");
    assert(!dm_android_fs_commit_snapshot(work,committed));
    assert(!dm_android_fs_clone_tree(committed,reopened));
    check_save(reopened,"generation-two");
    snprintf(file,sizeof file,"%s/game_save.bin",reopened);
    char name[32]={0};
    assert(getxattr(file,DM_ANDROID_GUEST_NAME_XATTR,name,sizeof name,0,0)==10);
    assert(!memcmp(name,"guest-name",10));
    memset(name,0,sizeof name);
    assert(getxattr(reopened,DM_ANDROID_GUEST_NAME_XATTR,name,sizeof name,0,0)==15);
    assert(!memcmp(name,"guest-directory",15));
    /* A real clone error must still fail and preserve the committed version. */
    snprintf(link,sizeof link,"%s/refused-link",work);
    assert(!symlink("game_save.bin",link));
    write_save(work,"must-not-commit");
    assert(dm_android_fs_commit_snapshot(work,committed)<0);
    check_save(committed,"generation-two");
    assert(!dm_android_fs_remove_tree(parent));
    puts("iPad save snapshot: PASS empty mount, write, commit, reopen, xattrs, failed commit preservation");
    return 0;
}
