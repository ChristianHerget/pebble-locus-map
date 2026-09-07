#include "core_test_support.h"

typedef struct {
  int register_calls;
  int reschedule_calls;
  int cancel_calls;
  uint32_t last_delay;
  bool register_succeeds;
  bool reschedule_succeeds;
  WatchMaintenanceTimerCallback callback;
  void *callback_context;
  int callback_calls;
  WatchMaintenanceTimer *timer;
} FakeTimerOperations;

static void *fake_timer_register(void *context, uint32_t delay_ms,
                                 WatchMaintenanceTimerCallback callback, void *callback_context) {
  FakeTimerOperations *fake = context;
  fake->register_calls++;
  fake->last_delay = delay_ms;
  fake->callback = callback;
  fake->callback_context = callback_context;
  return fake->register_succeeds ? fake : NULL;
}

static bool fake_timer_reschedule(void *context, void *timer, uint32_t delay_ms) {
  FakeTimerOperations *fake = context;
  assert(timer == fake);
  fake->reschedule_calls++;
  fake->last_delay = delay_ms;
  return fake->reschedule_succeeds;
}

static void fake_timer_cancel(void *context, void *timer) {
  FakeTimerOperations *fake = context;
  assert(timer == fake);
  fake->cancel_calls++;
}

static void fake_timer_callback(void *context) {
  FakeTimerOperations *fake = context;
  fake->callback_calls++;
  if (fake->callback_calls == 1 && fake->timer) {
    assert(watch_maintenance_timer_schedule(fake->timer, true, 30, 300));
  }
}

static void test_watch_maintenance_timer(void) {
  FakeTimerOperations fake = {
      .register_succeeds = true,
      .reschedule_succeeds = true,
  };
  WatchMaintenanceTimer timer;
  fake.timer = &timer;
  watch_maintenance_timer_initialize(&timer,
                                     (WatchMaintenanceTimerOperations){
                                         .register_timer = fake_timer_register,
                                         .reschedule_timer = fake_timer_reschedule,
                                         .cancel_timer = fake_timer_cancel,
                                     },
                                     &fake, fake_timer_callback, &fake);

  assert(watch_maintenance_timer_schedule(&timer, true, 10, 100));
  assert(fake.register_calls == 1 && fake.last_delay == 100);
  assert(watch_maintenance_timer_schedule(&timer, true, 10, 90));
  assert(fake.register_calls == 1 && fake.reschedule_calls == 0);

  assert(watch_maintenance_timer_schedule(&timer, true, 20, 200));
  assert(fake.reschedule_calls == 1 && fake.last_delay == 200);
  assert(watch_maintenance_timer_schedule(&timer, false, 0, 0));
  assert(fake.cancel_calls == 1);
  assert(watch_maintenance_timer_schedule(&timer, false, 0, 0));
  assert(fake.cancel_calls == 1);

  assert(watch_maintenance_timer_schedule(&timer, true, 25, 250));
  assert(fake.register_calls == 2);
  fake.callback(fake.callback_context);
  assert(fake.callback_calls == 1);
  assert(fake.register_calls == 3 && fake.reschedule_calls == 1);

  fake.reschedule_succeeds = false;
  assert(watch_maintenance_timer_schedule(&timer, true, 40, 400));
  assert(fake.reschedule_calls == 2 && fake.cancel_calls == 2 && fake.register_calls == 4);

  fake.register_succeeds = false;
  assert(!watch_maintenance_timer_schedule(&timer, true, 50, 500));
  assert(fake.reschedule_calls == 3 && fake.cancel_calls == 3 && fake.register_calls == 5);
  fake.register_succeeds = true;
  assert(watch_maintenance_timer_schedule(&timer, true, 50, 500));
  assert(fake.register_calls == 6);
  watch_maintenance_timer_cancel(&timer);
  assert(fake.cancel_calls == 4);
}

static void test_watch_maintenance_planner(void) {
  assert(watch_maintenance_should_schedule_reconciliation(true, true, false));
  assert(!watch_maintenance_should_schedule_reconciliation(true, true, true));
  assert(!watch_maintenance_should_schedule_reconciliation(false, true, false));
  assert(!watch_maintenance_should_schedule_reconciliation(true, false, false));
  WatchContextDecision context = watch_maintenance_context_decision(true, true);
  assert(context.reset_projection_ui);
  assert(context.request_runtime_config);
  context = watch_maintenance_context_decision(true, false);
  assert(context.reset_projection_ui);
  assert(context.request_runtime_config);
  context = watch_maintenance_context_decision(false, false);
  assert(!context.reset_projection_ui);
  assert(context.request_runtime_config);
  context = watch_maintenance_context_decision(false, true);
  assert(!context.reset_projection_ui);
  assert(!context.request_runtime_config);

  WatchMaintenanceDeadlines deadlines = {0};
  deadlines.active = WATCH_MAINTENANCE_BIT(WATCH_MAINTENANCE_NO_BRIDGE) |
                     WATCH_MAINTENANCE_BIT(WATCH_MAINTENANCE_STALE);
  deadlines.deadlines[WATCH_MAINTENANCE_NO_BRIDGE] = 110;
  deadlines.deadlines[WATCH_MAINTENANCE_STALE] = 131;

  WatchMaintenancePlan plan = watch_maintenance_plan(
      &deadlines, (WatchMaintenanceClock){.seconds = 100, .milliseconds = 250});
  assert(plan.due == 0);
  assert(plan.has_next && plan.next_deadline == 110 && plan.delay_ms == 9750);

  plan = watch_maintenance_plan(&deadlines,
                                (WatchMaintenanceClock){.seconds = 110, .milliseconds = 0});
  assert(plan.due == WATCH_MAINTENANCE_BIT(WATCH_MAINTENANCE_NO_BRIDGE));
  assert(plan.has_next && plan.next_deadline == 131 && plan.delay_ms == 21000);

  deadlines.deadlines[WATCH_MAINTENANCE_STALE] = 110;
  plan = watch_maintenance_plan(&deadlines,
                                (WatchMaintenanceClock){.seconds = 110, .milliseconds = 999});
  assert(plan.due == deadlines.active);
  assert(!plan.has_next && plan.delay_ms == 0);

  deadlines.active = WATCH_MAINTENANCE_BIT(WATCH_MAINTENANCE_COMMAND);
  deadlines.deadlines[WATCH_MAINTENANCE_COMMAND] = 5;
  plan = watch_maintenance_plan(
      &deadlines, (WatchMaintenanceClock){.seconds = UINT32_MAX - 2, .milliseconds = 500});
  assert(!plan.due && plan.has_next && plan.delay_ms == 7500);
  assert(!watch_maintenance_reached(UINT32_MAX - 2, 5));
  assert(watch_maintenance_reached(5, 5));

  deadlines.active = 0;
  plan =
      watch_maintenance_plan(&deadlines, (WatchMaintenanceClock){.seconds = 1, .milliseconds = 0});
  assert(!plan.due && !plan.has_next);

  deadlines.active = WATCH_MAINTENANCE_BIT(WATCH_MAINTENANCE_RECONCILIATION);
  deadlines.deadlines[WATCH_MAINTENANCE_RECONCILIATION] = 61;
  plan = watch_maintenance_plan(&deadlines,
                                (WatchMaintenanceClock){.seconds = 60, .milliseconds = 999});
  assert(!plan.due && plan.has_next && plan.delay_ms == 1);

  plan =
      watch_maintenance_plan(&deadlines, (WatchMaintenanceClock){.seconds = 61, .milliseconds = 0});
  assert(plan.due == WATCH_MAINTENANCE_BIT(WATCH_MAINTENANCE_RECONCILIATION));
  assert(!plan.has_next && plan.delay_ms == 0);
}

int main(void) {
  reset_store();
  fprintf(stderr, "[maintenance] test_watch_maintenance_timer\n");
  test_watch_maintenance_timer();
  reset_store();
  fprintf(stderr, "[maintenance] test_watch_maintenance_planner\n");
  test_watch_maintenance_planner();
  return 0;
}
