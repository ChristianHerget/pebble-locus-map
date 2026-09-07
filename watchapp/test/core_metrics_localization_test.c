#include "core_test_support.h"

static void test_ui_metrics(void) {
  UiMetricSnapshot snapshot = {
      .state = 1,
      .moving_time = 65,
      .elapsed = 90061,
      .distance = 123,
      .current_speed = 90,
      .current_pace = 301,
      .current_hr = 142,
      .altitude_format = FORMAT_M_0,
      .distance_format = FORMAT_KM_1,
      .moving_distance_format = FORMAT_MI_2,
      .current_speed_format = FORMAT_KPH_1,
      .average_speed_format = FORMAT_MPH_1,
      .max_speed_format = FORMAT_KNOT_0,
      .vertical_speed_format = FORMAT_MPS_2,
      .slope_format = FORMAT_PERCENT_0,
      .energy_format = FORMAT_KJ_0,
      .pace_format = FORMAT_PER_KM,
  };
  char output[24];
  assert(i18n_catalog_complete());
  assert(i18n_locale("de_DE") == I18N_LOCALE_DE);
  assert(i18n_locale("de-DE") == I18N_LOCALE_DE);
  assert(i18n_locale("en_US") == I18N_LOCALE_EN);
  assert(i18n_locale("fr_FR") == I18N_LOCALE_FR);
  assert(i18n_locale("es_ES") == I18N_LOCALE_ES);
  assert(i18n_locale("it_IT") == I18N_LOCALE_IT);
  assert(i18n_locale("pt_PT") == I18N_LOCALE_PT);
  assert(i18n_locale("zh_CN") == I18N_LOCALE_ZH_CN);
  assert(i18n_locale("zh_TW") == I18N_LOCALE_ZH_TW);
  i18n_set_locale(I18N_LOCALE_EN);
  assert(strcmp(ui_metric_label(METRIC_DISTANCE), "Distance") == 0);
  ui_metric_format(output, sizeof(output), METRIC_ELAPSED, &snapshot);
  assert(strcmp(output, "25:01") == 0);
  ui_metric_format(output, sizeof(output), METRIC_DISTANCE, &snapshot);
  assert(strcmp(output, "12.3 km") == 0);
  ui_metric_format(output, sizeof(output), METRIC_CURRENT_SPEED, &snapshot);
  assert(strcmp(output, "9.0 km/h") == 0);
  ui_metric_format(output, sizeof(output), METRIC_CURRENT_PACE, &snapshot);
  assert(strcmp(output, "5:01 /km") == 0);
  ui_metric_format(output, sizeof(output), METRIC_CURRENT_HR, &snapshot);
  assert(strcmp(output, "142 bpm") == 0);
  snapshot.current_pace = UI_METRIC_UNAVAILABLE;
  ui_metric_format(output, sizeof(output), METRIC_CURRENT_PACE, &snapshot);
  assert(strcmp(output, "—") == 0);
  snapshot.current_pace = 301;
  snapshot.distance = -123;
  i18n_set_locale(I18N_LOCALE_DE);
  assert(strcmp(ui_metric_label(METRIC_DISTANCE), "Strecke") == 0);
  ui_metric_format(output, sizeof(output), METRIC_DISTANCE, &snapshot);
  assert(strcmp(output, "-12,3 km") == 0);
  snapshot.distance = INT32_MIN + 1;
  snapshot.distance_format = FORMAT_MI_2;
  ui_metric_format(output, sizeof(output), METRIC_DISTANCE, &snapshot);
  assert(strcmp(output, "-21474836,47 mi") == 0);
  for (int format = 0; format < FORMAT_COUNT; format++)
    assert(ui_format_code_valid(format));
  assert(!ui_format_code_valid(-1));
  assert(!ui_format_code_valid(FORMAT_COUNT));
  snapshot.distance = 0;
  assert(ui_metric_snapshot_valid(&snapshot));
  snapshot.pace_format = FORMAT_KPH_1;
  assert(!ui_metric_snapshot_valid(&snapshot));
  snapshot.pace_format = FORMAT_PER_KM;
  snapshot.distance_format = FORMAT_KPH_1;
  assert(!ui_metric_snapshot_valid(&snapshot));
}

int main(void) {
  reset_store();
  fprintf(stderr, "[metrics-localization] test_ui_metrics\n");
  test_ui_metrics();
  return 0;
}
