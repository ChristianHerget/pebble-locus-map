#include "core_test_support.h"

static WatchProfileTransfer s_profile_transfer;
static char s_profile_transfer_buffer[WATCH_PROFILE_TRANSFER_BUFFER_SIZE];

typedef struct {
  bool received;
  uint32_t epoch;
  uint16_t age;
  int health_updates;
  int pending_applies;
} SnapshotState;

static bool accept_snapshot_epoch(SnapshotState *state, uint32_t epoch, bool stopped) {
  if (!watch_snapshot_epoch_allowed(state->received, state->epoch, epoch)) return false;
  state->received = true;
  state->epoch = epoch;
  state->age = 0;
  state->health_updates++;
  if (stopped) state->pending_applies++;
  return true;
}

static void test_watch_config_transfer(void) {
  WatchConfigTransfer transfer;
  char buffer[WATCH_CONFIG_BUFFER_SIZE];
  watch_config_transfer_initialize(&transfer);
  buffer[0] = '\0';

  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 7, 0, 3, "ab", 2) ==
         WATCH_TRANSFER_ACCEPTED);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 7, 1, 3, "cd", 2) ==
         WATCH_TRANSFER_ACCEPTED);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 7, 0, 3, "ab", 2) ==
         WATCH_TRANSFER_DUPLICATE);
  assert(transfer.next_chunk == 2);
  assert(transfer.length == 4);
  assert(strcmp(buffer, "abcd") == 0);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 7, 1, 3, "cd", 2) ==
         WATCH_TRANSFER_DUPLICATE);

  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 99, 1, 3, "unrelated",
                                      9) == WATCH_TRANSFER_IGNORED);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 99, 1,
                                      WATCH_CONFIG_MAX_CHUNKS + 1, "bad",
                                      3) == WATCH_TRANSFER_IGNORED);
  assert(transfer.id == 7);
  assert(transfer.next_chunk == 2);

  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 7, 2, 3, "ef", 2) ==
         WATCH_TRANSFER_COMPLETE);
  assert(strcmp(buffer, "abcdef") == 0);

  watch_config_transfer_initialize(&transfer);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 7, 0, 1, "abcdef", 6) ==
         WATCH_TRANSFER_COMPLETE);
  assert(strcmp(buffer, "abcdef") == 0);

  watch_config_transfer_reset(&transfer);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 8, 0, 2, "one", 3) ==
         WATCH_TRANSFER_ACCEPTED);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 8, 0, 2, "changed", 7) ==
         WATCH_TRANSFER_INVALID);
  assert(transfer.id == -1);

  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 9, 0, 2, "one", 3) ==
         WATCH_TRANSFER_ACCEPTED);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 9, 1, 3, "two", 3) ==
         WATCH_TRANSFER_INVALID);
  assert(transfer.id == -1);

  char small[5];
  assert(watch_config_transfer_accept(&transfer, small, sizeof(small), 10, 0, 2, "four", 4) ==
         WATCH_TRANSFER_ACCEPTED);
  assert(watch_config_transfer_accept(&transfer, small, sizeof(small), 10, 1, 2, "x", 1) ==
         WATCH_TRANSFER_INVALID);
  assert(transfer.id == -1);

  watch_config_transfer_initialize(&transfer);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 21, 0, 1, "new", 3) ==
         WATCH_TRANSFER_COMPLETE);
  watch_config_transfer_reset(&transfer);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 20, 0, 1, "old", 3) ==
         WATCH_TRANSFER_IGNORED);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 20, 0, 2, "old-", 4) ==
         WATCH_TRANSFER_IGNORED);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 20, 1, 2, "tail", 4) ==
         WATCH_TRANSFER_IGNORED);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 21, 0, 1, "changed", 7) ==
         WATCH_TRANSFER_INVALID);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 21, 0, 1, "new", 3) ==
         WATCH_TRANSFER_COMPLETE);
  assert(strcmp(buffer, "new") == 0);

  watch_config_transfer_initialize(&transfer);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 22, 0, 2, "ab", 2) ==
         WATCH_TRANSFER_ACCEPTED);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 22, 1, 2, "cd", 2) ==
         WATCH_TRANSFER_COMPLETE);
  watch_config_transfer_reset(&transfer);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 22, 0, 2, "ab", 2) ==
         WATCH_TRANSFER_ACCEPTED);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 22, 1, 2, "XX", 2) ==
         WATCH_TRANSFER_INVALID);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 22, 0, 2, "ab", 2) ==
         WATCH_TRANSFER_ACCEPTED);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 22, 1, 2, "cd", 2) ==
         WATCH_TRANSFER_COMPLETE);
  assert(strcmp(buffer, "abcd") == 0);

  watch_config_transfer_initialize(&transfer);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 31, 0, 2, "new-", 4) ==
         WATCH_TRANSFER_ACCEPTED);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 30, 0, 1, "old", 3) ==
         WATCH_TRANSFER_IGNORED);
  assert(transfer.id == 31);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 31, 1, 2, "tail", 4) ==
         WATCH_TRANSFER_COMPLETE);
  assert(strcmp(buffer, "new-tail") == 0);

  watch_config_transfer_initialize(&transfer);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), INT32_MAX, 0, 1, "edge",
                                      4) == WATCH_TRANSFER_COMPLETE);
  watch_config_transfer_reset(&transfer);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 0, 0, 1, "wrapped", 7) ==
         WATCH_TRANSFER_COMPLETE);
  watch_config_transfer_reset(&transfer);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), INT32_MAX, 0, 1, "stale",
                                      5) == WATCH_TRANSFER_IGNORED);
  assert(watch_config_transfer_accept(&transfer, buffer, sizeof(buffer), 0x40000000, 0, 1,
                                      "ambiguous", 9) == WATCH_TRANSFER_IGNORED);
}

static void test_transfer_serial_reservation(void) {
  const uint32_t key = 150;
  int32_t reserved = -1;
  reset_store();
  assert(watch_transfer_serial_reserve_persistent(key, 7, &reserved));
  assert(reserved == 7);
  assert(watch_transfer_serial_reserve_persistent(key, 99, &reserved));
  assert(reserved == 8);

  const int32_t maximum = INT32_MAX;
  assert(persist_write_data(key, &maximum, sizeof(maximum)) == (int)sizeof(maximum));
  assert(watch_transfer_serial_reserve_persistent(key, 99, &reserved));
  assert(reserved == 0);

  s_fail_write_key = (int)key;
  reserved = 123;
  assert(!watch_transfer_serial_reserve_persistent(key, 99, &reserved));
  assert(reserved == 123);
  s_fail_write_key = -1;
  assert(watch_transfer_serial_reserve_persistent(key, 99, &reserved));
  assert(reserved == 1);

  s_torn_write_key = (int)key;
  s_torn_write_bytes = sizeof(int32_t);
  reserved = 123;
  assert(!watch_transfer_serial_reserve_persistent(key, 99, &reserved));
  assert(reserved == 123);
  s_torn_write_key = -1;
  s_torn_write_bytes = 0;
  assert(watch_transfer_serial_reserve_persistent(key, 99, &reserved));
  assert(reserved == 3);

  const int32_t corrupt = -1;
  assert(persist_write_data(key, &corrupt, sizeof(corrupt)) == (int)sizeof(corrupt));
  assert(!watch_transfer_serial_reserve_persistent(key, 99, &reserved));
}

static WatchProfileTransferOutcome accept_profile_part(int32_t id, int index, int count, int result,
                                                       const char *data, uint32_t uptime_seconds) {
  return watch_profile_transfer_accept(&s_profile_transfer, s_profile_transfer_buffer,
                                       sizeof(s_profile_transfer_buffer), id, index, count, result,
                                       data, strlen(data), uptime_seconds);
}

static void test_snapshot_epoch_ordering(void) {
  SnapshotState state = {
      .received = true,
      .epoch = 200,
      .age = 0,
  };
  assert(!accept_snapshot_epoch(&state, 199, true));
  assert(state.epoch == 200);
  assert(state.age == 0);
  assert(state.health_updates == 0);
  assert(state.pending_applies == 0);

  state.age = 31;
  assert(!accept_snapshot_epoch(&state, 100, true));
  assert(state.epoch == 200);
  assert(state.age == 31);
  assert(state.health_updates == 0);
  assert(state.pending_applies == 0);

  assert(accept_snapshot_epoch(&state, 200, true));
  assert(state.epoch == 200);
  assert(state.age == 0);
  assert(state.health_updates == 1);
  assert(state.pending_applies == 1);

  SnapshotState reopened = {0};
  assert(accept_snapshot_epoch(&reopened, 100, false));
  assert(reopened.epoch == 100);
}

static void test_profile_transfer_reordering(void) {
  watch_profile_transfer_initialize(&s_profile_transfer);
  assert(accept_profile_part(7, 0, 3, 0, "A", 1) == WATCH_PROFILE_TRANSFER_ACCEPTED);
  assert(accept_profile_part(7, 1, 3, 0, "B", 2) == WATCH_PROFILE_TRANSFER_ACCEPTED);
  assert(accept_profile_part(7, 0, 3, 0, "A", 3) == WATCH_PROFILE_TRANSFER_DUPLICATE);
  assert(s_profile_transfer.received_count == 2);
  assert(s_profile_transfer.received[1]);
  assert(memcmp(s_profile_transfer_buffer + WATCH_PROFILE_CHUNK_BYTES, "B", 1) == 0);
  assert(s_profile_transfer.last_activity == 3);
  assert(accept_profile_part(7, 2, 3, 0, "C", 4) == WATCH_PROFILE_TRANSFER_COMPLETE);
  assert(s_profile_transfer.received_count == 3);
  size_t joined_length = 0;
  assert(watch_profile_transfer_join(&s_profile_transfer, s_profile_transfer_buffer,
                                     sizeof(s_profile_transfer_buffer), 8191, &joined_length));
  assert(joined_length == 3);
  assert(strcmp(s_profile_transfer_buffer, "ABC") == 0);

  watch_profile_transfer_reset(&s_profile_transfer);
  assert(accept_profile_part(8, 0, 3, 0, "old-", 5) == WATCH_PROFILE_TRANSFER_ACCEPTED);
  assert(accept_profile_part(8, 2, 3, 0, "tail", 6) == WATCH_PROFILE_TRANSFER_ACCEPTED);
  assert(accept_profile_part(8, 0, 3, 0, "new-", 7) == WATCH_PROFILE_TRANSFER_INVALID);
  assert(s_profile_transfer.id == -1);
  assert(s_profile_transfer.received_count == 0);
  assert(accept_profile_part(8, 1, 3, 0, "middle-", 8) == WATCH_PROFILE_TRANSFER_IGNORED);
  assert(accept_profile_part(8, 0, 3, 0, "new-", 9) == WATCH_PROFILE_TRANSFER_ACCEPTED);
  assert(accept_profile_part(8, 1, 3, 0, "middle-", 10) == WATCH_PROFILE_TRANSFER_ACCEPTED);
  assert(accept_profile_part(8, 2, 3, 0, "tail", 11) == WATCH_PROFILE_TRANSFER_COMPLETE);

  watch_profile_transfer_reset(&s_profile_transfer);
  assert(accept_profile_part(9, 0, 3, 0, "same", 12) == WATCH_PROFILE_TRANSFER_ACCEPTED);
  assert(accept_profile_part(9, 0, 2, 0, "same", 13) == WATCH_PROFILE_TRANSFER_INVALID);
  assert(s_profile_transfer.id == -1);
  assert(accept_profile_part(10, 0, 3, 0, "same", 14) == WATCH_PROFILE_TRANSFER_ACCEPTED);
  assert(accept_profile_part(10, 0, 3, 3, "same", 15) == WATCH_PROFILE_TRANSFER_INVALID);
  assert(s_profile_transfer.id == -1);

  assert(accept_profile_part(11, 0, 2, 0, "old-", 16) == WATCH_PROFILE_TRANSFER_ACCEPTED);
  assert(accept_profile_part(12, 0, 2, 0, "new-", 17) == WATCH_PROFILE_TRANSFER_ACCEPTED);
  assert(s_profile_transfer.id == 12);
  assert(s_profile_transfer.received_count == 1);
  assert(accept_profile_part(12, 1, 2, 0, "tail", 18) == WATCH_PROFILE_TRANSFER_COMPLETE);

  watch_profile_transfer_reset(&s_profile_transfer);
  assert(accept_profile_part(11, 0, 1, 0, "stale", 19) == WATCH_PROFILE_TRANSFER_IGNORED);
  assert(accept_profile_part(11, 0, 2, 0, "stale-", 19) == WATCH_PROFILE_TRANSFER_IGNORED);
  assert(accept_profile_part(11, 1, 2, 0, "tail", 19) == WATCH_PROFILE_TRANSFER_IGNORED);
  assert(accept_profile_part(12, 0, 2, 0, "new-", 20) == WATCH_PROFILE_TRANSFER_IGNORED);
  assert(s_profile_transfer.id == -1);

  assert(accept_profile_part(13, 0, 2, 0, "fresh-", 21) == WATCH_PROFILE_TRANSFER_ACCEPTED);
  assert(accept_profile_part(12, 0, 1, 0, "stale", 22) == WATCH_PROFILE_TRANSFER_IGNORED);
  assert(s_profile_transfer.id == 13);
  assert(accept_profile_part(13, 1, 2, 0, "tail", 23) == WATCH_PROFILE_TRANSFER_COMPLETE);

  watch_profile_transfer_initialize(&s_profile_transfer);
  assert(accept_profile_part(INT32_MAX, 0, 1, 0, "edge", 24) == WATCH_PROFILE_TRANSFER_COMPLETE);
  watch_profile_transfer_reset(&s_profile_transfer);
  assert(accept_profile_part(0, 0, 1, 0, "wrapped", 25) == WATCH_PROFILE_TRANSFER_COMPLETE);
  watch_profile_transfer_reset(&s_profile_transfer);
  assert(accept_profile_part(INT32_MAX, 0, 1, 0, "stale", 26) == WATCH_PROFILE_TRANSFER_IGNORED);
  assert(accept_profile_part(0x40000000, 0, 1, 0, "ambiguous", 27) ==
         WATCH_PROFILE_TRANSFER_IGNORED);
}

int main(void) {
  reset_store();
  fprintf(stderr, "[transfers-ordering] test_watch_config_transfer\n");
  test_watch_config_transfer();
  reset_store();
  fprintf(stderr, "[transfers-ordering] test_transfer_serial_reservation\n");
  test_transfer_serial_reservation();
  reset_store();
  fprintf(stderr, "[transfers-ordering] test_snapshot_epoch_ordering\n");
  test_snapshot_epoch_ordering();
  reset_store();
  fprintf(stderr, "[transfers-ordering] test_profile_transfer_reordering\n");
  test_profile_transfer_reordering();
  return 0;
}
