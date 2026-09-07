#include "core_test_support.h"

static bool delete_with_power_cut(const PersistentBlob *blob, int crash_before_delete) {
  s_delete_calls = 0;
  s_crash_before_delete = crash_before_delete;
  if (setjmp(s_power_cut) != 0) {
    s_crash_before_delete = -1;
    return false;
  }
  const bool deleted = persistent_blob_delete(blob);
  s_crash_before_delete = -1;
  assert(deleted);
  return true;
}

static void test_persistent_blob_recovery(void) {
  reset_store();
  const PersistentBlob blob = {
      .record_key = 1,
      .legacy_key = 2,
      .chunk_base = 10,
      .max_chunks = 4,
  };
  char first[701];
  char second[601];
  fill(first, 700, 'A');
  fill(second, 600, 'a');

  assert(!persistent_blob_exists(&blob));
  assert(persistent_blob_write(&blob, first, strlen(first)));
  assert(persistent_blob_exists(&blob));
  assert_blob_equals(&blob, first, strlen(first));

  s_fail_write_key = 10;
  assert(!persistent_blob_write(&blob, second, strlen(second)));
  s_fail_write_key = -1;
  char output[1025];
  assert(!persistent_blob_read(&blob, output, sizeof(output)));
  assert(output[0] == '\0');

  s_torn_write_key = 10;
  s_torn_write_bytes = 113;
  assert(!persistent_blob_write(&blob, second, strlen(second)));
  s_torn_write_key = -1;
  assert(!persistent_blob_read(&blob, output, sizeof(output)));

  s_fail_write_key = 1;
  assert(!persistent_blob_write(&blob, second, strlen(second)));
  s_fail_write_key = -1;
  assert(!persistent_blob_read(&blob, output, sizeof(output)));

  s_torn_write_key = 1;
  s_torn_write_bytes = 7;
  assert(!persistent_blob_write(&blob, second, strlen(second)));
  s_torn_write_key = -1;
  assert(!persistent_blob_read(&blob, output, sizeof(output)));

  assert(persistent_blob_write(&blob, second, strlen(second)));
  assert_blob_equals(&blob, second, strlen(second));

  const char legacy[] = "legacy configuration";
  assert(persist_write_string(blob.legacy_key, legacy) == (int)sizeof(legacy));
  stored(blob.record_key)->data[4] = 0xff;
  assert(!persistent_blob_read(&blob, output, sizeof(output)));
  assert(output[0] == '\0');

  assert(persistent_blob_delete(&blob));
  assert(!persistent_blob_exists(&blob));

  assert(persist_write_string(blob.legacy_key, legacy) == (int)sizeof(legacy));
  assert(persistent_blob_read(&blob, output, sizeof(output)));
  assert(strcmp(legacy, output) == 0);
  assert(persistent_blob_write(&blob, first, strlen(first)));
  assert(!persist_exists(blob.legacy_key));
  assert(persistent_blob_delete(&blob));
  assert(persistent_blob_delete(&blob));
}

static void test_persistent_blob_legacy_barrier(void) {
  const char legacy[] = "legacy configuration";
  char current[301];
  char output[1025];
  fill(current, 300, 'A');

  reset_store();
  assert(persist_write_string(TEST_BLOB.legacy_key, legacy) == (int)sizeof(legacy));
  s_fail_delete_key = (int)TEST_BLOB.legacy_key;
  assert(persistent_blob_write(&TEST_BLOB, current, 300));
  s_fail_delete_key = -1;
  assert(persistent_blob_read(&TEST_BLOB, output, sizeof(output)));
  assert(memcmp(output, current, 300) == 0);
  assert(output[300] == '\0');
  assert(persist_exists(TEST_BLOB.legacy_key));

  reset_store();
  assert(persist_write_string(TEST_BLOB.legacy_key, legacy) == (int)sizeof(legacy));
  s_fail_write_key = (int)TEST_BLOB.chunk_base;
  assert(!persistent_blob_write(&TEST_BLOB, current, 300));
  s_fail_write_key = -1;
  assert(persistent_blob_read(&TEST_BLOB, output, sizeof(output)));
  assert(strcmp(output, legacy) == 0);

  reset_store();
  assert(persistent_blob_write(&TEST_BLOB, current, 300));
  assert(persist_write_string(TEST_BLOB.legacy_key, legacy) == (int)sizeof(legacy));
  stored(TEST_BLOB.chunk_base)->length--;
  memset(output, 0xa5, sizeof(output));
  assert(!persistent_blob_read(&TEST_BLOB, output, sizeof(output)));
  assert(output[0] == '\0');

  reset_store();
  assert(persistent_blob_write(&TEST_BLOB, current, 300));
  assert(persist_write_string(TEST_BLOB.legacy_key, legacy) == (int)sizeof(legacy));
  stored(TEST_BLOB.record_key)->data[4] ^= 1;
  memset(output, 0xa5, sizeof(output));
  assert(!persistent_blob_read(&TEST_BLOB, output, sizeof(output)));
  assert(output[0] == '\0');

  reset_store();
  assert(persist_write_string(TEST_BLOB.legacy_key, legacy) == (int)sizeof(legacy));
  s_torn_write_key = (int)TEST_BLOB.record_key;
  s_torn_write_bytes = 7;
  s_fail_delete_key = (int)TEST_BLOB.record_key;
  assert(!persistent_blob_write(&TEST_BLOB, current, 300));
  s_torn_write_key = -1;
  s_fail_delete_key = -1;
  assert(persist_get_size(TEST_BLOB.record_key) == 7);
  assert(!persistent_blob_read(&TEST_BLOB, output, sizeof(output)));
  assert(output[0] == '\0');
}

static void test_persistent_blob_delete_power_cuts(void) {
  char first[301];
  char second[302];
  const char legacy[] = "ancient configuration";
  fill(first, 300, 'A');
  fill(second, 301, 'a');

  bool reached_completion = false;
  for (int cut = 0; cut < 20; cut++) {
    reset_store();
    assert(persistent_blob_write(&TEST_BLOB, first, 300));
    assert(persistent_blob_write(&TEST_BLOB, second, 301));
    assert(persist_write_string(TEST_BLOB.legacy_key, legacy) == (int)sizeof(legacy));

    if (delete_with_power_cut(&TEST_BLOB, cut)) {
      reached_completion = true;
      assert(!persistent_blob_exists(&TEST_BLOB));
      break;
    }

    char output[1025];
    const bool readable = persistent_blob_read(&TEST_BLOB, output, sizeof(output));
    if (readable) {
      const bool current_value = memcmp(output, second, 301) == 0 && output[301] == '\0';
      assert(current_value || strcmp(output, legacy) == 0);
    } else {
      assert(output[0] == '\0');
    }
    assert(persistent_blob_delete(&TEST_BLOB));
    assert(!persistent_blob_exists(&TEST_BLOB));
  }
  assert(reached_completion);
}

static void test_persistent_blob_delete_failures(void) {
  reset_store();
  char first[301];
  char second[302];
  char third[303];
  fill(first, 300, 'A');
  fill(second, 301, 'a');
  fill(third, 302, 'K');

  assert(persistent_blob_write(&TEST_BLOB, first, 300));
  assert(persistent_blob_write(&TEST_BLOB, second, 301));

  s_fail_delete_key = (int)TEST_BLOB.record_key;
  assert(!persistent_blob_write(&TEST_BLOB, third, 302));
  s_fail_delete_key = -1;
  assert_blob_equals(&TEST_BLOB, second, 301);

  s_fail_delete_key = (int)TEST_BLOB.chunk_base;
  assert(!persistent_blob_write(&TEST_BLOB, third, 302));
  s_fail_delete_key = -1;
  char unreadable[1025];
  assert(!persistent_blob_read(&TEST_BLOB, unreadable, sizeof(unreadable)));
  assert(persistent_blob_write(&TEST_BLOB, third, 302));
  assert_blob_equals(&TEST_BLOB, third, 302);

  s_fail_delete_key = (int)TEST_BLOB.record_key;
  assert(!persistent_blob_delete(&TEST_BLOB));
  assert(persist_exists(TEST_BLOB.record_key));
  s_fail_delete_key = -1;
  assert(persistent_blob_delete(&TEST_BLOB));
  assert(!persistent_blob_exists(&TEST_BLOB));

  assert(persistent_blob_write(&TEST_BLOB, first, 300));
  s_fail_delete_key = (int)TEST_BLOB.chunk_base;
  assert(!persistent_blob_delete(&TEST_BLOB));
  assert(persist_exists(TEST_BLOB.chunk_base));
  s_fail_delete_key = -1;
  assert(persistent_blob_delete(&TEST_BLOB));
  assert(!persistent_blob_exists(&TEST_BLOB));
}

int main(void) {
  reset_store();
  fprintf(stderr, "[persistence-recovery] test_persistent_blob_recovery\n");
  test_persistent_blob_recovery();
  reset_store();
  fprintf(stderr, "[persistence-recovery] test_persistent_blob_legacy_barrier\n");
  test_persistent_blob_legacy_barrier();
  reset_store();
  fprintf(stderr, "[persistence-recovery] test_persistent_blob_delete_power_cuts\n");
  test_persistent_blob_delete_power_cuts();
  reset_store();
  fprintf(stderr, "[persistence-recovery] test_persistent_blob_delete_failures\n");
  test_persistent_blob_delete_failures();
  return 0;
}
