#include "core_test_support.h"
StoredValue s_store[STORE_KEYS];
int s_fail_write_key = -1;
int s_torn_write_key = -1;
size_t s_torn_write_bytes;
int s_fail_delete_key = -1;
int s_crash_before_delete = -1;
int s_delete_calls;
int s_crash_before_mutation = -1;
int s_mutation_calls;
jmp_buf s_power_cut;
size_t s_persist_max_size = STORE_KEYS * PERSIST_DATA_MAX_LENGTH;
void maybe_cut_power_before_mutation(void) {
  if (s_crash_before_mutation >= 0 && s_mutation_calls++ == s_crash_before_mutation) {
    longjmp(s_power_cut, 1);
  }
}

StoredValue *stored(uint32_t key) {
  assert(key < STORE_KEYS);
  return &s_store[key];
}

bool persist_exists(uint32_t key) {
  return stored(key)->exists;
}

size_t persist_used(void) {
  size_t total = 0;
  for (size_t i = 0; i < STORE_KEYS; i++) {
    if (s_store[i].exists) total += s_store[i].length;
  }
  return total;
}

size_t persist_get_max_size(void) {
  return s_persist_max_size;
}

int persist_get_size(uint32_t key) {
  StoredValue *value = stored(key);
  return value->exists ? (int)value->length : E_DOES_NOT_EXIST;
}

int persist_read_data(uint32_t key, void *buffer, size_t buffer_size) {
  StoredValue *value = stored(key);
  if (!value->exists) return E_DOES_NOT_EXIST;
  const size_t size = value->length < buffer_size ? value->length : buffer_size;
  memcpy(buffer, value->data, size);
  return (int)size;
}

int persist_read_string(uint32_t key, char *buffer, size_t buffer_size) {
  return persist_read_data(key, buffer, buffer_size);
}

int persist_write_data(uint32_t key, const void *data, size_t size) {
  maybe_cut_power_before_mutation();
  if ((int)key == s_fail_write_key || size > PERSIST_DATA_MAX_LENGTH) return E_ERROR;
  StoredValue *value = stored(key);
  const size_t written_size =
      (int)key == s_torn_write_key && s_torn_write_bytes < size ? s_torn_write_bytes : size;
  if (persist_used() - (value->exists ? value->length : 0) + written_size > s_persist_max_size) {
    return E_ERROR;
  }
  if (written_size) memcpy(value->data, data, written_size);
  value->length = written_size;
  value->exists = true;
  return (int)key == s_torn_write_key ? E_ERROR : (int)size;
}

int persist_write_string(uint32_t key, const char *cstring) {
  return persist_write_data(key, cstring, strlen(cstring) + 1);
}

status_t persist_delete(uint32_t key) {
  maybe_cut_power_before_mutation();
  if (s_crash_before_delete >= 0 && s_delete_calls++ == s_crash_before_delete) {
    longjmp(s_power_cut, 1);
  }
  if ((int)key == s_fail_delete_key) return E_ERROR;
  StoredValue *value = stored(key);
  if (!value->exists) return E_DOES_NOT_EXIST;
  memset(value, 0, sizeof(*value));
  return S_TRUE;
}

void reset_store(void) {
  memset(s_store, 0, sizeof(s_store));
  s_fail_write_key = -1;
  s_torn_write_key = -1;
  s_torn_write_bytes = 0;
  s_fail_delete_key = -1;
  s_crash_before_delete = -1;
  s_delete_calls = 0;
  s_crash_before_mutation = -1;
  s_mutation_calls = 0;
  s_persist_max_size = STORE_KEYS * PERSIST_DATA_MAX_LENGTH;
}

void fill(char *output, size_t length, char seed) {
  for (size_t i = 0; i < length; i++)
    output[i] = (char)(seed + i % 20);
  output[length] = '\0';
}

const PersistentBlob TEST_BLOB = {
    .record_key = 1,
    .legacy_key = 2,
    .chunk_base = 10,
    .max_chunks = 4,
};

const PersistentBlob ACTIVE_CONFIG_BLOB = {
    .record_key = 4,
    .legacy_key = 5,
    .chunk_base = 30,
    .max_chunks = 4,
};

const PersistentBlob PENDING_CONFIG_BLOB = {
    .record_key = 7,
    .legacy_key = 8,
    .chunk_base = 50,
    .max_chunks = 4,
};
void assert_blob_equals(const PersistentBlob *blob, const char *expected, size_t length) {
  char output[1025];
  memset(output, 0xa5, sizeof(output));
  assert(persistent_blob_read(blob, output, sizeof(output)));
  assert(memcmp(output, expected, length) == 0);
  assert(output[length] == '\0');
}
