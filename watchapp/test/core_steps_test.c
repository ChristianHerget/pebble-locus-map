#include "core_test_support.h"

static void test_step_state_transitions_and_sampling(void) {
  const WatchStepRecording recording = {.recording_start_low = 10, .recording_start_high = 20};
  WatchStepState state;
  watch_step_state_initialize(&state);

  // A snapshot arriving before its cached projection announces unavailability, then matching
  // context activates sampling without requiring another snapshot.
  WatchStepEffects effects =
      watch_step_state_update(&state, true, false, true, true, recording, false);
  assert(effects.render && !effects.sample_now && watch_step_state_has_outbound(&state));
  const WatchStepPacket *packet = watch_step_state_prepare(&state);
  assert(packet && packet->delta == INT32_MIN && packet->sequence == 0);
  effects = watch_step_state_update(&state, true, true, true, true, recording, true);
  assert(effects.sample_now && !effects.discarded_prepared);
  effects = watch_step_state_health_read(&state, true, 100, 5, INT32_MIN);
  assert(effects.render && watch_step_state_available(&state));
  assert(packet->delta == INT32_MIN && packet->recording.recording_start_low == 10);
  watch_step_state_finish_prepared(&state);
  packet = watch_step_state_prepare(&state);
  assert(packet && packet->delta == 0 && packet->sequence == 1);
  watch_step_state_finish_prepared(&state);

  uint32_t deadline = 0;
  assert(watch_step_state_sample_deadline(&state, &deadline) && deadline == 65);
  effects = watch_step_state_update(&state, true, true, true, true, recording, false);
  assert(!effects.render && !effects.sample_now);
  // Recording/paused is deliberately represented by the same active identity.
  effects = watch_step_state_update(&state, true, true, true, true, recording, false);
  assert(state.baseline_valid && state.baseline == 100 && !effects.sample_now);

  // A backwards Health total is a reset: the new total, not a negative delta, is accumulated.
  watch_step_state_health_read(&state, true, 120, 65, INT32_MIN);
  watch_step_state_health_read(&state, true, 5, 125, INT32_MIN);
  packet = watch_step_state_prepare(&state);
  assert(packet && packet->delta == 25 && packet->sequence == 2);
  const uint8_t maximum_attempts = 4;
  uint8_t attempts = 0;
  for (int attempt = 1; attempt < maximum_attempts; attempt++) {
    assert(!watch_outbound_retry_failed(&attempts, maximum_attempts));
    assert(attempts == attempt);
    assert(watch_step_state_prepare(&state) == packet);
  }
  assert(watch_outbound_retry_failed(&attempts, maximum_attempts));
  assert(attempts == 0);
  watch_step_state_finish_prepared(&state); // Permanent drop consumes sequence 2.

  effects = watch_step_state_update(&state, false, true, true, true, recording, false);
  assert(effects.render && !watch_step_state_available(&state));
  packet = watch_step_state_prepare(&state);
  assert(packet && packet->delta == INT32_MIN && packet->sequence == 3);
  watch_step_state_finish_prepared(&state);
  effects = watch_step_state_update(&state, false, true, true, true, recording, false);
  assert(!effects.render && !watch_step_state_has_outbound(&state));

  effects = watch_step_state_update(&state, true, true, true, true, recording, false);
  assert(effects.sample_now);
  effects = watch_step_state_health_read(&state, false, -1, 130, INT32_MIN);
  assert(!effects.render && !watch_step_state_has_outbound(&state));
  effects = watch_step_state_health_read(&state, false, -1, 190, INT32_MIN);
  assert(!effects.render && !watch_step_state_has_outbound(&state));
  effects = watch_step_state_health_read(&state, true, 7, 250, INT32_MIN);
  assert(effects.render && watch_step_state_available(&state));
  packet = watch_step_state_prepare(&state);
  assert(packet && packet->delta == 0 && packet->sequence == 4);
  watch_step_state_finish_prepared(&state);

  effects = watch_step_state_health_read(&state, false, -1, 310, INT32_MIN);
  assert(effects.render && !watch_step_state_available(&state));
  packet = watch_step_state_prepare(&state);
  assert(packet && packet->delta == INT32_MIN && packet->sequence == 5);
  watch_step_state_finish_prepared(&state);
  effects = watch_step_state_health_read(&state, true, 8, 370, INT32_MIN);
  assert(effects.render && watch_step_state_available(&state));
  packet = watch_step_state_prepare(&state);
  assert(packet && packet->delta == 0 && packet->sequence == 6);
  watch_step_state_finish_prepared(&state);

  effects = watch_step_state_update(&state, true, true, true, false, recording, false);
  assert(effects.render && !watch_step_state_available(&state));
  assert(!watch_step_state_sample_deadline(&state, &deadline));
}

static void test_step_state_freezes_identity_and_never_reuses_sequences(void) {
  const WatchStepRecording first = {.recording_start_low = 1};
  const WatchStepRecording second = {.recording_start_low = 2};
  const WatchStepRecording third = {.recording_start_low = 3};
  WatchStepState state;
  watch_step_state_initialize(&state);
  assert(watch_step_state_update(&state, true, true, true, true, first, false).sample_now);
  watch_step_state_health_read(&state, true, 50, 0, INT32_MIN);
  const WatchStepPacket *old = watch_step_state_prepare(&state);
  assert(old && old->recording.recording_start_low == 1 && old->sequence == 0);

  // An in-flight packet stays frozen while new-identity work is queued separately.
  WatchStepEffects effects = watch_step_state_update(&state, true, true, true, true, second, true);
  assert(effects.sample_now && !effects.discarded_prepared);
  watch_step_state_health_read(&state, true, 60, 1, INT32_MIN);
  assert(old->recording.recording_start_low == 1 && old->sequence == 0 && old->delta == 0);
  watch_step_state_finish_prepared(&state);
  const WatchStepPacket *current = watch_step_state_prepare(&state);
  assert(current && current->recording.recording_start_low == 2 && current->sequence == 1);

  // A prepared retry that can no longer deliver is discarded, but its sequence is never reused.
  effects = watch_step_state_update(&state, true, true, true, true, third, false);
  assert(effects.discarded_prepared && effects.sample_now);
  watch_step_state_health_read(&state, true, 70, 2, INT32_MIN);
  current = watch_step_state_prepare(&state);
  assert(current && current->recording.recording_start_low == 3 && current->sequence == 2);
}

int main(void) {
  reset_store();
  fprintf(stderr, "[steps] test_step_state_transitions_and_sampling\n");
  test_step_state_transitions_and_sampling();
  reset_store();
  fprintf(stderr, "[steps] test_step_state_freezes_identity_and_never_reuses_sequences\n");
  test_step_state_freezes_identity_and_never_reuses_sequences();
  return 0;
}
