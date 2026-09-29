#ifndef LAB_NOTIFICATION_POLL_H
#define LAB_NOTIFICATION_POLL_H
#include <stdint.h>
#include <string.h>

/* Slots are written once with index+1 and never reused during a capture.
 * Reset this cache only after the sampler joins and before clearing slots.
 * published is an acquire-loaded count of notification pairs submitted (or
 * about to be submitted). This does not make GPU writes atomic with sampling.
 */
typedef struct LabNotificationCache {
  uint32_t ta[8], fragment[8];
} LabNotificationCache;

static inline unsigned lab_poll_notifications(LabNotificationCache *cache,
    volatile const unsigned *slots, unsigned published,
    uint32_t *ta, uint32_t *fragment) {
  unsigned reads = 0;
  if (published > 256)
    published = 256;
  for (unsigned i = 0; i < published; ++i) {
    uint32_t bit = 1u << (i % 32);
    unsigned word = i / 32;
    if (!(cache->ta[word] & bit)) {
      ++reads;
      if (slots[i] == i + 1)
        cache->ta[word] |= bit;
    }
    if (!(cache->fragment[word] & bit)) {
      ++reads;
      if (slots[256 + i] == i + 1)
        cache->fragment[word] |= bit;
    }
  }
  memcpy(ta, cache->ta, sizeof(cache->ta));
  memcpy(fragment, cache->fragment, sizeof(cache->fragment));
  return reads;
}
#endif
