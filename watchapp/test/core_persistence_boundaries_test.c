#include "core_test_support.h"

static void test_persistent_blob_boundaries(void) {
  reset_store();
  char value[1026];
  const struct {
    const char *name;
    size_t length;
  } cases[] = {
      {"empty", 0},          {"below chunk", 255}, {"full chunk", 256},
      {"second chunk", 257}, {"maximum", 1024},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    reset_store();
    fprintf(stderr, "  %s: length=%zu expected=round trip\n", cases[i].name, cases[i].length);
    fill(value, cases[i].length, (char)('A' + i));
    assert(persistent_blob_write(&TEST_BLOB, value, cases[i].length));
    assert_blob_equals(&TEST_BLOB, value, cases[i].length);
  }
  assert(!persistent_blob_write(&TEST_BLOB, value, 1025));
  fill(value, 1024, 'E');
  assert_blob_equals(&TEST_BLOB, value, 1024);

  char too_small[1024];
  assert(!persistent_blob_read(&TEST_BLOB, too_small, sizeof(too_small)));
  assert(too_small[0] == '\0');
}

static void test_persistent_blob_capacity(void) {
  reset_store();
  char first[258];
  char second[258];
  fill(first, 257, 'A');
  fill(second, 257, 'a');

  assert(persistent_blob_write(&TEST_BLOB, first, 257));
  const size_t first_usage = persist_used();
  const size_t metadata_size = stored(TEST_BLOB.record_key)->length;
  assert(first_usage == 257 + metadata_size);

  s_persist_max_size = first_usage - 1;
  assert(!persistent_blob_write(&TEST_BLOB, second, 257));
  char unreadable[1025];
  assert(!persistent_blob_read(&TEST_BLOB, unreadable, sizeof(unreadable)));
  assert(persist_used() == 0);

  assert(persistent_blob_delete(&TEST_BLOB));
  s_persist_max_size = 257 + metadata_size - 1;
  assert(!persistent_blob_write(&TEST_BLOB, first, 257));
  assert(!persistent_blob_exists(&TEST_BLOB));

  s_persist_max_size = 257 + metadata_size;
  assert(persistent_blob_write(&TEST_BLOB, first, 257));
  assert_blob_equals(&TEST_BLOB, first, 257);

  reset_store();
  const char unrelated[] = "other persisted state";
  assert(persist_write_data(50, unrelated, sizeof(unrelated)) == (int)sizeof(unrelated));
  s_persist_max_size = sizeof(unrelated) + 257 + metadata_size - 1;
  assert(!persistent_blob_write(&TEST_BLOB, first, 257));
  assert(persist_used() == sizeof(unrelated));
  assert(!persistent_blob_exists(&TEST_BLOB));

  s_persist_max_size = sizeof(unrelated) + 257 + metadata_size;
  assert(persistent_blob_write(&TEST_BLOB, first, 257));
  assert_blob_equals(&TEST_BLOB, first, 257);
}

static void test_persistent_blob_invalid_layout(void) {
  reset_store();
  assert(!persistent_blob_delete(NULL));
  const PersistentBlob invalid[] = {
      {
          .record_key = 1,
          .legacy_key = 3,
          .chunk_base = 10,
          .max_chunks = 0,
      },
      {
          .record_key = 1,
          .legacy_key = 1,
          .chunk_base = 10,
          .max_chunks = 4,
      },
      {
          .record_key = 10,
          .legacy_key = 3,
          .chunk_base = 10,
          .max_chunks = 4,
      },
      {
          .record_key = 1,
          .legacy_key = 11,
          .chunk_base = 10,
          .max_chunks = 4,
      },
      {
          .record_key = 1,
          .legacy_key = 3,
          .chunk_base = UINT32_MAX - 1,
          .max_chunks = 4,
      },
  };
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    assert(!persistent_blob_write(&invalid[i], "value", 5));
    assert(!persistent_blob_exists(&invalid[i]));
    assert(!persistent_blob_delete(&invalid[i]));
  }
}

int main(void) {
  reset_store();
  fprintf(stderr, "[persistence-boundaries] test_persistent_blob_boundaries\n");
  test_persistent_blob_boundaries();
  reset_store();
  fprintf(stderr, "[persistence-boundaries] test_persistent_blob_capacity\n");
  test_persistent_blob_capacity();
  reset_store();
  fprintf(stderr, "[persistence-boundaries] test_persistent_blob_invalid_layout\n");
  test_persistent_blob_invalid_layout();
  return 0;
}
