# Issue 70 scenario review

This is a one-time migration record against baseline `94ee511`, not a permanent test-count gate.
The split preserves 28 Android methods, 19 C functions, and 80 Python methods. Each baseline name
was checked against exactly one destination. Android bodies retain their synchronization,
assertions, cancellation, and cleanup. A whitespace-normalized body comparison confirmed all
28 Android bodies and 18 C bodies unchanged; Python AST comparison found only the five planned
behavior-test replacements changed. Specialized fakes remain beside their scenarios; common
factories and fakes live in test support.

The distance table in `SnapshotFormatterTest.distanceThresholdsAndPrecisionMatchLocusMediumFormatting`
retains inputs 999.9, 1000, 99,999, 100,000, 304.7, 305, 914.3, 915, 1852, and 1852.1 metres with
the original unit families and results. The speed table retains 25 and 28 metres/second, adding an
exact 900/KPH_1 assertion to the former format-only check. Each failure identifies the named case,
input, expected value, and actual value.

`test_persistent_blob_boundaries` retains lengths 0, 255, 256, 257, and 1024 as named cases, plus
the explicit 1025-byte rejection and undersized-read checks. Recovery and transfer sequences stay
explicit. The retired queued-replacement function remains a composed-storage regression; it does
not claim to match the current active-cache production flow.

The three device permission/location methods and two cleanup implementation-text methods now
execute shell fakes. Their names remain in the mapping. Static security, packaging, protocol,
and large-buffer checks remain; UI wiring assertions explain their need for a broader harness.
New C scenarios cover acceptance classification and cache startup, beyond the baseline mapping.

## Android instrumentation

All names below originated in `BridgeRuntimeTest`.

| Baseline scenario | Destination |
| --- | --- |
| `openingAnotherWatchReplacesTheActiveLifecycle` | `BridgeRuntimeLifecycleTest` |
| `replacementRequiresContextAndLateOldWatchCloseDoesNotInvalidateReplacement` | `BridgeRuntimeLifecycleTest` |
| `inboundMessageAfterProcessRestartRecoversTheOpenWatchLifecycle` | `BridgeRuntimeLifecycleTest` |
| `commandResultsAndRefreshesReturnOnlyToTheirSourceWatch` | `BridgeRuntimeCommandsTest` |
| `commandResultFollowsANewerAcceptedSnapshotAndLateOldDeliveryIsRejected` | `BridgeRuntimeCommandsTest` |
| `commandResultIsNotIssuedWhenThePostCommandSnapshotCannotBeDelivered` | `BridgeRuntimeCommandsTest` |
| `delayedLocusTransitionIsObservedBeforeAnOkCommandResult` | `BridgeRuntimeCommandsTest` |
| `unconfirmedTransitionReturnsFailedAndDedupeSkipsObsoleteTargetPolling` | `BridgeRuntimeCommandsTest` |
| `pauseResumeUsesTheTargetFromTheGatewaysExactRoutingDecision` | `BridgeRuntimeCommandsTest` |
| `commandFromAnExpiredConnectionSessionFailsBeforeMutation` | `BridgeRuntimeConnectionAuthorityTest` |
| `revocationWaitsOnlyForTheExactLocusActionNotConfirmationOrDelivery` | `BridgeRuntimeConnectionAuthorityTest` |
| `heartRateConsumerSurvivesOneSampleFailureAndRoutesTheNextUpdate` | `BridgeRuntimeHeartRateTest` |
| `queuedHeartRateSampleIsDroppedAcrossConnectionReset` | `BridgeRuntimeHeartRateTest` |
| `queuedHeartRateSampleIsDroppedWhenTheDeferredSelectionGuardIsNowFalse` | `BridgeRuntimeHeartRateTest` |
| `revocationWaitsForAnAdmittedHeartRateMutationToFinish` | `BridgeRuntimeHeartRateTest` |
| `selectionLossClearsTheActiveWatchSoARealReopenStartsPollingAgain` | `BridgeRuntimeConnectionAuthorityTest` |
| `connectionResetCancelsOldPollingBeforeTheNewSessionReopens` | `BridgeRuntimeConnectionAuthorityTest` |
| `staleSnapshotPublicationCannotOverwriteNewSessionDiagnostics` | `BridgeRuntimeConnectionAuthorityTest` |
| `profileTransfersForTheActiveWatchAreSerialized` | `BridgeRuntimeProfilesContextTest` |
| `unresolvedActiveProfileRefreshesCatalogBeforeSendingContext` | `BridgeRuntimeProfilesContextTest` |
| `failedContextDeliveryRemainsPendingAfterSnapshotSuccess` | `BridgeRuntimeProfilesContextTest` |
| `pauseResumeAndNameOnlyChangesRemainSnapshotOnlyButProfileIdChangeResendsContext` | `BridgeRuntimeProfilesContextTest` |
| `stoppedSnapshotInvalidatesContextBeforeSnapshotDeliveryFailure` | `BridgeRuntimeProfilesContextTest` |
| `failedOrInvalidProfileQueriesNeverSendAnAuthoritativeEmptyTransfer` | `BridgeRuntimeProfilesContextTest` |
| `successfulEmptyProfileQuerySendsTheAuthoritativeEmptyResult` | `BridgeRuntimeProfilesContextTest` |
| `type4AuthorityThenType12ReturnsSourceNeutralAccumulatedSteps` | `BridgeRuntimeStepsTest` |
| `stepIngressRejectsWrongAuthorityIdentityTrustRecordingAndReplay` | `BridgeRuntimeStepsTest` |
| `rejectedProfileQueryDoesNotConsumeATransferIdentifier` | `BridgeRuntimeProfilesContextTest` |

## C groups

All names below originated in `watch_core_test.c`.

| Baseline scenario | Destination |
| --- | --- |
| `test_watch_maintenance_timer` | `maintenance` |
| `test_watch_maintenance_planner` | `maintenance` |
| `test_step_state_transitions_and_sampling` | `steps` |
| `test_step_state_freezes_identity_and_never_reuses_sequences` | `steps` |
| `test_ui_metrics` | `metrics-localization` |
| `test_persistent_blob_boundaries` | `persistence-boundaries` |
| `test_persistent_blob_capacity` | `persistence-boundaries` |
| `test_persistent_blob_invalid_layout` | `persistence-boundaries` |
| `test_persistent_blob_recovery` | `persistence-recovery` |
| `test_persistent_blob_legacy_barrier` | `persistence-recovery` |
| `test_persistent_blob_delete_power_cuts` | `persistence-recovery` |
| `test_persistent_blob_delete_failures` | `persistence-recovery` |
| `test_watch_text_validation` | `configuration-parsing` |
| `test_watch_config` | `configuration-parsing` |
| `test_watch_config_transfer` | `transfers-ordering` |
| `test_transfer_serial_reservation` | `transfers-ordering` |
| `test_snapshot_epoch_ordering` | `transfers-ordering` |
| `test_profile_transfer_reordering` | `transfers-ordering` |
| `test_config_replacement_preserves_queued_baseline` | `configuration-storage` |

## Python groups

All names below originated in `test_scripts.py`.

| Baseline scenario | Destination |
| --- | --- |
| `ActionPinPolicyTest.test_actual_deploy_pages_5_0_1_update_is_accepted` | `test_action_pin_policy.ActionPinPolicyTest` |
| `ActionPinPolicyTest.test_each_action_accepts_patch_and_minor_updates_and_both_step_layouts` | `test_action_pin_policy.ActionPinPolicyTest` |
| `ActionPinPolicyTest.test_each_action_rejects_every_malformed_occurrence` | `test_action_pin_policy.ActionPinPolicyTest` |
| `ActionPinPolicyTest.test_missing_actions_cannot_be_satisfied_by_comments_strings_or_other_actions` | `test_action_pin_policy.ActionPinPolicyTest` |
| `ActionPinPolicyTest.test_active_declaration_after_block_scalar_is_checked` | `test_action_pin_policy.ActionPinPolicyTest` |
| `ActionPinPolicyTest.test_codeql_accepts_coordinated_updates` | `test_action_pin_policy.ActionPinPolicyTest` |
| `ActionPinPolicyTest.test_codeql_rejects_missing_extra_or_inconsistent_steps` | `test_action_pin_policy.ActionPinPolicyTest` |
| `ReleaseWorkflowTest.test_private_key_is_always_removed_before_public_processing` | `test_release_workflows.ReleaseWorkflowTest` |
| `ReleaseWorkflowTest.test_pages_actions_pin_stable_releases_in_approved_majors` | `test_release_workflows.ReleaseWorkflowTest` |
| `ReleaseWorkflowTest.test_virustotal_submission_is_pinned_protected_and_rate_limited` | `test_release_workflows.ReleaseWorkflowTest` |
| `ReleaseWorkflowTest.test_virustotal_failure_blocks_draft_creation` | `test_release_workflows.ReleaseWorkflowTest` |
| `ReleaseWorkflowTest.test_draft_build_is_artifact_only_attested_and_checks_tag_twice` | `test_release_workflows.ReleaseWorkflowTest` |
| `ReleaseWorkflowTest.test_publication_reverifies_after_pages_review` | `test_release_workflows.ReleaseWorkflowTest` |
| `ReleaseWorkflowTest.test_release_sboms_are_runtime_only_validated_and_generated_without_general_tests` | `test_release_workflows.ReleaseWorkflowTest` |
| `ReleaseWorkflowTest.test_release_notes_receive_complete_github_release_history` | `test_release_workflows.ReleaseWorkflowTest` |
| `ContinuousIntegrationWorkflowTest.test_validation_uses_protected_pull_requests_and_certifies_main_pushes` | `test_ci_policy.ContinuousIntegrationWorkflowTest` |
| `ContinuousIntegrationWorkflowTest.test_dependency_review_is_one_pull_request_only_pinned_v5_check` | `test_ci_policy.ContinuousIntegrationWorkflowTest` |
| `ContinuousIntegrationWorkflowTest.test_codeql_actions_share_one_full_sha_and_stable_v4_release` | `test_ci_policy.ContinuousIntegrationWorkflowTest` |
| `ContinuousIntegrationWorkflowTest.test_every_pull_request_runs_one_hosted_acceptance_pass` | `test_ci_policy.ContinuousIntegrationWorkflowTest` |
| `ContinuousIntegrationWorkflowTest.test_obsolete_probe_and_self_hosted_jobs_are_absent` | `test_ci_policy.ContinuousIntegrationWorkflowTest` |
| `ContinuousIntegrationWorkflowTest.test_failure_artifact_is_short_lived_and_binary_free_by_construction` | `test_ci_policy.ContinuousIntegrationWorkflowTest` |
| `ContinuousIntegrationWorkflowTest.test_dependabot_tracks_github_actions_weekly` | `test_ci_policy.ContinuousIntegrationWorkflowTest` |
| `ContinuousIntegrationWorkflowTest.test_actionlint_is_checksum_pinned_in_the_build_container` | `test_ci_policy.ContinuousIntegrationWorkflowTest` |
| `ContinuousIntegrationWorkflowTest.test_node_is_checksum_pinned_in_the_build_container` | `test_ci_policy.ContinuousIntegrationWorkflowTest` |
| `ContinuousIntegrationWorkflowTest.test_manual_acceptance_can_compare_source_and_published_provisioning` | `test_ci_policy.ContinuousIntegrationWorkflowTest` |
| `PublishedCiImageTest.test_image_set_uses_only_immutable_digest_pins` | `test_published_images.PublishedCiImageTest` |
| `PublishedCiImageTest.test_local_signature_verification_uses_a_digest_pinned_cosign_fallback` | `test_published_images.PublishedCiImageTest` |
| `PublishedCiImageTest.test_acceptance_runner_embeds_only_the_public_pebble_app_fixture` | `test_published_images.PublishedCiImageTest` |
| `PublishedCiImageTest.test_docker_cleanup_uses_the_published_runner_when_generator_is_absent` | `test_cleanup.CleanupScopeTest` |
| `PublishedCiImageTest.test_cleanup_does_not_mask_a_failed_artifact_deletion` | `test_cleanup.CleanupScopeTest` |
| `PublishedCiImageTest.test_docker_context_excludes_everything_except_public_build_inputs` | `test_published_images.PublishedCiImageTest` |
| `PublishedCiImageTest.test_kotlin_codeql_toolchain_is_a_separate_image` | `test_published_images.PublishedCiImageTest` |
| `PublishedCiImageTest.test_kotlin_codeql_traces_a_manual_gradle_build_in_the_pinned_image` | `test_published_images.PublishedCiImageTest` |
| `PublishedCiImageTest.test_image_invalidation_keys_cover_pins_and_relevant_inputs` | `test_published_images.PublishedCiImageTest` |
| `PublishedCiImageTest.test_verifier_rejects_mutable_tags_before_external_tools_are_needed` | `test_published_images.PublishedCiImageTest` |
| `PublishedCiImageTest.test_publication_is_protected_signed_attested_and_least_privilege` | `test_published_images.PublishedCiImageTest` |
| `DeviceReadinessTest.test_tap_text_targets_the_visible_part_of_a_clipped_control` | `test_device_readiness.DeviceReadinessTest` |
| `DeviceReadinessTest.test_tap_text_initializes_its_timeout_before_deadline_expansion` | `test_device_readiness.DeviceReadinessTest` |
| `DeviceReadinessTest.test_wait_for_android_retries_a_failed_initial_connect` | `test_device_readiness.DeviceReadinessTest` |
| `DeviceReadinessTest.test_wait_for_android_sets_wartburg_through_the_emulator_console` | `test_device_readiness.DeviceReadinessTest` |
| `DeviceReadinessTest.test_emulator_console_token_stays_in_the_private_runtime_volume` | `test_device_readiness.DeviceReadinessTest` |
| `DeviceReadinessTest.test_locus_acceptance_permissions_include_the_device_idle_allowlist` | `test_device_readiness.DeviceReadinessTest` |
| `DeviceReadinessTest.test_coreapp_onboarding_grants_only_its_notification_listener` | `test_device_readiness.DeviceReadinessTest` |
| `CleanupScopeTest.test_clean_revalidates_every_pod_and_volume_prefix` | `test_cleanup.CleanupScopeTest` |
| `CleanupScopeTest.test_docker_clean_uses_the_generator_to_remove_root_owned_outputs_first` | `test_cleanup.CleanupScopeTest` |
| `StaticPreflightTest.test_headless_acceptance_build_does_not_repeat_the_static_suite` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_acceptance_relaunches_locus_after_each_cold_boot` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_acceptance_retries_external_settings_webview_loading` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_acceptance_uses_the_manifest_activity_class_not_the_application_id` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_e2e_sideloads_the_pbw_with_coreapps_private_selinux_label` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_e2e_dismisses_a_stale_coreapp_onboarding_gate` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_e2e_polls_until_the_watch_settings_webview_is_rendered` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_e2e_commits_general_edits_before_saving_the_overview` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_e2e_starts_recording_through_the_debug_only_locus_api_surface` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_emery_retries_the_streamed_heart_rate_during_locus_ingestion` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_emery_and_gabbro_exercise_deterministic_watch_steps` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_static_path_does_not_require_acceptance_inputs` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_acceptance_suite_shares_warm_and_fresh_test_stages` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_acceptance_suite_rejects_more_than_two_watch_passes` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_acceptance_release_expectations_come_from_package_metadata` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_acceptance_doctor_reports_provisioning_without_burdening_static` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_golden_marker_covers_every_material_bootstrap_input` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_bootstrap_closes_locus_and_waits_for_guest_shutdown_before_reuse` | `test_acceptance_orchestration.AcceptanceOrchestrationTest` |
| `StaticPreflightTest.test_large_emulator_downloads_resume_and_retry_transport_failures` | `test_static_preflight.StaticPreflightTest` |
| `StaticPreflightTest.test_emulator_handles_the_fallback_discovery_path` | `test_static_preflight.StaticPreflightTest` |
| `StaticPreflightTest.test_pinned_emulator_generator_applies_its_dest_path_compatibility_patch` | `test_static_preflight.StaticPreflightTest` |
| `StaticPreflightTest.test_webrtc_image_generates_the_javascript_protocol_module` | `test_static_preflight.StaticPreflightTest` |
| `StaticPreflightTest.test_static_path_keeps_development_dependencies_in_a_container` | `test_static_preflight.StaticPreflightTest` |
| `StaticPreflightTest.test_sanitized_diagnostics_allowlist_excludes_installable_binaries` | `test_static_preflight.StaticPreflightTest` |
| `StaticPreflightTest.test_diagnostics_staging_copies_only_the_allowlist` | `test_static_preflight.StaticPreflightTest` |
| `StaticPreflightTest.test_doctor_requires_an_explicit_scope` | `test_static_preflight.StaticPreflightTest` |
| `StaticPreflightTest.test_image_refresh_is_explicit` | `test_static_preflight.StaticPreflightTest` |
| `StaticPreflightTest.test_release_manifest_policy_reads_the_compiled_apk_without_pipefail` | `test_static_preflight.StaticPreflightTest` |
| `ManualLabHarnessTest.test_manual_requires_one_supported_platform_before_host_preflight` | `test_manual_harness.ManualLabHarnessTest` |
| `ManualLabHarnessTest.test_manual_is_the_only_runtime_that_publishes_the_dashboard` | `test_manual_harness.ManualLabHarnessTest` |
| `ManualLabHarnessTest.test_manual_clones_and_protects_the_golden_state` | `test_manual_harness.ManualLabHarnessTest` |
| `ManualLabHarnessTest.test_manual_web_image_is_versioned_and_proxies_only_an_internal_api` | `test_manual_harness.ManualLabHarnessTest` |
| `ManualLabHarnessTest.test_dashboard_has_official_keyboard_mappings_and_ignores_text_editing` | `test_manual_harness.ManualLabHarnessTest` |
| `ManualLabHarnessTest.test_manual_setup_installs_current_artifacts_and_finishes_stopped` | `test_manual_harness.ManualLabHarnessTest` |
| `PrivateApkFingerprintTest.test_fingerprint_is_location_independent_and_content_sensitive` | `test_locus_fixture.PrivateApkFingerprintTest` |

## Focused reads and verification

Representative reads used the lifecycle class and common runtime support (77 and 284 lines),
`test_device_readiness.py` and shared policy support (247 and 85 lines), and the configuration
parsing group (163 lines), in place of the former 1,910-, 1,368-, and 1,319-line aggregate files.
The native commands in `docs/testing.md` select these areas without a shared command wrapper.

Completed checks on 2026-09-07:

- `./tools/podman-test static`: Python discovery, formatting, C analysis, Android JVM/build and
  instrumentation compilation, watch tests/lint, both Pebble builds, and packaging passed.
- `./tools/podman-test documentation` and `./tools/podman-test release-check` passed.
- Every C group passed independently and through its no-argument aggregate, with ASan/UBSan,
  the terminating UBSan probe, and production frame/stack limits. `--list`, an unknown group,
  and excess arguments were checked. A deliberately wrong expectation in a temporary copy
  produced the group, function, named case, payload, expected result, and actual result.
- Every split Python module passed independently, and aggregate discovery passed all 186 tests.
  Cleanup failure cases cover both Docker cleanup and host filesystem cleanup.
- Relative links and heading anchors in the updated developer guides passed validation.
- `./tools/podman-test acceptance-suite --locus-apks /home/christian/.local/share/trackglance-acceptance/locus-apks`
  passed with the pinned API 32 emulator and warm private fixture: all 61 instrumentation tests,
  Emery acceptance (335 seconds), and Gabbro acceptance (311 seconds), including launch and
  settings opening on both watch platforms. Disposable runtime cleanup passed.
- Native instrumentation filters independently executed Lifecycle (3), Commands (6), Connection
  Authority (5), Profiles/Context (8), Heart Rate (4), and Steps (2). The lifecycle method filter
  executed exactly `openingAnotherWatchReplacesTheActiveLifecycle`. Fresh XML reports were
  checked against the selected source methods, with no missing, extra, failed, or skipped tests.
- AGP returned status 1 despite those passing reports, including an `AndroidTestResultListener`
  reporting exception in the commands run. The aggregate acceptance wrapper accepted its fresh
  complete XML through its existing verification policy; the focused runs used the same approach
  with exact selected-method checks. No dependency or harness policy was changed to mask this.
- The focused JVM command `:android:app:testDebugUnitTest --tests '*SnapshotFormatterTest'` and
  Python method selection for emulator location setup passed.

Final aggregate logs are under local `build/check-logs/`: static `20260907T192147Z-PPTHbg`,
acceptance `20260907T191144Z-HjiK6p`, and release check `20260907T191739Z-OT0Qnq`.
The commands and scenario mapping above are the portable review evidence; generated outputs and
private fixtures are not committed.
