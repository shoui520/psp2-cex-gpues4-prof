#include "../examples/lab/notification_poll.h"
#include <assert.h>

int main(void) {
  unsigned slots[512] = {0};
  uint32_t ta[8], fragment[8];
  LabNotificationCache cache = {0};
  assert(lab_poll_notifications(&cache, slots, 0, ta, fragment) == 0);
  slots[0] = 1;
  assert(lab_poll_notifications(&cache, slots, 1, ta, fragment) == 2);
  assert(ta[0] == 1 && fragment[0] == 0);
  assert(lab_poll_notifications(&cache, slots, 1, ta, fragment) == 1);
  slots[256] = 1;
  assert(lab_poll_notifications(&cache, slots, 1, ta, fragment) == 1);
  assert(lab_poll_notifications(&cache, slots, 1, ta, fragment) == 0);
  /* New pairs can complete out of order; each word and both halves matter. */
  for (unsigned i = 1; i < 256; ++i) {
    slots[i] = i % 3 ? i + 1 : 0;
    slots[256+i] = i % 5 ? i + 1 : 0;
  }
  assert(lab_poll_notifications(&cache, slots, 256, ta, fragment) == 510);
  for (unsigned i = 0; i < 256; ++i) {
    assert(!!(ta[i/32] & (1u << (i%32))) == (slots[i] == i+1));
    assert(!!(fragment[i/32] & (1u << (i%32))) == (slots[256+i] == i+1));
    slots[i] = slots[256+i] = i+1;
  }
  lab_poll_notifications(&cache, slots, 256, ta, fragment);
  assert(lab_poll_notifications(&cache, slots, 256, ta, fragment) == 0);
  for (unsigned w = 0; w < 8; ++w)
    assert(ta[w] == UINT32_MAX && fragment[w] == UINT32_MAX);
  return 0;
}
