#pragma once

#include "persistent_blob.h"
#include "watch_config.h"

typedef enum {
  WATCH_CONFIG_CACHE_UNAVAILABLE,
  WATCH_CONFIG_CACHE_INVALID,
  WATCH_CONFIG_CACHE_LOADED,
} WatchConfigCacheResult;

// Obsolete cache deletion is best effort. Buffers belong to the caller.
WatchConfigCacheResult watch_config_storage_load(const PersistentBlob *active,
                                                 const PersistentBlob *obsolete,
                                                 size_t obsolete_count, char *buffer,
                                                 size_t buffer_size, const char *active_id,
                                                 WatchConfig *output);
