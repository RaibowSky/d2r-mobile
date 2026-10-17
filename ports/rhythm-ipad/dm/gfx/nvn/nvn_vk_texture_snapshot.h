#ifndef NVN_VK_TEXTURE_SNAPSHOT_H
#define NVN_VK_TEXTURE_SNAPSHOT_H
#include <stddef.h>
#include <string.h>

/* A dirty host page is only a candidate update: unrelated allocations can
 * share it. Compare every encoded byte, including all mips/layers, rather
 * than relying on a hash or treating a page write as a changed texture.
 * The caller must take its generation BEFORE protecting/reading the backing;
 * never replace that generation with one taken after this comparison. */
static inline int nvn_vk_texture_snapshot_same(const void *saved, size_t saved_bytes,
                                              const void *current, size_t current_bytes) {
    return saved && current && saved_bytes == current_bytes && current_bytes &&
           memcmp(saved, current, current_bytes) == 0;
}
#endif
