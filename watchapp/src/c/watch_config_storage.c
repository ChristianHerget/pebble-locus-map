#include "watch_config_storage.h"

WatchConfigCacheResult watch_config_storage_load(const PersistentBlob *active,
                                                 const PersistentBlob *obsolete,
                                                 size_t obsolete_count, char *buffer,
                                                 size_t buffer_size, const char *active_id,
                                                 WatchConfig *output) {
  for (size_t i = 0; i < obsolete_count; i++) {
    (void)persistent_blob_delete(&obsolete[i]);
  }
  if (!persistent_blob_read(active, buffer, buffer_size)) return WATCH_CONFIG_CACHE_UNAVAILABLE;
  return watch_config_parse(buffer, active_id, output) ? WATCH_CONFIG_CACHE_LOADED
                                                       : WATCH_CONFIG_CACHE_INVALID;
}
