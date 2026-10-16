#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#include "../host/dm.h"
#include "../host/front.h"
#include "../android/services_sdk.h"
#include "../android/services_platform.h"
#include "../android/services_fs.h"
#include "../android/input_backend.h"
#include "../android/romfs_image.h"
#include <stdatomic.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

struct dm_flags g_flag;
uint64_t g_t0_ns;
static _Atomic int foreground,started,input_ready;
static _Atomic uint64_t frame_count;
static void *native_layer;
static char documents[4096],data_root[4096];
static dispatch_queue_t lifecycle;
static NSString *current_phase=@"準備啟動";
static UIViewController *controller;
extern void guest_thread_init(void);
extern void crash_thread_init(void);
extern uint32_t svc_thread_main_init(void);
extern int dm_android_diagnostics_flush(void);
extern void dm_vk_set_shader_root(const char *);
extern void dm_vk_set_window(void *);
extern void dm_vk_set_display_hz(int);
extern void dm_audio_renderer_suspend(int);
extern void dm_audio_suspend(int);

static void persist_phase(const char *phase) {
    @autoreleasepool {
        NSString *name=[NSString stringWithUTF8String:phase];
        NSDictionary *status=@{@"phase":name,@"presented_frames":@(atomic_load(&frame_count)),@"foreground":@(atomic_load(&foreground))};
        NSData *json=[NSJSONSerialization dataWithJSONObject:status options:NSJSONWritingPrettyPrinted error:nil];
        [json writeToFile:[[NSString stringWithUTF8String:documents] stringByAppendingPathComponent:@"status.json"] options:NSDataWritingAtomic error:nil];
        dispatch_async(dispatch_get_main_queue(),^{current_phase=name;});
        LOG("IPAD_GAME_STAGE %s frames=%llu",phase,(unsigned long long)atomic_load(&frame_count));
    }
}
void dm_android_front_presented(uint64_t frame) {
    atomic_store(&frame_count,frame);
    if(frame==1 || frame%600==0)persist_phase("遊戲執行中");
}
void *dm_android_native_window(void) {return native_layer;}
static int input_source_ready(void *opaque) {(void)opaque;return atomic_load(&input_ready)&&atomic_load(&foreground);}
static int suspend_graphics(int on) {dm_gfx_suspend(on);return 0;}
static int pause_audio(int on) {dm_audio_suspend(on);dm_audio_renderer_suspend(on);return 0;}
static void path(char *dst,size_t n,const char *base,const char *leaf) {
    if(snprintf(dst,n,"%s/%s",base,leaf)>=(int)n)die("iPad path exceeds capacity");
}
static void *required(const char *name) {void *fn=guest_sym(name);if(!fn)die("Missing original startup export %s",name);return fn;}
static void call_original(const char *name) {
    LOG("IPAD_ORIGINAL_CALL %s",name);((void(*)(void))required(name))();LOG("IPAD_ORIGINAL_RETURN %s",name);
}
static void *game_thread(void *opaque) {
    (void)opaque;
    @autoreleasepool {
        crash_thread_init();
        char modules[4096],rom[4096],image[4096],save[4096],temp[4096],profile[4096],shaders[4096],fonts[4096];
        path(modules,sizeof modules,data_root,"modules");path(image,sizeof image,data_root,"game.romfs");
        path(shaders,sizeof shaders,data_root,"shaders");path(fonts,sizeof fonts,data_root,"fonts");
        path(rom,sizeof rom,documents,"romfs-view");path(save,sizeof save,documents,"save");
        path(temp,sizeof temp,documents,"temp");path(profile,sizeof profile,documents,"profile");
        struct stat st;if(stat(image,&st)||st.st_size!=3408969728LL)die("Original RomFS missing or incorrect size");
        persist_phase("讀取遊戲資源");
        if(romfs_image_setup_checked(image,rom,3408969728ULL))die("RomFS setup errno=%d",errno);
        if(dm_android_fs_ensure_directory(save)||dm_android_fs_ensure_directory(temp)||dm_android_fs_ensure_directory(profile))die("Private directory setup errno=%d",errno);
        setenv("DM_ROMFS",rom,1);setenv("DM_SAVE",save,1);setenv("DM_TEMP",temp,1);setenv("DM_FONT_DIR",fonts,1);
        setenv("DM_AUDIO","1",1);setenv("DM_WRITEBACK_PREPARE","1",1);setenv("DM_DIRTY_SUMMARY","1",1);
        dm_vk_set_shader_root(shaders);
        NSString *locale=[NSLocale preferredLanguages].firstObject?:@"zh-Hant-TW";
        dm_android_platform_config config={.mode=DM_ANDROID_HOST_ACTIVITY,.profile_directory=profile,
            .locale=locale.UTF8String,.locale_source="NSLocale.preferredLanguages",.content_manifest_verified=1,
            .graphics_present=1,.audio_present=1,.suspend_and_drain_graphics=suspend_graphics,
            .pause_audio=pause_audio,.flush_diagnostics=dm_android_diagnostics_flush};
        int error=dm_android_platform_bind(&config);if(!error)error=dm_android_platform_register_services();
        if(error)die("iPad platform initialization error=%d",error);
        dm_android_service_register(DM_ANDROID_SERVICE_FS,dm_android_fs_initialize,NULL);
        dm_android_window_state window={.focus=atomic_load(&foreground)?1:3,.surface_available=1,
            .width=1280,.height=720,.density=1,.refresh_hz=60,.thermal_status=-1};
        if(dm_android_platform_update_window(&window))die("iPad window initialization");
        const char *names[]={"main","sdk","rtld"};
        persist_phase("裝載遊戲");load_modules(modules,names,3);guest_thread_init();
        uint32_t handle=svc_thread_main_init();
        call_original("_init_libc0");
        ((void(*)(uint64_t,uint64_t))required("nnosInitialize"))(handle,0);
        call_original("_init_libc1");call_original("nninitInitializeSdkModule");
        call_original("nninitStartup");call_original("_init_libc2");
        persist_phase("啟動遊戲");run_module_inits();
        char *args[]={"RhythmHost",NULL};
        ((void(*)(int))required("_ZN2nn2os11SetHostArgcEi"))(1);
        ((void(*)(char **))required("_ZN2nn2os11SetHostArgvEPPc"))(args);
        call_original("nnMain");persist_phase("遊戲已退出");
    }
    return NULL;
}

@interface GameSurface:UIView
@property(nonatomic,strong) NSMapTable<UITouch *,NSNumber *> *contacts;
@end
@implementation GameSurface
+(Class)layerClass {return [CAMetalLayer class];}
-(instancetype)initWithFrame:(CGRect)frame {
    if((self=[super initWithFrame:frame])){
        self.multipleTouchEnabled=YES;self.backgroundColor=UIColor.blackColor;
        self.contacts=[NSMapTable strongToStrongObjectsMapTable];
        CAMetalLayer *layer=(CAMetalLayer *)self.layer;layer.device=MTLCreateSystemDefaultDevice();
        layer.pixelFormat=MTLPixelFormatBGRA8Unorm;layer.framebufferOnly=NO;
    }return self;
}
-(void)layoutSubviews {
    [super layoutSubviews];CAMetalLayer *layer=(CAMetalLayer *)self.layer;
    /* Stable logical output: avoid races between UIKit layout and Vulkan.
     * Metal scales to the aspect-preserving view on an M4 iPad. */
    layer.drawableSize=CGSizeMake(1280,720);
}
-(void)publish {
    dm_android_pointer points[16];int count=0;
    for(UITouch *t in self.contacts){CGPoint p=[t locationInView:self];
        points[count++]=(dm_android_pointer){.id=[[self.contacts objectForKey:t] unsignedIntValue],
            .x=(float)(p.x/self.bounds.size.width),.y=(float)(p.y/self.bounds.size.height)};}
    if(atomic_load(&input_ready))dm_android_input_publish_touch(points,(size_t)count);
}
-(void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    (void)event;BOOL used[16]={0};for(NSNumber *n in self.contacts.objectEnumerator)used[n.unsignedIntValue]=YES;
    for(UITouch *t in touches){for(unsigned i=0;i<16;i++)if(!used[i]){[self.contacts setObject:@(i) forKey:t];used[i]=YES;break;}}
    [self publish];
}
-(void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event {(void)touches;(void)event;[self publish];}
-(void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event {(void)event;for(UITouch *t in touches)[self.contacts removeObjectForKey:t];[self publish];}
-(void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event {(void)touches;(void)event;[self.contacts removeAllObjects];dm_android_input_cancel();}
@end

@interface GameController:UIViewController
@property(nonatomic,strong) GameSurface *surface;
@property(nonatomic,strong) NSMutableArray<UIButton *> *buttons;
@property(nonatomic) uint64_t held;
@end
@implementation GameController
-(BOOL)prefersStatusBarHidden {return YES;}
-(UIInterfaceOrientationMask)supportedInterfaceOrientations {return UIInterfaceOrientationMaskLandscape;}
-(void)viewDidLoad {
    [super viewDidLoad];controller=self;self.view.backgroundColor=UIColor.blackColor;
    self.surface=[[GameSurface alloc]initWithFrame:CGRectZero];[self.view addSubview:self.surface];
    self.buttons=[NSMutableArray array];
    NSArray *titles=@[@"A",@"B",@"X",@"Y",@"L",@"R",@"+",@"−",@"←",@"↑",@"→",@"↓"];
    NSArray *bits=@[@0,@1,@2,@3,@6,@7,@10,@11,@12,@13,@14,@15];
    for(NSUInteger i=0;i<titles.count;i++){
        UIButton *b=[UIButton buttonWithType:UIButtonTypeCustom];b.tag=[bits[i] integerValue];
        [b setTitle:titles[i] forState:UIControlStateNormal];b.titleLabel.font=[UIFont boldSystemFontOfSize:22];
        b.backgroundColor=[UIColor colorWithWhite:0.18 alpha:0.78];b.layer.cornerRadius=26;
        b.accessibilityLabel=titles[i];b.multipleTouchEnabled=YES;
        [b addTarget:self action:@selector(down:) forControlEvents:UIControlEventTouchDown|UIControlEventTouchDragEnter];
        [b addTarget:self action:@selector(up:) forControlEvents:UIControlEventTouchUpInside|UIControlEventTouchUpOutside|UIControlEventTouchCancel|UIControlEventTouchDragExit];
        [self.view addSubview:b];[self.buttons addObject:b];
    }
}
-(void)down:(UIButton *)button {self.held|=1ull<<button.tag;dm_android_input_publish_actions(self.held,0,0,0,0);}
-(void)up:(UIButton *)button {self.held&=~(1ull<<button.tag);dm_android_input_publish_actions(self.held,0,0,0,0);}
-(void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];CGRect bounds=self.view.bounds;CGFloat w=bounds.size.width,h=bounds.size.height;
    CGFloat gw=MIN(w,h*16/9),gh=gw*9/16;self.surface.frame=CGRectMake((w-gw)/2,(h-gh)/2,gw,gh);
    CGFloat left=self.view.safeAreaInsets.left+18,right=w-self.view.safeAreaInsets.right-18;
    CGFloat y=h-self.view.safeAreaInsets.bottom-76;
    CGPoint positions[]={ {right-58,y-48},{right-112,y},{right-58,y-102},{right-166,y-48},
        {left+26,56},{right-26,56},{w/2+48,h-42},{w/2-48,h-42},
        {left+26,y-48},{left+80,y-102},{left+134,y-48},{left+80,y} };
    for(NSUInteger i=0;i<self.buttons.count;i++)self.buttons[i].frame=CGRectMake(positions[i].x-26,positions[i].y-26,52,52);
}
-(void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    if(atomic_load(&started))return;
    NSString *root=[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"data"];
    NSString *guest=[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"Frameworks/Guest.framework/Guest"];
    if(![NSFileManager.defaultManager fileExistsAtPath:[root stringByAppendingPathComponent:@"game.romfs"]] ||
       ![NSFileManager.defaultManager fileExistsAtPath:guest]){
        UIAlertController *a=[UIAlertController alertControllerWithTitle:@"尚未加入遊戲資料" message:@"請在電腦完成遊戲封裝後，重新簽名並安裝完整版本。" preferredStyle:UIAlertControllerStyleAlert];
        [a addAction:[UIAlertAction actionWithTitle:@"確定" style:UIAlertActionStyleDefault handler:nil]];[self presentViewController:a animated:YES completion:nil];return;
    }
    if(atomic_exchange(&started,1))return;
    strlcpy(data_root,root.fileSystemRepresentation,sizeof data_root);
    native_layer=(__bridge void *)self.surface.layer;
    dm_android_input_config input={.source="UIKit.multiTouch.buttons",.panel_width=1280,.panel_height=720,
        .source_ready=input_source_ready,.logical_npad_style=1,.logical_npad_id=0};
    if(dm_android_input_bind(&input))die("iPad input binding");atomic_store(&input_ready,1);
    dispatch_async(lifecycle,^{
        dm_vk_set_window(native_layer);dm_vk_set_display_hz(60);
        pthread_t thread;pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,16u<<20);
        int error=pthread_create(&thread,&attr,game_thread,NULL);pthread_attr_destroy(&attr);
        if(error)die("iPad guest thread creation error=%d",error);pthread_detach(thread);
    });
}
@end

static void change_focus(int active) {
    atomic_store(&foreground,active);if(!active){dm_android_input_cancel();((GameController *)controller).held=0;}
    if(!atomic_load(&started))return;
    dispatch_async(lifecycle,^{
        pause_audio(1);dm_gfx_suspend(1);
        dm_android_platform_status status;dm_android_platform_get_status(&status);
        if(status.bound){dm_android_window_state w;dm_android_platform_get_window(&w);w.focus=active?1:3;dm_android_platform_update_window(&w);}
        guest_clock_freeze_for(1,!active);
        if(active){dm_gfx_suspend(0);pause_audio(0);}
    });
}
@interface AppDelegate:UIResponder<UIApplicationDelegate>
@property(nonatomic,strong) UIWindow *window;
@end
@implementation AppDelegate
-(BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)application;(void)options;
    NSString *docs=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
    strlcpy(documents,docs.fileSystemRepresentation,sizeof documents);
    NSString *log=[docs stringByAppendingPathComponent:@"game-startup.log"];
    int fd=open(log.fileSystemRepresentation,O_WRONLY|O_CREAT|O_TRUNC,0600);
    if(fd>=0){dup2(fd,STDERR_FILENO);close(fd);}setvbuf(stderr,NULL,_IONBF,0);
    g_t0_ns=now_ns();g_flag.fs=1;g_flag.svc=1;g_flag.dbg=1;
    lifecycle=dispatch_queue_create("org.rhythmhost.lifecycle",DISPATCH_QUEUE_SERIAL);
    AVAudioSession *audio=AVAudioSession.sharedInstance;NSError *error=nil;
    if(![audio setCategory:AVAudioSessionCategoryPlayback mode:AVAudioSessionModeDefault options:0 error:&error] ||
       ![audio setPreferredSampleRate:48000 error:&error] || ![audio setPreferredIOBufferDuration:0.005 error:&error] ||
       ![audio setActive:YES error:&error])LOG("IPAD_AUDIO_SESSION_FAILED %s",error.description.UTF8String);
    LOG("IPAD_HOST system=%s model=%s audio_rate=%.0f buffer_ms=%.3f",UIDevice.currentDevice.systemVersion.UTF8String,
        UIDevice.currentDevice.model.UTF8String,audio.sampleRate,audio.IOBufferDuration*1000);
    self.window=[[UIWindow alloc]initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController=[GameController new];[self.window makeKeyAndVisible];
    application.idleTimerDisabled=YES;return YES;
}
-(void)applicationDidBecomeActive:(UIApplication *)application {(void)application;change_focus(1);}
-(void)applicationWillResignActive:(UIApplication *)application {(void)application;change_focus(0);}
@end
int main(int argc,char **argv) {@autoreleasepool{return UIApplicationMain(argc,argv,nil,NSStringFromClass(AppDelegate.class));}}

/* SDK keyboard requests run on a guest thread; UIKit presentation stays on main. */
static _Thread_local int keyboard_minimum;
void dm_android_keyboard_constraints(int minimum) {keyboard_minimum=minimum;}
void dm_ui_text_entered(const char *text) {LOG("IPAD_KEYBOARD_RETURN bytes=%zu",text?strlen(text):0);}
int front_text_input(const char *title,const char *sub,const char *guide,const char *initial,int max_chars,char *out,int capacity) {
    if(!atomic_load(&foreground)||!controller||capacity<=0)return 0;
    dispatch_semaphore_t done=dispatch_semaphore_create(0);__block NSString *answer=nil;
    NSString *t=[NSString stringWithUTF8String:title?:""];
    NSString *s=[NSString stringWithFormat:@"%s\n%s",sub?:"",guide?:""];
    NSString *value=[NSString stringWithUTF8String:initial?:""];int minimum=keyboard_minimum;
    dispatch_async(dispatch_get_main_queue(),^{
        UIAlertController *alert=[UIAlertController alertControllerWithTitle:t message:s preferredStyle:UIAlertControllerStyleAlert];
        [alert addTextFieldWithConfigurationHandler:^(UITextField *field){field.text=value;}];
        [alert addAction:[UIAlertAction actionWithTitle:@"取消" style:UIAlertActionStyleCancel handler:^(UIAlertAction *action){(void)action;dispatch_semaphore_signal(done);}]];
        [alert addAction:[UIAlertAction actionWithTitle:@"確定" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action){
            (void)action;NSString *v=alert.textFields.firstObject.text;if(v.length>=(NSUInteger)MAX(0,minimum)&&v.length<=(NSUInteger)MAX(0,max_chars))answer=v;
            dispatch_semaphore_signal(done);}]];
        [controller presentViewController:alert animated:YES completion:nil];
    });
    dispatch_semaphore_wait(done,DISPATCH_TIME_FOREVER);if(!answer)return 0;
    if(strlen(answer.UTF8String)>=(size_t)capacity)return 0;strlcpy(out,answer.UTF8String,(size_t)capacity);return 1;
}
