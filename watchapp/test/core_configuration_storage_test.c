#include "core_test_support.h"
#include "watch_config_storage.h"

static void setup_config_replacement(const char *current, const char *queued) {
  reset_store();
  assert(persistent_blob_write(&ACTIVE_CONFIG_BLOB, current, strlen(current)));
  assert(persistent_blob_write(&PENDING_CONFIG_BLOB, queued, strlen(queued)));
}

// Composed-storage regression for the retired queued-replacement design.
// This exercises durable blob composition; current production caches only the active projection.
static bool replace_config_preserving_pending(const char *replacement) {
  char queued[1025];
  if (!persistent_blob_read(&PENDING_CONFIG_BLOB, queued, sizeof(queued)) ||
      !persistent_blob_write(&ACTIVE_CONFIG_BLOB, queued, strlen(queued)) ||
      !persistent_blob_write(&ACTIVE_CONFIG_BLOB, replacement, strlen(replacement))) {
    return false;
  }
  return persistent_blob_delete(&PENDING_CONFIG_BLOB);
}

static int config_value_kind(const char *value, const char *current, const char *queued,
                             const char *replacement) {
  if (strcmp(value, current) == 0) return 0;
  if (strcmp(value, queued) == 0) return 1;
  if (strcmp(value, replacement) == 0) return 2;
  return -1;
}

static void assert_config_replacement_invariant(const char *current, const char *queued,
                                                const char *replacement) {
  char active[1025];
  char pending[1025];
  assert(persistent_blob_read(&ACTIVE_CONFIG_BLOB, active, sizeof(active)));
  const int active_kind = config_value_kind(active, current, queued, replacement);
  assert(active_kind >= 0);

  const bool pending_exists = persistent_blob_exists(&PENDING_CONFIG_BLOB);
  const bool pending_readable =
      persistent_blob_read(&PENDING_CONFIG_BLOB, pending, sizeof(pending));
  if (pending_readable) {
    assert(strcmp(pending, queued) == 0);
    assert(active_kind == 0 || active_kind == 1);
  } else if (pending_exists) {
    // Pending deletion may have lost power after its data disappeared but before
    // metadata cleanup. Promotion must already have made the queued baseline active.
    assert(active_kind == 1);
  } else {
    assert(active_kind == 1 || active_kind == 2);
  }
  if (active_kind == 2) assert(!pending_exists);
}

static void recover_config_replacement(const char *queued, const char *replacement) {
  char pending[1025];
  if (persistent_blob_read(&PENDING_CONFIG_BLOB, pending, sizeof(pending))) {
    assert(strcmp(pending, queued) == 0);
    assert(persistent_blob_write(&ACTIVE_CONFIG_BLOB, pending, strlen(pending)));
  }
  if (persistent_blob_exists(&PENDING_CONFIG_BLOB)) {
    assert(persistent_blob_delete(&PENDING_CONFIG_BLOB));
  }
  assert(persistent_blob_write(&ACTIVE_CONFIG_BLOB, replacement, strlen(replacement)));
  assert_blob_equals(&ACTIVE_CONFIG_BLOB, replacement, strlen(replacement));
  assert(!persistent_blob_exists(&PENDING_CONFIG_BLOB));
}

static void test_config_replacement_preserves_queued_baseline(void) {
  const char current[] = "current configuration";
  const char queued[] = "confirmed queued configuration";
  const char replacement[] = "direct replacement configuration";

  setup_config_replacement(current, queued);
  s_fail_write_key = (int)ACTIVE_CONFIG_BLOB.chunk_base;
  assert(!replace_config_preserving_pending(replacement));
  s_fail_write_key = -1;
  char unreadable_active[1025];
  assert(!persistent_blob_read(&ACTIVE_CONFIG_BLOB, unreadable_active, sizeof(unreadable_active)));
  assert_blob_equals(&PENDING_CONFIG_BLOB, queued, strlen(queued));

  setup_config_replacement(current, queued);
  s_fail_delete_key = (int)PENDING_CONFIG_BLOB.chunk_base;
  assert(!replace_config_preserving_pending(replacement));
  s_fail_delete_key = -1;
  assert_blob_equals(&ACTIVE_CONFIG_BLOB, replacement, strlen(replacement));
  assert(!persistent_blob_exists(&PENDING_CONFIG_BLOB));

  setup_config_replacement(current, queued);
  s_fail_delete_key = (int)PENDING_CONFIG_BLOB.record_key;
  assert(!replace_config_preserving_pending(replacement));
  s_fail_delete_key = -1;
  assert_blob_equals(&ACTIVE_CONFIG_BLOB, replacement, strlen(replacement));
  assert(persistent_blob_exists(&PENDING_CONFIG_BLOB));
  char unreadable[1025];
  assert(!persistent_blob_read(&PENDING_CONFIG_BLOB, unreadable, sizeof(unreadable)));
  recover_config_replacement(queued, replacement);

  setup_config_replacement(current, queued);
  s_fail_write_key = (int)ACTIVE_CONFIG_BLOB.chunk_base;
  assert(!replace_config_preserving_pending(replacement));
  s_fail_write_key = -1;
  assert(!persistent_blob_read(&ACTIVE_CONFIG_BLOB, unreadable_active, sizeof(unreadable_active)));
  assert_blob_equals(&PENDING_CONFIG_BLOB, queued, strlen(queued));

  setup_config_replacement(current, queued);
  assert(replace_config_preserving_pending(replacement));
  assert_config_replacement_invariant(current, queued, replacement);

  // A same-ID application retry repeats the durable write. It is safe whether the
  // previous result was lost before or after the replacement commit.
  assert(persistent_blob_write(&ACTIVE_CONFIG_BLOB, replacement, strlen(replacement)));
  assert_blob_equals(&ACTIVE_CONFIG_BLOB, replacement, strlen(replacement));
}

static void test_startup_cache(void) {
  char buffer[WATCH_CONFIG_BUFFER_SIZE];
  WatchConfig config;
  const char valid[] = "dark|1|10|12345|4294967295|42\nDefault|1,3,5|walk";
  const PersistentBlob obsolete[] = {PENDING_CONFIG_BLOB, TEST_BLOB};
  reset_store();
  assert(watch_config_storage_load(&ACTIVE_CONFIG_BLOB, obsolete, 2, buffer, sizeof(buffer), "",
                                   &config) == WATCH_CONFIG_CACHE_UNAVAILABLE);
  assert(persistent_blob_write(&ACTIVE_CONFIG_BLOB, valid, strlen(valid)));
  assert(persistent_blob_write(&PENDING_CONFIG_BLOB, "old", 3));
  assert(persistent_blob_write(&TEST_BLOB, "catalog", 7));
  for (int startup = 0; startup < 2; startup++) {
    assert(watch_config_storage_load(&ACTIVE_CONFIG_BLOB, obsolete, 2, buffer, sizeof(buffer), "",
                                     &config) == WATCH_CONFIG_CACHE_LOADED);
    assert(!persistent_blob_exists(&PENDING_CONFIG_BLOB));
    assert(!persistent_blob_exists(&TEST_BLOB));
    assert(strcmp(config.locus_id, "12345") == 0);
    assert(config.fingerprint_a == UINT32_MAX && config.fingerprint_b == 42);
    assert(config.profile_count == 1 && config.selected == 0);
    assert(strcmp(config.profiles[0].id, "walk") == 0);
  }
  assert(persistent_blob_write(&PENDING_CONFIG_BLOB, "old", 3));
  assert(persistent_blob_write(&TEST_BLOB, "catalog", 7));
  s_fail_delete_key = (int)PENDING_CONFIG_BLOB.record_key;
  assert(watch_config_storage_load(&ACTIVE_CONFIG_BLOB, obsolete, 2, buffer, sizeof(buffer), "",
                                   &config) == WATCH_CONFIG_CACHE_LOADED);
  assert(persist_exists(PENDING_CONFIG_BLOB.record_key));
  assert(!persistent_blob_exists(&TEST_BLOB));
  s_fail_delete_key = -1;
  assert(persistent_blob_write(&ACTIVE_CONFIG_BLOB, "malformed", 9));
  assert(watch_config_storage_load(&ACTIVE_CONFIG_BLOB, obsolete, 2, buffer, sizeof(buffer), "",
                                   &config) == WATCH_CONFIG_CACHE_INVALID);
  assert(!persistent_blob_exists(&PENDING_CONFIG_BLOB));
  assert(persistent_blob_write(&ACTIVE_CONFIG_BLOB, valid, strlen(valid)));
  assert(persist_delete(ACTIVE_CONFIG_BLOB.chunk_base) == S_TRUE);
  assert(watch_config_storage_load(&ACTIVE_CONFIG_BLOB, obsolete, 2, buffer, sizeof(buffer), "",
                                   &config) == WATCH_CONFIG_CACHE_UNAVAILABLE);
}

int main(void) {
  fprintf(stderr, "[configuration-storage] test_startup_cache\n");
  test_startup_cache();
  reset_store();
  fprintf(stderr, "[configuration-storage] test_config_replacement_preserves_queued_baseline\n");
  test_config_replacement_preserves_queued_baseline();
  return 0;
}
