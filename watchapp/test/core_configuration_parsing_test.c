#include "core_test_support.h"

static bool parse(const char *value, const char *active, WatchConfig *output) {
  char buffer[4096];
  const size_t length = strlen(value);
  assert(length < sizeof(buffer));
  memcpy(buffer, value, length + 1);
  return watch_config_parse(buffer, active, output);
}

static void test_watch_text_validation(void) {
  assert(watch_waypoint_name_valid("Summit note"));
  assert(watch_waypoint_name_valid("pipe | is allowed"));
  assert(watch_waypoint_name_valid("\xE2\x80\x83 summit \xE2\x80\x83"));
  assert(!watch_waypoint_name_valid(""));
  assert(!watch_waypoint_name_valid("   "));
  assert(!watch_waypoint_name_valid("\xC2\xA0\xC2\xA0"));
  assert(!watch_waypoint_name_valid("\xEF\xBB\xBF"));
  assert(!watch_waypoint_name_valid("bad\tname"));
  assert(!watch_waypoint_name_valid("\xC0\xAF"));

  char maximum[WATCH_WAYPOINT_NAME_BYTES + 2];
  memset(maximum, 'x', WATCH_WAYPOINT_NAME_BYTES);
  maximum[WATCH_WAYPOINT_NAME_BYTES] = '\0';
  assert(watch_waypoint_name_valid(maximum));
  maximum[WATCH_WAYPOINT_NAME_BYTES] = 'x';
  maximum[WATCH_WAYPOINT_NAME_BYTES + 1] = '\0';
  assert(!watch_waypoint_name_valid(maximum));

  assert(watch_profile_names_equal("\xC3\x84rger", "\xC3\xA4rger"));
  assert(watch_profile_names_equal("\xCE\xA3", "\xCF\x83"));
  assert(watch_profile_names_equal("\xD0\xAF", "\xD1\x8F"));
  assert(!watch_profile_names_equal("\xC4\x80", "\xC4\x81"));

  assert(watch_locus_profile_valid("1", "Hiking"));
  assert(watch_locus_profile_valid("0", "Default"));
  assert(watch_locus_profile_valid("-9223372036854775808", "Internal"));
  assert(watch_locus_profile_valid("9223372036854775807", "Wandern \xC3\x84"));
  assert(!watch_locus_profile_valid("-0", "Hiking"));
  assert(!watch_locus_profile_valid("9223372036854775808", "Hiking"));
  assert(!watch_locus_profile_valid("12x", "Hiking"));
  assert(!watch_locus_profile_valid("12", "bad|name"));
  assert(!watch_locus_profile_valid("12", "bad\nname"));
}

static void test_watch_config(void) {
  WatchConfig config;
  const char valid[] = "dark|1|10|12345|4294967295|42\n"
                       "Default|1,3,5|walk\n"
                       "Climb|1,10,11|climb";
  assert(parse(valid, "walk", &config));
  assert(config.profile_count == 2);
  assert(config.selected == 0);
  assert(config.dark);
  assert(config.watch_hr_to_locus);
  assert(config.heart_rate_interval == 10);
  assert(strcmp(config.locus_id, "12345") == 0);
  assert(config.fingerprint_a == UINT32_MAX);
  assert(config.fingerprint_b == 42);
  assert(!config.profiles[1].protected_profile);
  assert(config.profiles[1].metrics[2] == 11);
  assert(parse(valid, "climb", &config));
  assert(config.selected == 1);

  assert(!parse("blue|0|5|1|1|2\nOnly|1|id", NULL, &config));
  assert(!parse("dark|2|5|1|1|2\nOnly|1|id", NULL, &config));
  assert(!parse("dark|0|0|1|1|2\nOnly|1|id", NULL, &config));
  assert(parse("dark|0|5|0|1|2\nOnly|1|id", NULL, &config));
  assert(!parse("dark|0|5|-0|1|2\nOnly|1|id", NULL, &config));
  assert(!parse("dark|0|5|01|1|2\nOnly|1|id", NULL, &config));
  assert(!parse("dark|0|5|1|x|2\nOnly|1|id", NULL, &config));
  assert(!parse("dark|0|5|1|1|2\nOnly|1x|id", NULL, &config));
  assert(!parse("dark|0|5|1|1|2\nOnly|1,1|id", NULL, &config));
  assert(!parse("dark|0|5|1|1|2\nOnly|1|same\nOther|2|same", NULL, &config));
  assert(parse("dark|0|5|1|1|2\nOnly|1|one\nonly|2|two", NULL, &config));
  assert(!parse("dark|0|5|1|1|2\nOnly|1|id|extra", NULL, &config));
  assert(!parse("dark|0|5|1|1|2\n   |1|id", NULL, &config));
  assert(!parse("dark|0|5|1|1|2\nOnly|1|1234567890123456789012345678901234567890", NULL, &config));

  char unicode_name[85];
  size_t offset = 0;
  for (int i = 0; i < 20; i++) {
    const unsigned char boot[] = {0xf0, 0x9f, 0xa5, 0xbe};
    memcpy(unicode_name + offset, boot, sizeof(boot));
    offset += sizeof(boot);
  }
  unicode_name[offset] = '\0';
  char unicode_config[512];
  snprintf(unicode_config, sizeof(unicode_config), "dark|0|5|1|1|2\n%s|1|unicode", unicode_name);
  assert(parse(unicode_config, NULL, &config));
  strcat(unicode_name, "x");
  snprintf(unicode_config, sizeof(unicode_config), "dark|0|5|1|1|2\n%s|1|unicode", unicode_name);
  assert(!parse(unicode_config, NULL, &config));

  char too_many[1024] = "dark|0|5|1|1|2";
  for (int i = 0; i < 5; i++) {
    char line[80];
    snprintf(line, sizeof(line), "\nProfile%d|1|id%d", i, i);
    strcat(too_many, line);
  }
  assert(!parse(too_many, NULL, &config));

  assert(watch_profile_list_valid("1|Walking\n42|Wandern \xC3\x84",
                                  strlen("1|Walking\n42|Wandern \xC3\x84")));
  assert(watch_profile_list_valid("0|Default\n-1|Internal", strlen("0|Default\n-1|Internal")));
  assert(watch_profile_list_valid("-9223372036854775808|Minimum",
                                  strlen("-9223372036854775808|Minimum")));
  assert(!watch_profile_list_valid("1|Walking\n", strlen("1|Walking\n")));
  assert(!watch_profile_list_valid("1|Walking\n\n2|Cycling", strlen("1|Walking\n\n2|Cycling")));
  assert(!watch_profile_list_valid("1|Walking\n2|Cycling\n1|Running",
                                   strlen("1|Walking\n2|Cycling\n1|Running")));
  assert(!watch_profile_list_valid("-0|Walking", strlen("-0|Walking")));
  assert(!watch_profile_list_valid("01|Walking", strlen("01|Walking")));
  assert(!watch_profile_list_valid("-9223372036854775809|Walking",
                                   strlen("-9223372036854775809|Walking")));
  assert(!watch_profile_list_valid("1|Walk|ing", strlen("1|Walk|ing")));
  const char invalid_utf8[] = {'1', '|', 'B', 'a', 'd', (char)0xc0, (char)0xaf};
  assert(!watch_profile_list_valid(invalid_utf8, sizeof(invalid_utf8)));
}

static void test_config_acceptance(void) {
  const struct {
    const char *name;
    const char *payload;
    uint32_t fingerprint_a;
    uint32_t fingerprint_b;
    const char *locus_id;
    WatchConfigAcceptance expected;
  } cases[] = {
      {"matching", "dark|1|10|12345|1|2\nDefault|1|walk", 1, 2, "12345", WATCH_CONFIG_ACCEPTED},
      {"first fingerprint", "dark|1|10|12345|1|2\nDefault|1|walk", 9, 2, "12345",
       WATCH_CONFIG_INVALID},
      {"second fingerprint", "dark|1|10|12345|1|2\nDefault|1|walk", 1, 9, "12345",
       WATCH_CONFIG_INVALID},
      {"wrong profile", "dark|1|10|12345|1|2\nDefault|1|walk", 1, 2, "99",
       WATCH_CONFIG_WRONG_LOCUS_PROFILE},
      {"fingerprint before profile", "dark|1|10|12345|1|2\nDefault|1|walk", 9, 2, "99",
       WATCH_CONFIG_INVALID},
      {"invalid payload before profile", "invalid", 1, 2, "99", WATCH_CONFIG_INVALID},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    WatchConfig config;
    const bool valid = parse(cases[i].payload, "", &config);
    const WatchConfigAcceptance actual = watch_config_classify(
        valid, &config, cases[i].fingerprint_a, cases[i].fingerprint_b, cases[i].locus_id);
    fprintf(stderr, "  %s: payload=%s fingerprints=%lu,%lu locus=%s expected=%d actual=%d\n",
            cases[i].name, cases[i].payload, (unsigned long)cases[i].fingerprint_a,
            (unsigned long)cases[i].fingerprint_b, cases[i].locus_id, cases[i].expected, actual);
    assert(actual == cases[i].expected);
  }
}

int main(void) {
  fprintf(stderr, "[configuration-parsing] test_config_acceptance\n");
  test_config_acceptance();
  reset_store();
  fprintf(stderr, "[configuration-parsing] test_watch_text_validation\n");
  test_watch_text_validation();
  reset_store();
  fprintf(stderr, "[configuration-parsing] test_watch_config\n");
  test_watch_config();
  return 0;
}
