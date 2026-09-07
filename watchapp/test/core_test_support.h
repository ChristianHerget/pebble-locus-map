#pragma once
#include "persistent_blob.h"
#include "i18n.h"
#include "ui_metrics.h"
#include "watch_config.h"
#include "watch_state.h"
#include "watch_maintenance.h"
#include "watch_maintenance_timer.h"
#include "watch_outbound_retry.h"
#include "watch_step_state.h"

#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include <pebble.h>

#define STORE_KEYS 256
typedef struct {
  bool exists;
  size_t length;
  unsigned char data[PERSIST_DATA_MAX_LENGTH];
} StoredValue;
extern StoredValue s_store[STORE_KEYS];
extern int s_fail_write_key;
extern int s_torn_write_key;
extern size_t s_torn_write_bytes;
extern int s_fail_delete_key;
extern int s_crash_before_delete;
extern int s_delete_calls;
extern int s_crash_before_mutation;
extern int s_mutation_calls;
extern jmp_buf s_power_cut;
extern size_t s_persist_max_size;
extern const PersistentBlob TEST_BLOB;
extern const PersistentBlob ACTIVE_CONFIG_BLOB;
extern const PersistentBlob PENDING_CONFIG_BLOB;
void maybe_cut_power_before_mutation(void);
StoredValue *stored(uint32_t key);
bool persist_exists(uint32_t key);
size_t persist_used(void);
size_t persist_get_max_size(void);
int persist_get_size(uint32_t key);
int persist_read_data(uint32_t key, void *buffer, size_t buffer_size);
int persist_read_string(uint32_t key, char *buffer, size_t buffer_size);
int persist_write_data(uint32_t key, const void *data, size_t size);
int persist_write_string(uint32_t key, const char *cstring);
status_t persist_delete(uint32_t key);
void reset_store(void);
void fill(char *output, size_t length, char seed);
void assert_blob_equals(const PersistentBlob *blob, const char *expected, size_t length);
