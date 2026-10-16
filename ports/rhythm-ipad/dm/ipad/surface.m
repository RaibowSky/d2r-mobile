#import <QuartzCore/CAMetalLayer.h>
#import <CoreFoundation/CoreFoundation.h>
#include "surface.h"
int dm_ipad_surface_width(void *layer) { return (int)((__bridge CAMetalLayer *)layer).drawableSize.width; }
int dm_ipad_surface_height(void *layer) { return (int)((__bridge CAMetalLayer *)layer).drawableSize.height; }
void dm_ipad_surface_acquire(void *layer) { CFRetain(layer); }
void dm_ipad_surface_release(void *layer) { CFRelease(layer); }
