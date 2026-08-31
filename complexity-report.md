# Phase-1 Complexity Hardening Report

Accepted starting SHA: `3fc2e678d10809a4dca8b28107710534160803ab`
Candidate measurement HEAD: `8c12beaf3d202be9e1cc6771f5c2ad23684a28b1`

## Named-function before/after summary

| Measure | Before | After |
|---|---:|---:|
| Named functions | 543 | 1200 |
| Average named-function CC | 5.703 | 3.841 |
| Median named-function CC | 4 | 3 |
| 90th percentile named-function CC | 13 | 7 |
| Maximum named-function CC | 61 | 13 |
| Named functions with CC > 5 | 171 | 275 |
| Named functions with CC > 7 | 115 | 116 |
| Named functions with CC > 10 | 71 | 1 |

The p90 is nearest-rank `ceil(0.90 * N)`. The named-function summary excludes PowerShell top-level script bodies; script bodies are measured and enforced separately. GitHub Actions run blocks are parsed as executable policy targets: only trivial wrappers remain inline. The historical Before snapshot contains only bodies present in that accepted historical scope, while After includes the complete current project-owned script-body scope.

## Script bodies before/after summary

| Measure | Before | After |
|---|---:|---:|
| Script bodies | 5 | 35 |
| Average Script bodies CC | 36.2 | 1.257 |
| Median Script bodies CC | 26 | 1 |
| 90th percentile Script bodies CC | 80 | 2 |
| Maximum Script bodies CC | 80 | 5 |
| Script bodies with CC > 5 | 4 | 0 |
| Script bodies with CC > 7 | 4 | 0 |
| Script bodies with CC > 10 | 3 | 0 |

## Enforced scopes before/after summary

| Measure | Before | After |
|---|---:|---:|
| Enforced scopes | 548 | 1235 |
| Average Enforced scopes CC | 5.982 | 3.768 |
| Median Enforced scopes CC | 4 | 3 |
| 90th percentile Enforced scopes CC | 13 | 7 |
| Maximum Enforced scopes CC | 80 | 13 |
| Enforced scopes with CC > 5 | 175 | 275 |
| Enforced scopes with CC > 7 | 119 | 116 |
| Enforced scopes with CC > 10 | 74 | 1 |

## GitHub Actions executable-block policy

Workflow YAML is parsed structurally. Trivial launcher/setup wrappers remain inline; substantial PowerShell must be extracted into measured `.ps1` code, and substantial unsupported shell/interpreter code fails closed.

Owned run blocks: 45; substantial inline PowerShell: 0; unsupported substantial blocks: 0

| Workflow | Job | Step | Shell | Lines | NLOC | Classification | Features |
|---|---|---|---|---:|---:|---|---|
| `.github/workflows/engine-complexity.yaml` | `complexity` | Install isolated complexity analyzer | `bash` | 22-25 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-complexity.yaml` | `complexity` | Install pinned workflow YAML parser | `pwsh` | 28-31 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-complexity.yaml` | `complexity` | Run complexity checker self-tests | `pwsh` | 36-39 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-complexity.yaml` | `complexity` | Enforce complexity budget | `pwsh` | 44-46 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task1.yaml` | `build-smoke-package-x64` | Verify headless runtime footprint | `pwsh` | 30-32 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task1.yaml` | `build-smoke-package-x64` | Smoke test obs-engine protocol v1 | `pwsh` | 37-39 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task1.yaml` | `build-smoke-package-x64` | Enforce smoke result | `pwsh` | 53-54 | 1 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task10.yaml` | `media-v1` | Verify normal package excludes Task 10 fixture and second APIs | `pwsh` | 31-33 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task10.yaml` | `media-v1` | Build CI-only deterministic media source | `pwsh` | 36-38 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task10.yaml` | `media-v1` | Stage CI-only media source | `pwsh` | 41-43 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task10.yaml` | `media-v1` | Run Task 10 media integration regression | `pwsh` | 46-49 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task10.yaml` | `media-v1` | Remove explicitly staged Task 10 test source | `pwsh` | 52-54 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task11.yaml` | `filter-v1` | Verify Task 10 core isolation and Task 11 callback ordering | `pwsh` | 26-28 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task11.yaml` | `filter-v1` | Verify normal package excludes fixtures and second APIs | `pwsh` | 37-39 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task11.yaml` | `filter-v1` | Build CI-only deterministic filter source | `pwsh` | 42-44 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task11.yaml` | `filter-v1` | Stage CI-only filter source | `pwsh` | 47-49 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task11.yaml` | `filter-v1` | Run Task 11 filter integration regression | `pwsh` | 52-55 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task11.yaml` | `filter-v1` | Run Task 11 timeout ownership race regression | `pwsh` | 58-61 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task11.yaml` | `filter-v1` | Remove explicitly staged Task 11 test source | `pwsh` | 65-67 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task2.yaml` | `build-smoke-package-x64` | Smoke test protocol v2 framing | `pwsh` | 33-35 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task2.yaml` | `build-smoke-package-x64` | Enforce Task 2 smoke result | `pwsh` | 49-50 | 1 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task3.yaml` | `build-smoke-package-x64` | Smoke test protocol v2 capabilities | `pwsh` | 33-35 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task3.yaml` | `build-smoke-package-x64` | Enforce Task 3 smoke result | `pwsh` | 49-50 | 1 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task4.yaml` | `build-smoke-package-x64` | Smoke test protocol v2 revisions | `pwsh` | 33-35 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task4.yaml` | `build-smoke-package-x64` | Enforce Task 4 smoke result | `pwsh` | 49-50 | 1 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task5.yaml` | `build-smoke-package-x64` | Build and run bounded event queue policy test | `pwsh` | 33-35 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task5.yaml` | `build-smoke-package-x64` | Smoke test protocol v2 subscriptions and event delivery | `pwsh` | 41-43 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task5.yaml` | `build-smoke-package-x64` | Enforce Task 5 verification result | `pwsh` | 57-58 | 1 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task6.yaml` | `build-smoke-package-x64` | Smoke test protocol v2 source scene item lifecycle | `pwsh` | 33-35 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task6.yaml` | `build-smoke-package-x64` | Enforce Task 6 smoke result | `pwsh` | 49-50 | 1 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task7.yaml` | `build-smoke-package-x64` | Build and run generic properties bridge test | `pwsh` | 33-35 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task7.yaml` | `build-smoke-package-x64` | Smoke test protocol v2 properties API | `pwsh` | 41-43 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task7.yaml` | `build-smoke-package-x64` | Enforce Task 7 test results | `pwsh` | 57-58 | 1 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task8-concurrency.yaml` | `source-event-capture-thread-isolation` | Compile and run capture routing regression | `pwsh` | 21-23 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task8-concurrency.yaml` | `source-event-capture-thread-isolation` | Verify production bridge ordering and isolation invariants | `pwsh` | 26-28 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task8-concurrency.yaml` | `deterministic-source-concurrency-a-f` | Build CI-only deterministic source module | `pwsh` | 47-49 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task8-concurrency.yaml` | `deterministic-source-concurrency-a-f` | Run deterministic A-F integration regression | `pwsh` | 54-56 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task8-concurrency.yaml` | `deterministic-source-concurrency-a-f` | Enforce deterministic A-F regression | `pwsh` | 70-71 | 1 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task8.yaml` | `build-smoke-package-x64` | Smoke test complete protocol v2 source namespace | `pwsh` | 33-35 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task8.yaml` | `build-smoke-package-x64` | Enforce Task 8 smoke result | `pwsh` | 49-50 | 1 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task9.yaml` | `interaction-v1` | Verify normal package excludes Task 9 test source | `pwsh` | 31-33 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task9.yaml` | `interaction-v1` | Build CI-only deterministic interaction source | `pwsh` | 36-38 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task9.yaml` | `interaction-v1` | Stage CI-only interaction source | `pwsh` | 41-43 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task9.yaml` | `interaction-v1` | Run Task 9 interaction integration regression | `pwsh` | 46-49 | 2 | TRIVIAL_WRAPPER | — |
| `.github/workflows/engine-protocol-v2-task9.yaml` | `interaction-v1` | Remove explicitly staged Task 9 test source | `pwsh` | 52-54 | 2 | TRIVIAL_WRAPPER | — |

## Named function-by-function comparison

| Function | File | Before CC | After CC | Before NLOC | After NLOC | Notes |
|---|---|---:|---:|---:|---:|---|
| `obs_source_destroy_defer` | `libobs/obs-source.c` | 13 | 13 | 65 | 65 | unchanged |
| `Assert-Task11LiveFilterReads` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 10 | — | 50 | new cohesive helper/function in scoped file |
| `Get-OperatorBlameLines` | `tools/check-complexity.ps1` | — | 10 | — | 41 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_scene_create` | `engine/runtime_v2.cpp` | 10 | 10 | 37 | 37 | unchanged |
| `obs_engine::Engine::v2_sync_source_observers` | `engine/runtime_source_v2.cpp` | 15 | 10 | 58 | 35 | reduced by 5 |
| `obs_engine::Engine::v2_source_duplicate` | `engine/runtime_source_v2.cpp` | 14 | 10 | 54 | 35 | reduced by 4 |
| `Get-WorkflowRunBlocks` | `tools/check-complexity.ps1` | — | 10 | — | 35 | new cohesive helper/function in scoped file |
| `obs_engine::collect_filter_signal` | `engine/runtime_filter_v2.cpp` | 22 | 10 | 81 | 34 | reduced by 12 |
| `obs_engine::Engine::v2_sync_filter_registry` | `engine/runtime_filter_v2.cpp` | — | 10 | — | 34 | new cohesive helper/function in scoped file |
| `obs_engine::take_deferred_source_events` | `engine/runtime_source_v2.cpp` | 10 | 10 | 34 | 34 | unchanged |
| `obs_engine::Engine::v2_item_set_transform` | `engine/runtime_v2.cpp` | 25 | 10 | 79 | 34 | reduced by 15 |
| `Get-NonBlankLineCount` | `tools/check-complexity.ps1` | — | 10 | — | 34 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_settle_filter_mutation` | `engine/runtime_filter_v2.cpp` | 16 | 10 | 36 | 30 | reduced by 6 |
| `initialize_source_mutexes` | `libobs/obs-source.c` | — | 10 | — | 30 | new cohesive helper/function in scoped file |
| `obs_source_update_internal` | `libobs/obs-source.c` | 15 | 10 | 42 | 30 | reduced by 5 |
| `Get-WorkflowPowerShellAstMetrics` | `tools/check-complexity.ps1` | — | 10 | — | 30 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_filter_set_order` | `engine/runtime_filter_v2.cpp` | 12 | 10 | 33 | 29 | reduced by 2 |
| `obs_engine::prepare_v2_request` | `engine/protocol_v2.cpp` | — | 10 | — | 27 | new cohesive helper/function in scoped file |
| `obs_engine::read_line_limited` | `engine/protocol.cpp` | 10 | 10 | 26 | 26 | unchanged |
| `Invoke-Task7LiveRead` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 10 | — | 24 | new cohesive helper/function in scoped file |
| `Invoke-Task5InitialSubscriptionChecks` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 10 | — | 23 | new cohesive helper/function in scoped file |
| `Invoke-Task6SettingsPatch` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 10 | — | 23 | new cohesive helper/function in scoped file |
| `obs_engine::read_mouse_button_input` | `engine/runtime_interaction_v2.cpp` | — | 10 | — | 18 | new cohesive helper/function in scoped file |
| `Get-PostHistoryRecreatedPaths` | `tools/check-complexity.ps1` | — | 10 | — | 18 | new cohesive helper/function in scoped file |
| `Assert-Task2Hello` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 10 | — | 17 | new cohesive helper/function in scoped file |
| `obs_engine::filter_settings_event_matches` | `engine/runtime_filter_v2.cpp` | 10 | 10 | 17 | 17 | unchanged |
| `Get-WorkflowInterpreter` | `tools/check-complexity.ps1` | — | 10 | — | 14 | new cohesive helper/function in scoped file |
| `Invoke-Task11FilterMutations` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 9 | — | 75 | new cohesive helper/function in scoped file |
| `Invoke-Task5EventQueuePolicy` | `.github/scripts/engine-protocol-v2-task5-event-queue-policy.ps1` | — | 9 | — | 56 | new cohesive helper/function in scoped file |
| `Invoke-Task7PropertiesBridge` | `.github/scripts/engine-protocol-v2-task7-properties-bridge.ps1` | — | 9 | — | 56 | new cohesive helper/function in scoped file |
| `Get-PathScopeState` | `tools/check-complexity.ps1` | — | 9 | — | 47 | new cohesive helper/function in scoped file |
| `New-AutomaticIdentity` | `tools/check-complexity.ps1` | — | 9 | — | 41 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_properties_invoke_button` | `engine/runtime_properties_v2.cpp` | 18 | 9 | 63 | 40 | reduced by 9 |
| `obs_engine::Engine::v2_filter_prepare_parent_removal` | `engine/runtime_filter_v2.cpp` | 9 | 9 | 36 | 36 | unchanged |
| `Get-UnmigratedIdentityViolations` | `tools/check-complexity.ps1` | — | 9 | — | 36 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_interaction_mouse_move` | `engine/runtime_interaction_v2.cpp` | 9 | 9 | 35 | 35 | unchanged |
| `process_media_action` | `libobs/obs-source.c` | — | 9 | — | 34 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::command_source_create` | `engine/runtime.cpp` | 9 | 9 | 32 | 32 | unchanged |
| `obs_source_media_action_enqueue` | `libobs/obs-source.c` | 20 | 9 | 64 | 32 | reduced by 11 |
| `Get-PathScopeLines` | `tools/check-complexity.ps1` | — | 9 | — | 32 | new cohesive helper/function in scoped file |
| `Invoke-Task6Transform` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 9 | — | 30 | new cohesive helper/function in scoped file |
| `obs_engine::handle_filter_request` | `engine/protocol_filter_v2.cpp` | 17 | 9 | 51 | 30 | reduced by 8 |
| `obs_engine::Engine::command_item_transform` | `engine/runtime.cpp` | 18 | 9 | 65 | 30 | reduced by 9 |
| `Update-PostAcceptedPathProvenance` | `tools/check-complexity.ps1` | — | 9 | — | 30 | new cohesive helper/function in scoped file |
| `Get-IdentityMigrationCurrent` | `tools/check-complexity.ps1` | — | 9 | — | 28 | new cohesive helper/function in scoped file |
| `obs_engine::publish_deferred_source_snapshot` | `engine/runtime_source_v2.cpp` | 9 | 9 | 27 | 27 | unchanged |
| `obs_engine::canonicalize_source_result` | `engine/runtime_source_settle_v2.cpp` | 9 | 9 | 26 | 26 | unchanged |
| `Read-IdentityMigrationEntries` | `tools/check-complexity.ps1` | — | 9 | — | 26 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_settle_source_mutation` | `engine/runtime_source_settle_v2.cpp` | 9 | 9 | 25 | 25 | unchanged |
| `Assert-TrustedBaselineScope` | `tools/check-complexity.ps1` | — | 9 | — | 25 | new cohesive helper/function in scoped file |
| `check_dynamic_properties` | `engine/properties_test.cpp` | — | 9 | — | 21 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_media_toggle_pause` | `engine/runtime_media_v2.cpp` | 14 | 9 | 33 | 21 | reduced by 5 |
| `obs_engine::strip_inline_list_items` | `engine/runtime_properties_v2.cpp` | 9 | 9 | 21 | 21 | unchanged |
| `Assert-TrustedBaselineReportShape` | `tools/check-complexity.ps1` | — | 9 | — | 19 | new cohesive helper/function in scoped file |
| `obs_engine::collect_sensitive_recursive` | `engine/properties_sensitive.cpp` | 9 | 9 | 17 | 17 | unchanged |
| `obs_engine::Engine::v2_read_source_create_options` | `engine/runtime_v2.cpp` | — | 9 | — | 17 | new cohesive helper/function in scoped file |
| `Invoke-Task8LoadState` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 9 | — | 16 | new cohesive helper/function in scoped file |
| `obs_engine::read_candidate_params` | `engine/runtime_properties_v2.cpp` | 9 | 9 | 15 | 15 | unchanged |
| `Assert-Task11Ordering` | `.github/scripts/engine-protocol-v2-task11-core-audit.ps1` | — | 9 | — | 14 | new cohesive helper/function in scoped file |
| `Invoke-Task8SourceRemoval` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 9 | — | 12 | new cohesive helper/function in scoped file |
| `obs_engine::parse_u32` | `engine/config.cpp` | 9 | 9 | 12 | 12 | unchanged |
| `Invoke-Task8SourceIdentity` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 9 | — | 10 | new cohesive helper/function in scoped file |
| `Initialize-Task10Sources` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 8 | — | 56 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_source_remove` | `engine/runtime_v2.cpp` | 9 | 8 | 40 | 39 | reduced by 1 |
| `main` | `engine/host.cpp` | 16 | 8 | 69 | 38 | reduced by 8 |
| `obs_engine::Engine::v2_properties_get_list_items` | `engine/runtime_properties_v2.cpp` | 8 | 8 | 37 | 37 | unchanged |
| `obs_engine::Engine::v2_source_get_missing_files` | `engine/runtime_source_v2.cpp` | 9 | 8 | 39 | 37 | reduced by 1 |
| `New-WorkflowRunBlockRecord` | `tools/check-complexity.ps1` | — | 8 | — | 36 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_filter_duplicate` | `engine/runtime_filter_v2.cpp` | 18 | 8 | 60 | 35 | reduced by 10 |
| `obs_engine::take_deferred_media_events` | `engine/runtime_media_v2.cpp` | 14 | 8 | 38 | 34 | reduced by 6 |
| `obs_engine::take_deferred_filter_events` | `engine/runtime_filter_v2.cpp` | 13 | 8 | 41 | 33 | reduced by 5 |
| `obs_engine::publish_media_events` | `engine/runtime_media_v2.cpp` | 31 | 8 | 82 | 33 | reduced by 23 |
| `obs_engine::Engine::v2_item_create` | `engine/runtime_v2.cpp` | 8 | 8 | 33 | 33 | unchanged |
| `Invoke-Task10PlayPause` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 8 | — | 32 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_apply_filter_settings` | `engine/runtime_filter_v2.cpp` | — | 8 | — | 32 | new cohesive helper/function in scoped file |
| `Invoke-CheckMode` | `tools/check-complexity.ps1` | — | 8 | — | 32 | new cohesive helper/function in scoped file |
| `obs_engine::serialize_property_list_items` | `engine/properties.cpp` | 8 | 8 | 31 | 31 | unchanged |
| `obs_engine::parse_subscription_list` | `engine/protocol_v2.cpp` | 12 | 8 | 38 | 29 | reduced by 4 |
| `Invoke-Task6SourceRemoval` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 8 | — | 28 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_add_filter_observer` | `engine/runtime_filter_v2.cpp` | — | 8 | — | 28 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::command_scene_create` | `engine/runtime.cpp` | 8 | 8 | 27 | 27 | unchanged |
| `Get-AstDecisionWeight` | `tools/check-complexity.ps1` | — | 8 | — | 27 | new cohesive helper/function in scoped file |
| `obs_engine::decode_utf8_scalars` | `engine/runtime_interaction_v2.cpp` | 21 | 8 | 40 | 26 | reduced by 13 |
| `Read-EngineMessage` | `.github/scripts/engine-protocol-v2-task11.ps1` | 8 | 8 | 25 | 25 | unchanged |
| `test_state_overflow_requires_resync` | `engine/events_test.cpp` | 8 | 8 | 25 | 25 | unchanged |
| `obs_engine::Engine::v2_filter_rename` | `engine/runtime_filter_v2.cpp` | 10 | 8 | 29 | 25 | reduced by 2 |
| `obs_engine::Engine::v2_collect_media_observer_changes` | `engine/runtime_media_v2.cpp` | — | 8 | — | 25 | new cohesive helper/function in scoped file |
| `New-OperatorCodeCommitRecords` | `tools/check-complexity.ps1` | — | 8 | — | 25 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_filter_register_source_filters` | `engine/runtime_filter_v2.cpp` | 13 | 8 | 41 | 24 | reduced by 5 |
| `obs_engine::publish_media_batch` | `engine/runtime_media_v2.cpp` | — | 8 | — | 24 | new cohesive helper/function in scoped file |
| `obs_engine::publish_source_events` | `engine/runtime_source_v2.cpp` | 16 | 8 | 50 | 24 | reduced by 8 |
| `obs_engine::Engine::v2_source_rename` | `engine/runtime_source_v2.cpp` | 9 | 8 | 25 | 23 | reduced by 1 |
| `Test-MetricInScope` | `tools/check-complexity.ps1` | — | 8 | — | 22 | new cohesive helper/function in scoped file |
| `obs_engine::decode_utf8_lead` | `engine/runtime_interaction_v2.cpp` | — | 8 | — | 19 | new cohesive helper/function in scoped file |
| `obs_engine::wait_for_media_action` | `engine/runtime_media_v2.cpp` | — | 8 | — | 19 | new cohesive helper/function in scoped file |
| `Invoke-Task7LiveRefresh` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 8 | — | 17 | new cohesive helper/function in scoped file |
| `check_frame_rate_schema` | `engine/properties_test.cpp` | — | 8 | — | 17 | new cohesive helper/function in scoped file |
| `Stop-Task11AfterFailure` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 8 | — | 15 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_prepare_filter_settlement` | `engine/runtime_filter_v2.cpp` | — | 8 | — | 15 | new cohesive helper/function in scoped file |
| `Invoke-Task8Rename` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 8 | — | 14 | new cohesive helper/function in scoped file |
| `obs_engine::parse_handle_text` | `engine/runtime_filter_v2.cpp` | 8 | 8 | 14 | 14 | unchanged |
| `obs_engine::parse_handle_text` | `engine/runtime_interaction_v2.cpp` | 8 | 8 | 14 | 14 | unchanged |
| `obs_engine::parse_handle_text` | `engine/runtime_media_v2.cpp` | 8 | 8 | 14 | 14 | unchanged |
| `obs_engine::parse_handle_text` | `engine/runtime_properties_v2.cpp` | 8 | 8 | 14 | 14 | unchanged |
| `obs_engine::parse_handle_text` | `engine/runtime_source_settle_v2.cpp` | 8 | 8 | 14 | 14 | unchanged |
| `obs_engine::parse_handle_text` | `engine/runtime_source_v2.cpp` | 8 | 8 | 14 | 14 | unchanged |
| `obs_engine::parse_handle_text` | `engine/runtime_v2.cpp` | 8 | 8 | 14 | 14 | unchanged |
| `obs_engine::read_mouse_wheel_input` | `engine/runtime_interaction_v2.cpp` | — | 8 | — | 13 | new cohesive helper/function in scoped file |
| `Stop-Task10AfterFailure` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 8 | — | 12 | new cohesive helper/function in scoped file |
| `Assert-Task8BridgePreRequestOrdering` | `.github/scripts/engine-protocol-v2-task8-concurrency-bridge-audit.ps1` | — | 8 | — | 12 | new cohesive helper/function in scoped file |
| `Assert-Task8BridgePostRequestOrdering` | `.github/scripts/engine-protocol-v2-task8-concurrency-bridge-audit.ps1` | — | 8 | — | 12 | new cohesive helper/function in scoped file |
| `Stop-Task8AfterFailure` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | — | 8 | — | 12 | new cohesive helper/function in scoped file |
| `check_telemetry_output` | `engine/events_test.cpp` | — | 8 | — | 12 | new cohesive helper/function in scoped file |
| `obs_engine::is_valid_utf8_scalar` | `engine/runtime_interaction_v2.cpp` | — | 8 | — | 12 | new cohesive helper/function in scoped file |
| `Assert-Task3Hello` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 8 | — | 11 | new cohesive helper/function in scoped file |
| `obs_engine::batch_matches_filter_update` | `engine/runtime_filter_v2.cpp` | 8 | 8 | 10 | 10 | unchanged |
| `Invoke-Task10ToggleAndSeek` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 7 | — | 49 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::handle` | `engine/runtime.cpp` | 18 | 7 | 46 | 44 | reduced by 11 |
| `New-PowerShellScriptBodyMetric` | `tools/check-complexity.ps1` | — | 7 | — | 43 | new cohesive helper/function in scoped file |
| `Invoke-Task11SourceDuplicate` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 7 | — | 40 | new cohesive helper/function in scoped file |
| `New-ScopeContext` | `tools/check-complexity.ps1` | — | 7 | — | 40 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_prepare_filter_shutdown` | `engine/runtime_filter_v2.cpp` | 7 | 7 | 36 | 36 | unchanged |
| `dispatch_request` | `engine/host.cpp` | 7 | 7 | 35 | 35 | unchanged |
| `Invoke-Task6SourceCreation` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 7 | — | 33 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_scene_remove` | `engine/runtime_v2.cpp` | 10 | 7 | 43 | 33 | reduced by 3 |
| `obs_engine::Engine::command_scene_add` | `engine/runtime.cpp` | 7 | 7 | 33 | 33 | unchanged |
| `obs_engine::EventDispatcher::subscribe` | `engine/events.cpp` | 7 | 7 | 31 | 31 | unchanged |
| `obs_engine::settle_deferred_source_update` | `engine/runtime_source_settle_v2.cpp` | 7 | 7 | 31 | 31 | unchanged |
| `Merge-IdentityMigrationMaps` | `tools/check-complexity.ps1` | — | 7 | — | 31 | new cohesive helper/function in scoped file |
| `Invoke-WorkflowExecutableAudit` | `tools/check-complexity.ps1` | — | 7 | — | 30 | new cohesive helper/function in scoped file |
| `obs_engine::queue_deferred_media_events_locked` | `engine/runtime_media_v2.cpp` | — | 7 | — | 28 | new cohesive helper/function in scoped file |
| `Find-BeforeMetric` | `tools/check-complexity.ps1` | — | 7 | — | 28 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_filter_set_enabled` | `engine/runtime_filter_v2.cpp` | 7 | 7 | 27 | 27 | unchanged |
| `obs_engine::Engine::v2_end_event_capture` | `engine/runtime_source_v2.cpp` | 7 | 7 | 26 | 26 | unchanged |
| `obs_engine::apply_transform_vector` | `engine/runtime_v2.cpp` | — | 7 | — | 26 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::prepare_startup_environment` | `engine/runtime.cpp` | — | 7 | — | 26 | new cohesive helper/function in scoped file |
| `obs_engine::handle_session_method` | `engine/protocol_v2.cpp` | — | 7 | — | 24 | new cohesive helper/function in scoped file |
| `obs_engine::mark_filter_settlement_lost` | `engine/runtime_filter_v2.cpp` | 7 | 7 | 24 | 24 | unchanged |
| `obs_engine::Engine::v2_end_filter_event_capture` | `engine/runtime_filter_v2.cpp` | 7 | 7 | 24 | 24 | unchanged |
| `obs_engine::Engine::v2_end_media_event_capture` | `engine/runtime_media_v2.cpp` | 7 | 7 | 24 | 24 | unchanged |
| `obs_engine::EventDispatcher::run` | `engine/events.cpp` | 11 | 7 | 35 | 23 | reduced by 4 |
| `run_protocol_loop` | `engine/host.cpp` | — | 7 | — | 23 | new cohesive helper/function in scoped file |
| `obs_engine::wait_for_filter_update` | `engine/runtime_filter_v2.cpp` | — | 7 | — | 23 | new cohesive helper/function in scoped file |
| `obs_source_init` | `libobs/obs-source.c` | 15 | 7 | 47 | 23 | reduced by 8 |
| `obs_engine::Engine::load_runtime_modules` | `engine/runtime.cpp` | — | 7 | — | 22 | new cohesive helper/function in scoped file |
| `New-InclusionBaselineMap` | `tools/check-complexity.ps1` | — | 7 | — | 22 | new cohesive helper/function in scoped file |
| `obs_engine::collect_media_signal` | `engine/runtime_media_v2.cpp` | 24 | 7 | 57 | 21 | reduced by 17 |
| `obs_engine::collect_source_signal` | `engine/runtime_source_v2.cpp` | 23 | 7 | 66 | 21 | reduced by 16 |
| `Get-WorkingTreeRecreatedPaths` | `tools/check-complexity.ps1` | — | 7 | — | 21 | new cohesive helper/function in scoped file |
| `Get-NonCyclomaticCategory` | `tools/check-complexity.ps1` | — | 7 | — | 21 | new cohesive helper/function in scoped file |
| `obs_engine::resolve_property_schema` | `engine/properties.cpp` | 7 | 7 | 20 | 20 | unchanged |
| `obs_engine::route_filter_payload_locked` | `engine/runtime_filter_v2.cpp` | — | 7 | — | 20 | new cohesive helper/function in scoped file |
| `obs_engine::promote_deferred_filter_update` | `engine/runtime_filter_v2.cpp` | 7 | 7 | 20 | 20 | unchanged |
| `obs_engine::parse_modifiers` | `engine/runtime_interaction_v2.cpp` | 7 | 7 | 19 | 19 | unchanged |
| `obs_engine::normalize_kind_entry` | `engine/runtime_source_settle_v2.cpp` | 7 | 7 | 19 | 19 | unchanged |
| `media_action_callback_available` | `libobs/obs-source.c` | — | 7 | — | 19 | new cohesive helper/function in scoped file |
| `obs_engine::is_event_name` | `engine/events.cpp` | 14 | 7 | 20 | 18 | reduced by 7 |
| `obs_engine::read_filter_settings_input` | `engine/runtime_filter_v2.cpp` | — | 7 | — | 18 | new cohesive helper/function in scoped file |
| `obs_engine::resolve_uncertain_filter_updates_locked` | `engine/runtime_filter_v2.cpp` | 7 | 7 | 18 | 18 | unchanged |
| `obs_engine::promote_deferred_media_action_locked` | `engine/runtime_media_v2.cpp` | 7 | 7 | 18 | 18 | unchanged |
| `Read-Event` | `.github/scripts/engine-protocol-v2-task10.ps1` | 11 | 7 | 30 | 17 | reduced by 4 |
| `Read-StateEvent` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 7 | 7 | 17 | 17 | unchanged |
| `obs_engine::validate_property_patch` | `engine/properties.cpp` | 7 | 7 | 17 | 17 | unchanged |
| `Invoke-Task4StaleClose` | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | — | 7 | — | 16 | new cohesive helper/function in scoped file |
| `Get-CeilingMetricViolations` | `tools/check-complexity.ps1` | — | 7 | — | 16 | new cohesive helper/function in scoped file |
| `check_list_schema` | `engine/properties_test.cpp` | — | 7 | — | 15 | new cohesive helper/function in scoped file |
| `obs_engine::batch_matches_source_update` | `engine/runtime_source_settle_v2.cpp` | 7 | 7 | 15 | 15 | unchanged |
| `Invoke-Task8Duplicate` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 7 | — | 13 | new cohesive helper/function in scoped file |
| `obs_engine::parse_v2_request_options` | `engine/protocol_v2.cpp` | — | 7 | — | 13 | new cohesive helper/function in scoped file |
| `obs_engine::read_text_input` | `engine/runtime_interaction_v2.cpp` | — | 7 | — | 12 | new cohesive helper/function in scoped file |
| `Assert-Task7SchemaProperties` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 7 | — | 11 | new cohesive helper/function in scoped file |
| `Assert-Task5StoppingEvent` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 7 | — | 10 | new cohesive helper/function in scoped file |
| `Invoke-Task8ReplaceSettings` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 7 | — | 10 | new cohesive helper/function in scoped file |
| `check_eviction_output` | `engine/events_test.cpp` | — | 7 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::read_key_input` | `engine/runtime_interaction_v2.cpp` | — | 7 | — | 9 | new cohesive helper/function in scoped file |
| `Invoke-Task8SaveState` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 7 | — | 7 | new cohesive helper/function in scoped file |
| `Invoke-Task8CaptureRouting` | `.github/scripts/engine-protocol-v2-task8-concurrency-capture-routing.ps1` | — | 6 | — | 59 | new cohesive helper/function in scoped file |
| `New-HistoryContext` | `tools/check-complexity.ps1` | — | 6 | — | 49 | new cohesive helper/function in scoped file |
| `Start-EngineCase` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 6 | 6 | 44 | 44 | unchanged |
| `Get-PowerShellFileMetrics` | `tools/check-complexity.ps1` | — | 6 | — | 40 | new cohesive helper/function in scoped file |
| `Invoke-Task10RestartAndStop` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 6 | — | 37 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_build_property_target` | `engine/runtime_properties_v2.cpp` | 27 | 6 | 86 | 37 | reduced by 21 |
| `obs_engine::Engine::v2_source_create` | `engine/runtime_v2.cpp` | 13 | 6 | 47 | 37 | reduced by 7 |
| `Write-Task1Diagnostics` | `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1` | — | 6 | — | 34 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_properties_validate` | `engine/runtime_properties_v2.cpp` | 6 | 6 | 33 | 33 | unchanged |
| `Get-LizardRows` | `tools/check-complexity.ps1` | — | 6 | — | 32 | new cohesive helper/function in scoped file |
| `obs_engine::EventDispatcher::emit` | `engine/events.cpp` | 6 | 6 | 31 | 31 | unchanged |
| `obs_engine::Engine::v2_filter_list` | `engine/runtime_filter_v2.cpp` | 7 | 6 | 33 | 31 | reduced by 1 |
| `obs_engine::Engine::v2_prepare_media_shutdown` | `engine/runtime_media_v2.cpp` | 6 | 6 | 31 | 31 | unchanged |
| `obs_engine::Engine::v2_prepare_shutdown` | `engine/runtime_source_v2.cpp` | 6 | 6 | 31 | 31 | unchanged |
| `Invoke-RaceNewerRequest` | `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | — | 6 | — | 30 | new cohesive helper/function in scoped file |
| `Initialize-Task11FilterGraph` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 6 | — | 30 | new cohesive helper/function in scoped file |
| `obs_engine::publish_filter_events` | `engine/runtime_filter_v2.cpp` | 34 | 6 | 106 | 30 | reduced by 28 |
| `obs_engine::Engine::v2_media_set_position` | `engine/runtime_media_v2.cpp` | 13 | 6 | 39 | 29 | reduced by 7 |
| `obs_engine::Engine::v2_source_load_state` | `engine/runtime_source_v2.cpp` | 21 | 6 | 48 | 29 | reduced by 15 |
| `obs_engine::Engine::v2_add_source_observer` | `engine/runtime_source_v2.cpp` | — | 6 | — | 28 | new cohesive helper/function in scoped file |
| `Invoke-Task7PropertiesSmoke` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 6 | — | 27 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_filter_kind_list` | `engine/runtime_filter_v2.cpp` | 6 | 6 | 27 | 27 | unchanged |
| `obs_engine::Engine::v2_filter_create` | `engine/runtime_filter_v2.cpp` | 21 | 6 | 64 | 27 | reduced by 15 |
| `Invoke-RaceAction` | `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | — | 6 | — | 26 | new cohesive helper/function in scoped file |
| `obs_engine::handle_v2_request` | `engine/protocol_v2.cpp` | 39 | 6 | 138 | 26 | reduced by 33 |
| `obs_engine::Engine::v2_add_media_observer` | `engine/runtime_media_v2.cpp` | — | 6 | — | 26 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_source_kind_list` | `engine/runtime_v2.cpp` | 6 | 6 | 26 | 26 | unchanged |
| `Complete-Task6Session` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 6 | — | 25 | new cohesive helper/function in scoped file |
| `Invoke-Task6RuntimeSmoke` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 6 | — | 25 | new cohesive helper/function in scoped file |
| `obs_engine::handle_runtime_method` | `engine/protocol_v2.cpp` | — | 6 | — | 25 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::command_source_types` | `engine/runtime.cpp` | — | 6 | — | 25 | new cohesive helper/function in scoped file |
| `obs_engine::parse_args` | `engine/config.cpp` | 15 | 6 | 48 | 24 | reduced by 9 |
| `test_ordered_resync_preserves_queued_event` | `engine/events_test.cpp` | 6 | 6 | 24 | 24 | unchanged |
| `obs_engine::Engine::v2_filter_order_data` | `engine/runtime_filter_v2.cpp` | 6 | 6 | 24 | 24 | unchanged |
| `Validate-Allowlist` | `tools/check-complexity.ps1` | — | 6 | — | 24 | new cohesive helper/function in scoped file |
| `Run-CaseE` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 6 | 6 | 23 | 23 | unchanged |
| `Complete-Task5Session` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 6 | — | 22 | new cohesive helper/function in scoped file |
| `test_telemetry_policy` | `engine/events_test.cpp` | 18 | 6 | 40 | 22 | reduced by 12 |
| `obs_engine::Engine::v2_register_attached_filter` | `engine/runtime_filter_v2.cpp` | — | 6 | — | 22 | new cohesive helper/function in scoped file |
| `obs_source_deferred_update` | `libobs/obs-source.c` | 6 | 6 | 22 | 22 | unchanged |
| `Get-HistoricalPathAliases` | `tools/check-complexity.ps1` | — | 6 | — | 22 | new cohesive helper/function in scoped file |
| `Invoke-Task6ItemCreation` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 6 | — | 21 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_build_source_kind_property_target` | `engine/runtime_properties_v2.cpp` | — | 6 | — | 21 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_build_filter_kind_property_target` | `engine/runtime_properties_v2.cpp` | — | 6 | — | 21 | new cohesive helper/function in scoped file |
| `obs_engine::read_settings_json` | `engine/runtime_source_settle_v2.cpp` | 6 | 6 | 21 | 21 | unchanged |
| `obs_engine::Engine::v2_normalize_source_kind_metadata` | `engine/runtime_source_settle_v2.cpp` | 6 | 6 | 21 | 21 | unchanged |
| `Get-WorkflowOperatorLines` | `tools/check-complexity.ps1` | — | 6 | — | 21 | new cohesive helper/function in scoped file |
| `obs_engine::read_finite_double` | `engine/protocol.cpp` | 6 | 6 | 20 | 20 | unchanged |
| `obs_engine::Engine::v2_register_filter` | `engine/runtime_filter_v2.cpp` | — | 6 | — | 20 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_prepare_property_button` | `engine/runtime_properties_v2.cpp` | — | 6 | — | 20 | new cohesive helper/function in scoped file |
| `Assert-JsonUniqueProperties` | `tools/check-complexity.ps1` | — | 6 | — | 20 | new cohesive helper/function in scoped file |
| `obs_engine::collect_filter_ref` | `engine/runtime_filter_v2.cpp` | 6 | 6 | 19 | 19 | unchanged |
| `obs_engine::collect_filter_state_events` | `engine/runtime_filter_v2.cpp` | — | 6 | — | 19 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_source_kind_defaults` | `engine/runtime_v2.cpp` | 6 | 6 | 19 | 19 | unchanged |
| `obs_engine::apply_legacy_transform_alignment` | `engine/runtime.cpp` | — | 6 | — | 19 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_sync_media_observers` | `engine/runtime_media_v2.cpp` | 17 | 6 | 59 | 18 | reduced by 11 |
| `Get-PathLanguage` | `tools/check-complexity.ps1` | — | 6 | — | 18 | new cohesive helper/function in scoped file |
| `Assert-IdentityMigrationFunctionIdentity` | `tools/check-complexity.ps1` | — | 6 | — | 18 | new cohesive helper/function in scoped file |
| `Assert-Task11FilterObserver` | `.github/scripts/engine-protocol-v2-task11-core-audit.ps1` | — | 6 | — | 17 | new cohesive helper/function in scoped file |
| `Assert-Task3HelloCapabilities` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 6 | — | 17 | new cohesive helper/function in scoped file |
| `Complete-Task4Session` | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | — | 6 | — | 17 | new cohesive helper/function in scoped file |
| `Read-Task6StateEvent` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 6 | — | 17 | new cohesive helper/function in scoped file |
| `obs_engine::apply_transform_alignment` | `engine/runtime_v2.cpp` | — | 6 | — | 17 | new cohesive helper/function in scoped file |
| `Get-WorkflowLexicalFeatures` | `tools/check-complexity.ps1` | — | 6 | — | 17 | new cohesive helper/function in scoped file |
| `Invoke-Task7ListItems` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 6 | — | 16 | new cohesive helper/function in scoped file |
| `Invoke-Task8SourceSmoke` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 6 | — | 16 | new cohesive helper/function in scoped file |
| `obs_engine::read_subscription_entry` | `engine/protocol_v2.cpp` | — | 6 | — | 16 | new cohesive helper/function in scoped file |
| `register_capture_sources` | `plugins/win-capture/plugin-main.c` | — | 6 | — | 16 | new cohesive helper/function in scoped file |
| `Get-ReportStatistics` | `tools/check-complexity.ps1` | — | 6 | — | 16 | new cohesive helper/function in scoped file |
| `setup_telemetry_policy` | `engine/events_test.cpp` | — | 6 | — | 15 | new cohesive helper/function in scoped file |
| `obs_engine::FilterCallbackScope::FilterCallbackScope` | `engine/runtime_filter_v2.cpp` | 6 | 6 | 15 | 15 | unchanged |
| `obs_engine::MediaCallbackScope::MediaCallbackScope` | `engine/runtime_media_v2.cpp` | 6 | 6 | 15 | 15 | unchanged |
| `obs_engine::collect_media_state_events` | `engine/runtime_media_v2.cpp` | — | 6 | — | 15 | new cohesive helper/function in scoped file |
| `obs_engine::SourceCallbackScope::SourceCallbackScope` | `engine/runtime_source_v2.cpp` | 6 | 6 | 15 | 15 | unchanged |
| `Invoke-Task7Validate` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 6 | — | 14 | new cohesive helper/function in scoped file |
| `obs_engine::read_key_text` | `engine/runtime_interaction_v2.cpp` | — | 6 | — | 14 | new cohesive helper/function in scoped file |
| `obs_engine::erase_password_settings` | `engine/properties.cpp` | 6 | 6 | 13 | 13 | unchanged |
| `obs_engine::is_safe_identifier` | `engine/validation.hpp` | 14 | 6 | 15 | 13 | reduced by 8 |
| `Stop-Task10Engine` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 6 | — | 12 | new cohesive helper/function in scoped file |
| `Stop-Task11Engine` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 6 | — | 12 | new cohesive helper/function in scoped file |
| `Stop-Task9Engine` | `.github/scripts/engine-protocol-v2-task9.ps1` | — | 6 | — | 12 | new cohesive helper/function in scoped file |
| `obs_engine::route_filter_uncertainty_locked` | `engine/runtime_filter_v2.cpp` | — | 6 | — | 12 | new cohesive helper/function in scoped file |
| `obs_engine::route_queued_media_event_locked` | `engine/runtime_media_v2.cpp` | — | 6 | — | 12 | new cohesive helper/function in scoped file |
| `obs_engine::read_media_position` | `engine/runtime_media_v2.cpp` | — | 6 | — | 12 | new cohesive helper/function in scoped file |
| `obs_engine::find_unversioned_input_id` | `engine/runtime_source_settle_v2.cpp` | 6 | 6 | 12 | 12 | unchanged |
| `check_pattern_cases` | `engine/events_test.cpp` | — | 6 | — | 11 | new cohesive helper/function in scoped file |
| `obs_engine::request_handle` | `engine/protocol.cpp` | 6 | 6 | 11 | 11 | unchanged |
| `obs_engine::read_source_state_input` | `engine/runtime_source_v2.cpp` | — | 6 | — | 11 | new cohesive helper/function in scoped file |
| `obs_engine::prepare_runtime_result` | `engine/protocol_v2.cpp` | — | 6 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::read_property_target` | `engine/runtime_properties_v2.cpp` | — | 6 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::validate_button_target` | `engine/runtime_properties_v2.cpp` | — | 6 | — | 10 | new cohesive helper/function in scoped file |
| `main` | `engine/events_test.cpp` | 6 | 6 | 9 | 9 | unchanged |
| `check_default_button` | `engine/properties_test.cpp` | — | 6 | — | 9 | new cohesive helper/function in scoped file |
| `check_url_button` | `engine/properties_test.cpp` | — | 6 | — | 8 | new cohesive helper/function in scoped file |
| `obs_engine::point_inside_source` | `engine/runtime_interaction_v2.cpp` | 6 | 6 | 8 | 8 | unchanged |
| `Invoke-Task8KindGet` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 6 | — | 7 | new cohesive helper/function in scoped file |
| `Invoke-Task8ActivityState` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 6 | — | 7 | new cohesive helper/function in scoped file |
| `Invoke-Task8LiveProperties` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 6 | — | 6 | new cohesive helper/function in scoped file |
| `obs_engine::key_matches` | `engine/runtime_interaction_v2.cpp` | 6 | 6 | 6 | 6 | unchanged |
| `Invoke-Task9PruneScenario` | `.github/scripts/engine-protocol-v2-task9.ps1` | — | 5 | — | 51 | new cohesive helper/function in scoped file |
| `New-InventoryMarkdown` | `tools/check-complexity.ps1` | — | 5 | — | 46 | new cohesive helper/function in scoped file |
| `Invoke-Task10SourceTransitions` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 5 | — | 38 | new cohesive helper/function in scoped file |
| `Invoke-Task11DuplicateAndRemove` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 5 | — | 36 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_interaction_reset` | `engine/runtime_interaction_v2.cpp` | 10 | 5 | 62 | 35 | reduced by 5 |
| `Write-Task6Diagnostics` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 5 | — | 30 | new cohesive helper/function in scoped file |
| `New-IdentityMigrationEntry` | `tools/check-complexity.ps1` | — | 5 | — | 30 | new cohesive helper/function in scoped file |
| `New-AutomaticIdentityMaps` | `tools/check-complexity.ps1` | — | 5 | — | 29 | new cohesive helper/function in scoped file |
| `Add-WorkflowExecutableSection` | `tools/check-complexity.ps1` | — | 5 | — | 28 | new cohesive helper/function in scoped file |
| `New-HistoricalPathContext` | `tools/check-complexity.ps1` | — | 5 | — | 28 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_interaction_mouse_button` | `engine/runtime_interaction_v2.cpp` | 22 | 5 | 60 | 27 | reduced by 17 |
| `Assert-Task6Capabilities` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 5 | — | 26 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_source_patch_settings` | `engine/runtime_v2.cpp` | 6 | 5 | 28 | 26 | reduced by 1 |
| `Invoke-Task11SafeAfterLate` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 5 | — | 25 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_properties_resolve` | `engine/runtime_properties_v2.cpp` | 5 | 5 | 25 | 25 | unchanged |
| `New-InventoryFileRecords` | `tools/check-complexity.ps1` | — | 5 | — | 24 | new cohesive helper/function in scoped file |
| `Get-RelatedBaselineMetrics` | `tools/check-complexity.ps1` | — | 5 | — | 23 | new cohesive helper/function in scoped file |
| `Invoke-Task1Footprint` | `.github/scripts/engine-protocol-v2-task1-footprint.ps1` | — | 5 | — | 22 | new cohesive helper/function in scoped file |
| `Write-Task2Diagnostics` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 5 | — | 22 | new cohesive helper/function in scoped file |
| `Write-Task3Diagnostics` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 5 | — | 22 | new cohesive helper/function in scoped file |
| `Write-Task4Diagnostics` | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | — | 5 | — | 22 | new cohesive helper/function in scoped file |
| `Write-Task5Diagnostics` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 5 | — | 22 | new cohesive helper/function in scoped file |
| `Write-Task7Diagnostics` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 5 | — | 22 | new cohesive helper/function in scoped file |
| `obs_engine::disconnect_filter_observer` | `engine/runtime_filter_v2.cpp` | 5 | 5 | 22 | 22 | unchanged |
| `obs_engine::Engine::v2_move_filter` | `engine/runtime_filter_v2.cpp` | — | 5 | — | 22 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_source_replace_settings` | `engine/runtime_source_v2.cpp` | 6 | 5 | 24 | 22 | reduced by 1 |
| `Initialize-Task10Session` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 5 | — | 21 | new cohesive helper/function in scoped file |
| `test_patterns_and_overlap` | `engine/events_test.cpp` | 13 | 5 | 30 | 21 | reduced by 8 |
| `test_state_prefers_telemetry_eviction` | `engine/events_test.cpp` | 11 | 5 | 27 | 21 | reduced by 6 |
| `obs_engine::queue_deferred_filter_events_locked` | `engine/runtime_filter_v2.cpp` | — | 5 | — | 21 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_create_filter_object` | `engine/runtime_filter_v2.cpp` | — | 5 | — | 21 | new cohesive helper/function in scoped file |
| `Assert-IdentityMigrationUniqueName` | `tools/check-complexity.ps1` | — | 5 | — | 21 | new cohesive helper/function in scoped file |
| `Test-BaselineIdentityPresent` | `tools/check-complexity.ps1` | — | 5 | — | 21 | new cohesive helper/function in scoped file |
| `obs_engine::publish_filter_batch` | `engine/runtime_filter_v2.cpp` | — | 5 | — | 20 | new cohesive helper/function in scoped file |
| `Get-ReportFunctionMetric` | `tools/check-complexity.tests.ps1` | — | 5 | — | 20 | new cohesive helper/function in scoped file |
| `Assert-Task8BridgeMarkers` | `.github/scripts/engine-protocol-v2-task8-concurrency-bridge-audit.ps1` | — | 5 | — | 19 | new cohesive helper/function in scoped file |
| `obs_engine::ProtocolWriter::run` | `engine/protocol.cpp` | 5 | 5 | 19 | 19 | unchanged |
| `obs_engine::update_key_tracking` | `engine/runtime_interaction_v2.cpp` | — | 5 | — | 19 | new cohesive helper/function in scoped file |
| `obs_engine::make_kind_metadata` | `engine/runtime_source_v2.cpp` | 5 | 5 | 19 | 19 | unchanged |
| `obs_engine::Engine::shutdown` | `engine/runtime.cpp` | 5 | 5 | 19 | 19 | unchanged |
| `Assert-CheckerFailure` | `tools/check-complexity.tests.ps1` | — | 5 | — | 19 | new cohesive helper/function in scoped file |
| `Send-V2Request` | `.github/scripts/engine-protocol-v2-task11.ps1` | 5 | 5 | 18 | 18 | unchanged |
| `obs_engine::Engine::v2_get_interaction_source` | `engine/runtime_interaction_v2.cpp` | 5 | 5 | 18 | 18 | unchanged |
| `obs_engine::promote_deferred_source_update` | `engine/runtime_source_settle_v2.cpp` | 5 | 5 | 18 | 18 | unchanged |
| `Get-CandidateChangedLines` | `tools/check-complexity.ps1` | — | 5 | — | 18 | new cohesive helper/function in scoped file |
| `Get-ExactException` | `tools/check-complexity.ps1` | — | 5 | — | 18 | new cohesive helper/function in scoped file |
| `New-CurrentPathSet` | `tools/check-complexity.ps1` | — | 5 | — | 18 | new cohesive helper/function in scoped file |
| `Send-V2Request` | `.github/scripts/engine-protocol-v2-task10.ps1` | 5 | 5 | 17 | 17 | unchanged |
| `Stop-RaceEngine` | `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | 5 | 5 | 17 | 17 | unchanged |
| `Send-V2Request` | `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | 5 | 5 | 17 | 17 | unchanged |
| `Complete-Task7Session` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 5 | — | 17 | new cohesive helper/function in scoped file |
| `obs_engine::EventDispatcher::enqueue_telemetry_locked` | `engine/events.cpp` | — | 5 | — | 17 | new cohesive helper/function in scoped file |
| `obs_engine::EventDispatcher::wait_for_next_event` | `engine/events.cpp` | — | 5 | — | 17 | new cohesive helper/function in scoped file |
| `obs_engine::ProtocolWriter::enqueue` | `engine/protocol.cpp` | 5 | 5 | 17 | 17 | unchanged |
| `obs_engine::Engine::v2_remove_unattached_filters` | `engine/runtime_filter_v2.cpp` | — | 5 | — | 17 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_source_kind_properties` | `engine/runtime_source_v2.cpp` | 5 | 5 | 17 | 17 | unchanged |
| `Send-Task1EngineRequest` | `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1` | — | 5 | — | 16 | new cohesive helper/function in scoped file |
| `Invoke-Task6Bootstrap` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 5 | — | 16 | new cohesive helper/function in scoped file |
| `Write-Task8Diagnostics` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 5 | — | 16 | new cohesive helper/function in scoped file |
| `obs_engine::settle_deferred_filter_update` | `engine/runtime_filter_v2.cpp` | 18 | 5 | 50 | 16 | reduced by 13 |
| `obs_engine::settle_media_action` | `engine/runtime_media_v2.cpp` | 17 | 5 | 53 | 16 | reduced by 12 |
| `obs_engine::read_source_handle` | `engine/runtime_source_settle_v2.cpp` | 5 | 5 | 16 | 16 | unchanged |
| `Read-Until-Resync` | `.github/scripts/engine-protocol-v2-task11.ps1` | 8 | 5 | 26 | 15 | reduced by 3 |
| `log_level_name` | `engine/host.cpp` | 5 | 5 | 15 | 15 | unchanged |
| `pin_working_directory_to_executable` | `engine/host.cpp` | 5 | 5 | 15 | 15 | unchanged |
| `obs_engine::combo_format_name` | `engine/properties.cpp` | 5 | 5 | 15 | 15 | unchanged |
| `obs_engine::serialize_text_property` | `engine/properties.cpp` | — | 5 | — | 15 | new cohesive helper/function in scoped file |
| `obs_engine::list_value_type_matches` | `engine/properties.cpp` | — | 5 | — | 15 | new cohesive helper/function in scoped file |
| `obs_engine::invoke_property_button` | `engine/properties.cpp` | 5 | 5 | 15 | 15 | unchanged |
| `obs_engine::update_filter_uncertainty_locked` | `engine/runtime_filter_v2.cpp` | — | 5 | — | 15 | new cohesive helper/function in scoped file |
| `obs_engine::read_u32_optional` | `engine/runtime_interaction_v2.cpp` | 5 | 5 | 15 | 15 | unchanged |
| `Get-YamlMappingValue` | `tools/check-complexity.ps1` | — | 5 | — | 15 | new cohesive helper/function in scoped file |
| `Read-Until-Resync` | `.github/scripts/engine-protocol-v2-task10.ps1` | 8 | 5 | 25 | 14 | reduced by 3 |
| `Assert-Task5Hello` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 5 | — | 14 | new cohesive helper/function in scoped file |
| `Read-Task8StateEvent` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 5 | — | 14 | new cohesive helper/function in scoped file |
| `obs_engine::list_value_is_enabled` | `engine/properties.cpp` | 12 | 5 | 32 | 14 | reduced by 7 |
| `obs_engine::read_click_count` | `engine/runtime_interaction_v2.cpp` | — | 5 | — | 14 | new cohesive helper/function in scoped file |
| `Read-Until-Resync` | `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | 5 | 5 | 13 | 13 | unchanged |
| `Invoke-Task6StalePatch` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 5 | — | 13 | new cohesive helper/function in scoped file |
| `obs_engine::remember_uncertain_filter_update_locked` | `engine/runtime_filter_v2.cpp` | 5 | 5 | 13 | 13 | unchanged |
| `obs_engine::capture_filter_events_locked` | `engine/runtime_filter_v2.cpp` | — | 5 | — | 13 | new cohesive helper/function in scoped file |
| `obs_engine::capture_source_events_locked` | `engine/runtime_source_v2.cpp` | — | 5 | — | 13 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_source_kind_get` | `engine/runtime_source_v2.cpp` | 5 | 5 | 13 | 13 | unchanged |
| `Assert-Task11PropertyContract` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 5 | — | 12 | new cohesive helper/function in scoped file |
| `Assert-Task3CapabilitiesResponse` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 5 | — | 12 | new cohesive helper/function in scoped file |
| `Invoke-Task6Defaults` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 5 | — | 12 | new cohesive helper/function in scoped file |
| `Invoke-Task6InitialSettings` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 5 | — | 12 | new cohesive helper/function in scoped file |
| `obs_engine::submit_filter_settings` | `engine/runtime_filter_v2.cpp` | — | 5 | — | 12 | new cohesive helper/function in scoped file |
| `Invoke-Task5TelemetryUpgrade` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 5 | — | 11 | new cohesive helper/function in scoped file |
| `Invoke-Task7Bootstrap` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 5 | — | 11 | new cohesive helper/function in scoped file |
| `obs_engine::validate_float_property` | `engine/properties.cpp` | — | 5 | — | 11 | new cohesive helper/function in scoped file |
| `obs_engine::read_int32_required` | `engine/runtime_interaction_v2.cpp` | 5 | 5 | 11 | 11 | unchanged |
| `obs_engine::run_media_action` | `engine/runtime_media_v2.cpp` | — | 5 | — | 11 | new cohesive helper/function in scoped file |
| `Test-ExceptionIdentity` | `tools/check-complexity.ps1` | — | 5 | — | 11 | new cohesive helper/function in scoped file |
| `Assert-AEvents` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 5 | 5 | 10 | 10 | unchanged |
| `obs_engine::EventDispatcher::matches_locked` | `engine/events.cpp` | 5 | 5 | 10 | 10 | unchanged |
| `obs_engine::read_filter_kind` | `engine/runtime_filter_v2.cpp` | — | 5 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::read_optional_filter_name` | `engine/runtime_filter_v2.cpp` | — | 5 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::append_media_signal_event` | `engine/runtime_media_v2.cpp` | — | 5 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::read_source_state_kind` | `engine/runtime_source_v2.cpp` | — | 5 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::read_mouse_button_state` | `engine/runtime_interaction_v2.cpp` | — | 5 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::read_key_state` | `engine/runtime_interaction_v2.cpp` | — | 5 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::mark_orphaned_media_batch_locked` | `engine/runtime_media_v2.cpp` | — | 5 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::read_source_duplicate_name` | `engine/runtime_source_v2.cpp` | — | 5 | — | 9 | new cohesive helper/function in scoped file |
| `Assert-EventTargets` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 5 | — | 8 | new cohesive helper/function in scoped file |
| `Assert-SafeSettingsPayload` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 5 | — | 8 | new cohesive helper/function in scoped file |
| `Assert-Task4Hello` | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | — | 5 | — | 8 | new cohesive helper/function in scoped file |
| `Complete-Task8Session` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 5 | — | 8 | new cohesive helper/function in scoped file |
| `obs_engine::is_valid_event_pattern` | `engine/events.cpp` | 5 | 5 | 8 | 8 | unchanged |
| `obs_engine::execute_filter_request` | `engine/protocol_filter_v2.cpp` | — | 5 | — | 8 | new cohesive helper/function in scoped file |
| `obs_engine::parse_v2_request` | `engine/protocol_v2.cpp` | 18 | 5 | 39 | 8 | reduced by 13 |
| `obs_engine::read_source_state_name` | `engine/runtime_source_v2.cpp` | — | 5 | — | 8 | new cohesive helper/function in scoped file |
| `Assert-Task7SchemaTarget` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 5 | — | 7 | new cohesive helper/function in scoped file |
| `Invoke-Task8Bootstrap` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 5 | — | 7 | new cohesive helper/function in scoped file |
| `Invoke-Task8KindProperties` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 5 | — | 7 | new cohesive helper/function in scoped file |
| `Test-UnsupportedProjectPath` | `tools/check-complexity.ps1` | — | 5 | — | 7 | new cohesive helper/function in scoped file |
| `New-BaselineMarkdown` | `tools/check-complexity.ps1` | — | 4 | — | 70 | new cohesive helper/function in scoped file |
| `Invoke-Task9InitialInteraction` | `.github/scripts/engine-protocol-v2-task9.ps1` | — | 4 | — | 68 | new cohesive helper/function in scoped file |
| `Initialize-Task11Session` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 4 | — | 52 | new cohesive helper/function in scoped file |
| `New-PowerShellFunctionMetric` | `tools/check-complexity.ps1` | — | 4 | — | 45 | new cohesive helper/function in scoped file |
| `Complete-Task10Scenario` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 4 | — | 43 | new cohesive helper/function in scoped file |
| `Get-ScopedCppMetrics` | `tools/check-complexity.ps1` | — | 4 | — | 42 | new cohesive helper/function in scoped file |
| `Invoke-Task8ConcurrencyRegression` | `.github/scripts/engine-protocol-v2-task8-concurrency-run.ps1` | — | 4 | — | 31 | new cohesive helper/function in scoped file |
| `Assert-Task9ExpectedLogs` | `.github/scripts/engine-protocol-v2-task9.ps1` | — | 4 | — | 30 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_interaction_key` | `engine/runtime_interaction_v2.cpp` | 22 | 4 | 64 | 27 | reduced by 18 |
| `New-IdentityMigrationMaps` | `tools/check-complexity.ps1` | — | 4 | — | 26 | new cohesive helper/function in scoped file |
| `main` | `engine/properties_test.cpp` | 54 | 4 | 156 | 25 | reduced by 50 |
| `Start-RaceEngine` | `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | 4 | 4 | 23 | 23 | unchanged |
| `obs_engine::Engine::v2_interaction_text` | `engine/runtime_interaction_v2.cpp` | 10 | 4 | 30 | 23 | reduced by 6 |
| `obs_engine::Engine::command_program_set` | `engine/runtime.cpp` | 4 | 4 | 23 | 23 | unchanged |
| `obs_engine::EventDispatcher::require_resync_after_queued_events` | `engine/events.cpp` | 5 | 4 | 27 | 22 | reduced by 1 |
| `obs_engine::Engine::command_source_settings` | `engine/runtime.cpp` | 4 | 4 | 22 | 22 | unchanged |
| `obs_engine::Engine::command_scene_destroy` | `engine/runtime.cpp` | 4 | 4 | 22 | 22 | unchanged |
| `translate_media_action_type` | `libobs/obs-source.c` | — | 4 | — | 22 | new cohesive helper/function in scoped file |
| `Complete-Task11Scenario` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 4 | — | 21 | new cohesive helper/function in scoped file |
| `Finish-EngineCase` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 4 | 4 | 21 | 21 | unchanged |
| `obs_engine::disconnect_media_observer` | `engine/runtime_media_v2.cpp` | 4 | 4 | 21 | 21 | unchanged |
| `obs_engine::Engine::v2_build_filter_property_target` | `engine/runtime_properties_v2.cpp` | — | 4 | — | 21 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_source_list` | `engine/runtime_source_v2.cpp` | 4 | 4 | 21 | 21 | unchanged |
| `obs_engine::Engine::v2_source_reset_settings` | `engine/runtime_source_v2.cpp` | 5 | 4 | 23 | 21 | reduced by 1 |
| `obs_engine::Engine::command_source_update` | `engine/runtime.cpp` | 4 | 4 | 21 | 21 | unchanged |
| `Get-PowerShellMetrics` | `tools/check-complexity.ps1` | — | 4 | — | 21 | new cohesive helper/function in scoped file |
| `obs_engine::prepare_filter_mutation` | `engine/protocol_filter_v2.cpp` | — | 4 | — | 20 | new cohesive helper/function in scoped file |
| `obs_engine::send_v2_error` | `engine/protocol_v2.cpp` | 4 | 4 | 20 | 20 | unchanged |
| `obs_engine::Engine::v2_filter_record_update_baseline` | `engine/runtime_filter_v2.cpp` | 4 | 4 | 20 | 20 | unchanged |
| `obs_engine::Engine::v2_filter_forget_source` | `engine/runtime_filter_v2.cpp` | 4 | 4 | 20 | 20 | unchanged |
| `obs_engine::Engine::v2_filter_get_settings` | `engine/runtime_filter_v2.cpp` | 4 | 4 | 20 | 20 | unchanged |
| `obs_engine::Engine::v2_interaction_focus` | `engine/runtime_interaction_v2.cpp` | 4 | 4 | 20 | 20 | unchanged |
| `obs_engine::Engine::v2_build_source_property_target` | `engine/runtime_properties_v2.cpp` | — | 4 | — | 20 | new cohesive helper/function in scoped file |
| `Invoke-Task6SceneCreation` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 4 | — | 19 | new cohesive helper/function in scoped file |
| `Invoke-Task6SecondItem` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 4 | — | 19 | new cohesive helper/function in scoped file |
| `obs_engine::EventDispatcher::unsubscribe` | `engine/events.cpp` | 4 | 4 | 19 | 19 | unchanged |
| `obs_engine::validate_revision_guard` | `engine/protocol_v2.cpp` | 4 | 4 | 19 | 19 | unchanged |
| `obs_engine::handle_session_unsubscribe` | `engine/protocol_v2.cpp` | — | 4 | — | 19 | new cohesive helper/function in scoped file |
| `obs_engine::disconnect_observer` | `engine/runtime_source_v2.cpp` | 4 | 4 | 19 | 19 | unchanged |
| `Get-CheckMetricViolations` | `tools/check-complexity.ps1` | — | 4 | — | 19 | new cohesive helper/function in scoped file |
| `Invoke-AfterMode` | `tools/check-complexity.ps1` | — | 4 | — | 18 | new cohesive helper/function in scoped file |
| `obs_engine::EventDispatcher::publish` | `engine/events.cpp` | 12 | 4 | 52 | 17 | reduced by 8 |
| `obs_engine::read_string_field` | `engine/protocol_v2.cpp` | 4 | 4 | 17 | 17 | unchanged |
| `obs_engine::read_string_field` | `engine/runtime_filter_v2.cpp` | 4 | 4 | 17 | 17 | unchanged |
| `obs_engine::read_string_field` | `engine/runtime_interaction_v2.cpp` | 4 | 4 | 17 | 17 | unchanged |
| `obs_engine::read_string_field` | `engine/runtime_media_v2.cpp` | 4 | 4 | 17 | 17 | unchanged |
| `obs_engine::read_string_field` | `engine/runtime_properties_v2.cpp` | 4 | 4 | 17 | 17 | unchanged |
| `obs_engine::read_string_field` | `engine/runtime_source_v2.cpp` | 4 | 4 | 17 | 17 | unchanged |
| `obs_engine::read_string_field` | `engine/runtime_v2.cpp` | 4 | 4 | 17 | 17 | unchanged |
| `process_media_actions` | `libobs/obs-source.c` | 11 | 4 | 45 | 17 | reduced by 7 |
| `Get-LizardVersion` | `tools/check-complexity.ps1` | — | 4 | — | 17 | new cohesive helper/function in scoped file |
| `Get-ComparisonMetricLine` | `tools/check-complexity.ps1` | — | 4 | — | 17 | new cohesive helper/function in scoped file |
| `Read-NextEvent` | `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | 4 | 4 | 16 | 16 | unchanged |
| `Invoke-Task5SubscriptionDedupe` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 4 | — | 16 | new cohesive helper/function in scoped file |
| `Invoke-Task6ItemRemoval` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 4 | — | 16 | new cohesive helper/function in scoped file |
| `Invoke-Task6SceneRemoval` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 4 | — | 16 | new cohesive helper/function in scoped file |
| `obs_engine::read_integer_field` | `engine/runtime_filter_v2.cpp` | 4 | 4 | 16 | 16 | unchanged |
| `obs_engine::commit_direct_filter_event_locked` | `engine/runtime_filter_v2.cpp` | — | 4 | — | 16 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_get_filter_parent` | `engine/runtime_filter_v2.cpp` | — | 4 | — | 16 | new cohesive helper/function in scoped file |
| `obs_engine::read_integer_field` | `engine/runtime_interaction_v2.cpp` | 4 | 4 | 16 | 16 | unchanged |
| `obs_engine::read_integer_field` | `engine/runtime_media_v2.cpp` | 4 | 4 | 16 | 16 | unchanged |
| `obs_engine::commit_direct_media_event_locked` | `engine/runtime_media_v2.cpp` | — | 4 | — | 16 | new cohesive helper/function in scoped file |
| `obs_engine::read_integer_field` | `engine/runtime_source_v2.cpp` | 4 | 4 | 16 | 16 | unchanged |
| `obs_engine::queue_deferred_source_events_locked` | `engine/runtime_source_v2.cpp` | — | 4 | — | 16 | new cohesive helper/function in scoped file |
| `obs_engine::commit_direct_source_event_locked` | `engine/runtime_source_v2.cpp` | — | 4 | — | 16 | new cohesive helper/function in scoped file |
| `Test-LineIntersection` | `tools/check-complexity.ps1` | — | 4 | — | 16 | new cohesive helper/function in scoped file |
| `Get-OperatorCommitPathRecords` | `tools/check-complexity.ps1` | — | 4 | — | 16 | new cohesive helper/function in scoped file |
| `Invoke-Task7SourceFixture` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 4 | — | 15 | new cohesive helper/function in scoped file |
| `obs_engine::ProtocolWriter::start` | `engine/protocol.cpp` | 4 | 4 | 15 | 15 | unchanged |
| `obs_engine::read_integer` | `engine/protocol.cpp` | 4 | 4 | 15 | 15 | unchanged |
| `record_source_update_range_locked` | `libobs/obs-source.c` | — | 4 | — | 15 | new cohesive helper/function in scoped file |
| `obs_engine::commit_filter_result` | `engine/protocol_filter_v2.cpp` | — | 4 | — | 14 | new cohesive helper/function in scoped file |
| `obs_engine::commit_runtime_result` | `engine/protocol_v2.cpp` | — | 4 | — | 14 | new cohesive helper/function in scoped file |
| `obs_engine::classify_media_event_locked` | `engine/runtime_media_v2.cpp` | — | 4 | — | 14 | new cohesive helper/function in scoped file |
| `obs_engine::enqueue_media_action` | `engine/runtime_media_v2.cpp` | — | 4 | — | 14 | new cohesive helper/function in scoped file |
| `Get-PathScopeExclusion` | `tools/check-complexity.ps1` | — | 4 | — | 14 | new cohesive helper/function in scoped file |
| `Read-Task1EngineMessage` | `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1` | — | 4 | — | 13 | new cohesive helper/function in scoped file |
| `Read-EngineMessage` | `.github/scripts/engine-protocol-v2-task10.ps1` | 4 | 4 | 13 | 13 | unchanged |
| `Read-EngineMessage` | `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | 4 | 4 | 13 | 13 | unchanged |
| `Read-Task2EngineMessage` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 4 | — | 13 | new cohesive helper/function in scoped file |
| `Read-Task3EngineMessage` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 4 | — | 13 | new cohesive helper/function in scoped file |
| `Read-Task4EngineMessage` | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | — | 4 | — | 13 | new cohesive helper/function in scoped file |
| `Read-Task5EngineMessage` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 4 | — | 13 | new cohesive helper/function in scoped file |
| `Read-Task6EngineMessage` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 4 | — | 13 | new cohesive helper/function in scoped file |
| `Read-Task7EngineMessage` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 4 | — | 13 | new cohesive helper/function in scoped file |
| `Read-EngineMessage` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 4 | 4 | 13 | 13 | unchanged |
| `Read-EngineMessage` | `.github/scripts/engine-protocol-v2-task9.ps1` | 4 | 4 | 13 | 13 | unchanged |
| `obs_engine::text_type_name` | `engine/properties.cpp` | 4 | 4 | 13 | 13 | unchanged |
| `obs_engine::combo_type_name` | `engine/properties.cpp` | 4 | 4 | 13 | 13 | unchanged |
| `obs_engine::read_update_serial_range` | `engine/runtime_filter_v2.cpp` | 4 | 4 | 13 | 13 | unchanged |
| `obs_engine::annotate_filter_batch_locked` | `engine/runtime_filter_v2.cpp` | — | 4 | — | 13 | new cohesive helper/function in scoped file |
| `obs_engine::mouse_button_flag` | `engine/runtime_interaction_v2.cpp` | 4 | 4 | 13 | 13 | unchanged |
| `obs_engine::Engine::v2_get_media_source` | `engine/runtime_media_v2.cpp` | — | 4 | — | 13 | new cohesive helper/function in scoped file |
| `obs_engine::apply_candidate` | `engine/runtime_properties_v2.cpp` | 4 | 4 | 13 | 13 | unchanged |
| `Test-AstInsideRange` | `tools/check-complexity.ps1` | — | 4 | — | 13 | new cohesive helper/function in scoped file |
| `Invoke-Task6BadCreate` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 4 | — | 12 | new cohesive helper/function in scoped file |
| `Invoke-Task8SourceCreation` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 4 | — | 12 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_filter_observers_to_add` | `engine/runtime_filter_v2.cpp` | — | 4 | — | 12 | new cohesive helper/function in scoped file |
| `Read-TrustedBaselineDocument` | `tools/check-complexity.ps1` | — | 4 | — | 12 | new cohesive helper/function in scoped file |
| `Get-IdentityStringField` | `tools/check-complexity.ps1` | — | 4 | — | 12 | new cohesive helper/function in scoped file |
| `Get-SameNameBaselineCandidates` | `tools/check-complexity.ps1` | — | 4 | — | 12 | new cohesive helper/function in scoped file |
| `Initialize-Task1Protocol` | `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1` | — | 4 | — | 11 | new cohesive helper/function in scoped file |
| `Stop-Task1AfterFailure` | `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1` | — | 4 | — | 11 | new cohesive helper/function in scoped file |
| `Assert-Order` | `.github/scripts/engine-protocol-v2-task11.ps1` | 4 | 4 | 11 | 11 | unchanged |
| `Stop-Task2AfterFailure` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 4 | — | 11 | new cohesive helper/function in scoped file |
| `Stop-Task3AfterFailure` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 4 | — | 11 | new cohesive helper/function in scoped file |
| `Stop-Task4AfterFailure` | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | — | 4 | — | 11 | new cohesive helper/function in scoped file |
| `Invoke-Task5TelemetryRemoval` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 4 | — | 11 | new cohesive helper/function in scoped file |
| `Stop-Task5AfterFailure` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 4 | — | 11 | new cohesive helper/function in scoped file |
| `Read-Task8EngineMessage` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 4 | — | 11 | new cohesive helper/function in scoped file |
| `obs_engine::parse_numeric_argument` | `engine/config.cpp` | — | 4 | — | 11 | new cohesive helper/function in scoped file |
| `obs_engine::validate_list_property` | `engine/properties.cpp` | — | 4 | — | 11 | new cohesive helper/function in scoped file |
| `obs_engine::validate_color_property` | `engine/properties.cpp` | — | 4 | — | 11 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_sync_filter_observers` | `engine/runtime_filter_v2.cpp` | 28 | 4 | 97 | 11 | reduced by 24 |
| `obs_engine::prune_stale_interaction_sources_locked` | `engine/runtime_interaction_v2.cpp` | 4 | 4 | 11 | 11 | unchanged |
| `obs_source_dosignal` | `libobs/obs-internal.h` | 4 | 4 | 11 | 11 | unchanged |
| `Get-RegressionMetricViolation` | `tools/check-complexity.ps1` | — | 4 | — | 11 | new cohesive helper/function in scoped file |
| `Assert-Task11CapabilitySet` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 4 | — | 10 | new cohesive helper/function in scoped file |
| `Assert-Task5FinalSubscriptions` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 4 | — | 10 | new cohesive helper/function in scoped file |
| `Invoke-Task7Resolve` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 4 | — | 10 | new cohesive helper/function in scoped file |
| `find_schema_entry` | `engine/properties_test.cpp` | 4 | 4 | 10 | 10 | unchanged |
| `obs_engine::validate_int_property` | `engine/properties.cpp` | — | 4 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::clone_property_settings` | `engine/properties.cpp` | 4 | 4 | 10 | 10 | unchanged |
| `obs_engine::handle_v2_request` | `engine/protocol_filter_v2.cpp` | 4 | 4 | 10 | 10 | unchanged |
| `obs_engine::parse_v2_request_id` | `engine/protocol_v2.cpp` | — | 4 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::is_bounded_string` | `engine/runtime_filter_v2.cpp` | 4 | 4 | 10 | 10 | unchanged |
| `obs_engine::publish_deferred_filter_snapshot` | `engine/runtime_filter_v2.cpp` | 12 | 4 | 38 | 10 | reduced by 8 |
| `obs_engine::append_filter_settings_event` | `engine/runtime_filter_v2.cpp` | — | 4 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::read_key_native_fields` | `engine/runtime_interaction_v2.cpp` | — | 4 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::publish_deferred_media_snapshot` | `engine/runtime_media_v2.cpp` | 15 | 4 | 42 | 10 | reduced by 11 |
| `obs_engine::is_bounded_string` | `engine/runtime_source_v2.cpp` | 4 | 4 | 10 | 10 | unchanged |
| `obs_engine::is_bounded_string` | `engine/runtime_v2.cpp` | 4 | 4 | 10 | 10 | unchanged |
| `obs_engine::is_bounded_string` | `engine/runtime.cpp` | 4 | 4 | 10 | 10 | unchanged |
| `Test-BaselineMetricIdentity` | `tools/check-complexity.ps1` | — | 4 | — | 10 | new cohesive helper/function in scoped file |
| `Invoke-Task4ReadOnlyGuard` | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | — | 4 | — | 9 | new cohesive helper/function in scoped file |
| `Invoke-Task4UnsupportedGuard` | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | — | 4 | — | 9 | new cohesive helper/function in scoped file |
| `Invoke-Task4PostConflictPing` | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | — | 4 | — | 9 | new cohesive helper/function in scoped file |
| `check_group_schema` | `engine/properties_test.cpp` | — | 4 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::parse_v2_request_operation` | `engine/protocol_v2.cpp` | — | 4 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::parse_v2_request_method` | `engine/protocol_v2.cpp` | — | 4 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::filter_type_exists` | `engine/runtime_filter_v2.cpp` | 4 | 4 | 9 | 9 | unchanged |
| `obs_engine::result_has_filter_event` | `engine/runtime_filter_v2.cpp` | 4 | 4 | 9 | 9 | unchanged |
| `obs_engine::quarantine_uncertain_filter_event_locked` | `engine/runtime_filter_v2.cpp` | — | 4 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::result_has_source_event` | `engine/runtime_media_v2.cpp` | 4 | 4 | 9 | 9 | unchanged |
| `obs_engine::route_media_event_locked` | `engine/runtime_media_v2.cpp` | — | 4 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::filter_type_exists` | `engine/runtime_properties_v2.cpp` | 4 | 4 | 9 | 9 | unchanged |
| `obs_engine::result_has_source_event` | `engine/runtime_source_settle_v2.cpp` | 4 | 4 | 9 | 9 | unchanged |
| `obs_engine::result_has_source_event` | `engine/runtime_source_v2.cpp` | 4 | 4 | 9 | 9 | unchanged |
| `obs_engine::Engine::input_type_exists` | `engine/runtime.cpp` | 4 | 4 | 9 | 9 | unchanged |
| `Get-WorkflowExecutableLineCount` | `tools/check-complexity.ps1` | — | 4 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::pattern_matches` | `engine/events.cpp` | 4 | 4 | 8 | 8 | unchanged |
| `check_password_schema` | `engine/properties_test.cpp` | — | 4 | — | 8 | new cohesive helper/function in scoped file |
| `obs_engine::has_pending_media_action_locked` | `engine/runtime_media_v2.cpp` | — | 4 | — | 8 | new cohesive helper/function in scoped file |
| `obs_engine::read_property_name` | `engine/runtime_properties_v2.cpp` | 4 | 4 | 8 | 8 | unchanged |
| `obs_engine::read_source_state_version` | `engine/runtime_source_v2.cpp` | — | 4 | — | 8 | new cohesive helper/function in scoped file |
| `Assert-SafeEventWireOrder` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 4 | — | 7 | new cohesive helper/function in scoped file |
| `check_overlap_output` | `engine/events_test.cpp` | — | 4 | — | 7 | new cohesive helper/function in scoped file |
| `obs_engine::filter_settings_are_equal` | `engine/runtime_filter_v2.cpp` | — | 4 | — | 7 | new cohesive helper/function in scoped file |
| `obs_engine::read_action_serial` | `engine/runtime_media_v2.cpp` | 4 | 4 | 7 | 7 | unchanged |
| `reserve_source_update_serial_locked` | `libobs/obs-source.c` | — | 4 | — | 7 | new cohesive helper/function in scoped file |
| `Assert-Error` | `.github/scripts/engine-protocol-v2-task10.ps1` | 4 | 4 | 6 | 6 | unchanged |
| `Assert-Timeout` | `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | 4 | 4 | 6 | 6 | unchanged |
| `Assert-Error` | `.github/scripts/engine-protocol-v2-task11.ps1` | 4 | 4 | 6 | 6 | unchanged |
| `Assert-Task2Unknown` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 4 | — | 6 | new cohesive helper/function in scoped file |
| `Assert-Task2BadParams` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 4 | — | 6 | new cohesive helper/function in scoped file |
| `Assert-Task3Unknown` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 4 | — | 6 | new cohesive helper/function in scoped file |
| `looks_like_v2_request` | `engine/host.cpp` | 4 | 4 | 6 | 6 | unchanged |
| `Assert-Task2Ping` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 4 | — | 5 | new cohesive helper/function in scoped file |
| `Assert-Task2BadId` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 4 | — | 5 | new cohesive helper/function in scoped file |
| `Assert-Task3Ping` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 4 | — | 5 | new cohesive helper/function in scoped file |
| `Assert-ErrorAtRevision` | `.github/scripts/engine-protocol-v2-task9.ps1` | 4 | 4 | 5 | 5 | unchanged |
| `obs_engine::filter_update_event_covers_serial` | `engine/runtime_filter_v2.cpp` | 4 | 4 | 5 | 5 | unchanged |
| `Invoke-Task8Refresh` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 4 | — | 4 | new cohesive helper/function in scoped file |
| `Invoke-Task10UnsupportedAndLateSeek` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 3 | — | 42 | new cohesive helper/function in scoped file |
| `Get-Statistics` | `tools/check-complexity.ps1` | — | 3 | — | 38 | new cohesive helper/function in scoped file |
| `New-MeasurementContext` | `tools/check-complexity.ps1` | — | 3 | — | 37 | new cohesive helper/function in scoped file |
| `Invoke-Task9ValidationScenario` | `.github/scripts/engine-protocol-v2-task9.ps1` | — | 3 | — | 32 | new cohesive helper/function in scoped file |
| `New-PathScopeResult` | `tools/check-complexity.ps1` | — | 3 | — | 31 | new cohesive helper/function in scoped file |
| `New-BaselineContext` | `tools/check-complexity.ps1` | — | 3 | — | 30 | new cohesive helper/function in scoped file |
| `Invoke-Task11UnrelatedUpdate` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 3 | — | 29 | new cohesive helper/function in scoped file |
| `New-PostPathContext` | `tools/check-complexity.ps1` | — | 3 | — | 29 | new cohesive helper/function in scoped file |
| `Invoke-Task10BlockingFollowup` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 3 | — | 28 | new cohesive helper/function in scoped file |
| `Invoke-Task10BlockingSignal` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 3 | — | 28 | new cohesive helper/function in scoped file |
| `Invoke-Task11OrderValidation` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 3 | — | 27 | new cohesive helper/function in scoped file |
| `obs_engine::property_type_name` | `engine/properties.cpp` | 14 | 3 | 33 | 27 | reduced by 11 |
| `obs_engine::handle_capability_request` | `engine/protocol_filter_v2.cpp` | 3 | 3 | 27 | 27 | unchanged |
| `Start-Task11Engine` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 3 | — | 26 | new cohesive helper/function in scoped file |
| `Initialize-Task6Runtime` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 3 | — | 25 | new cohesive helper/function in scoped file |
| `Initialize-Task7Runtime` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 3 | — | 25 | new cohesive helper/function in scoped file |
| `Start-Task10Engine` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 3 | — | 24 | new cohesive helper/function in scoped file |
| `Start-Task1Engine` | `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1` | — | 3 | — | 23 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_filter_remove` | `engine/runtime_filter_v2.cpp` | 5 | 3 | 28 | 23 | reduced by 2 |
| `obs_engine::Engine::v2_interaction_mouse_wheel` | `engine/runtime_interaction_v2.cpp` | 10 | 3 | 34 | 23 | reduced by 7 |
| `New-WorkflowAssessment` | `tools/check-complexity.ps1` | — | 3 | — | 23 | new cohesive helper/function in scoped file |
| `Start-Task9Engine` | `.github/scripts/engine-protocol-v2-task9.ps1` | — | 3 | — | 22 | new cohesive helper/function in scoped file |
| `obs_engine::serialize_frame_rate_property` | `engine/properties.cpp` | — | 3 | — | 22 | new cohesive helper/function in scoped file |
| `obs_engine::make_state_snapshot` | `engine/runtime_source_v2.cpp` | 3 | 3 | 22 | 22 | unchanged |
| `Invoke-Task11TimeoutStart` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 3 | — | 21 | new cohesive helper/function in scoped file |
| `Start-Task2Engine` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 3 | — | 21 | new cohesive helper/function in scoped file |
| `Start-Task3Engine` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 3 | — | 21 | new cohesive helper/function in scoped file |
| `Start-Task4Engine` | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | — | 3 | — | 21 | new cohesive helper/function in scoped file |
| `Start-Task5Engine` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 3 | — | 21 | new cohesive helper/function in scoped file |
| `Initialize-Task8Runtime` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 3 | — | 21 | new cohesive helper/function in scoped file |
| `obs_engine::EventDispatcher::enqueue_state_locked` | `engine/events.cpp` | — | 3 | — | 21 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_source_get_state` | `engine/runtime_source_v2.cpp` | 4 | 3 | 23 | 21 | reduced by 1 |
| `obs_engine::remove_filter_observer` | `engine/runtime_filter_v2.cpp` | 3 | 3 | 20 | 20 | unchanged |
| `Read-WorkflowYamlRoot` | `tools/check-complexity.ps1` | — | 3 | — | 20 | new cohesive helper/function in scoped file |
| `New-NonCyclomaticRecords` | `tools/check-complexity.ps1` | — | 3 | — | 20 | new cohesive helper/function in scoped file |
| `Remove-Task11ParentSource` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 3 | — | 19 | new cohesive helper/function in scoped file |
| `Complete-Task9Session` | `.github/scripts/engine-protocol-v2-task9.ps1` | — | 3 | — | 19 | new cohesive helper/function in scoped file |
| `Assert-IdentityMigrationTargets` | `tools/check-complexity.ps1` | — | 3 | — | 19 | new cohesive helper/function in scoped file |
| `Invoke-Task1ProtocolV1Smoke` | `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1` | — | 3 | — | 18 | new cohesive helper/function in scoped file |
| `Invoke-Task2FramingSmoke` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 3 | — | 18 | new cohesive helper/function in scoped file |
| `Invoke-Task3CapabilitiesSmoke` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 3 | — | 18 | new cohesive helper/function in scoped file |
| `Invoke-Task4RevisionsSmoke` | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | — | 3 | — | 18 | new cohesive helper/function in scoped file |
| `Invoke-Task5SubscriptionsSmoke` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 3 | — | 18 | new cohesive helper/function in scoped file |
| `run_engine` | `engine/host.cpp` | — | 3 | — | 18 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_media_play` | `engine/runtime_media_v2.cpp` | 8 | 3 | 31 | 18 | reduced by 5 |
| `obs_engine::Engine::v2_media_pause` | `engine/runtime_media_v2.cpp` | 8 | 3 | 31 | 18 | reduced by 5 |
| `obs_engine::Engine::v2_media_stop` | `engine/runtime_media_v2.cpp` | 8 | 3 | 30 | 18 | reduced by 5 |
| `obs_engine::make_properties_document` | `engine/runtime_properties_v2.cpp` | 3 | 3 | 18 | 18 | unchanged |
| `obs_engine::Engine::command_source_destroy` | `engine/runtime.cpp` | 3 | 3 | 18 | 18 | unchanged |
| `initialize_compatibility_updater` | `plugins/win-capture/plugin-main.c` | — | 3 | — | 18 | new cohesive helper/function in scoped file |
| `Get-PostAcceptedOperatorPathSets` | `tools/check-complexity.ps1` | — | 3 | — | 18 | new cohesive helper/function in scoped file |
| `New-TestFilter` | `.github/scripts/engine-protocol-v2-task11.ps1` | 3 | 3 | 17 | 17 | unchanged |
| `Initialize-Task6Protocol` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 3 | — | 17 | new cohesive helper/function in scoped file |
| `Assert-Task7Capabilities` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 3 | — | 17 | new cohesive helper/function in scoped file |
| `Assert-Task9Capabilities` | `.github/scripts/engine-protocol-v2-task9.ps1` | — | 3 | — | 17 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_filter_get_enabled` | `engine/runtime_filter_v2.cpp` | 3 | 3 | 17 | 17 | unchanged |
| `obs_engine::Engine::v2_item_remove` | `engine/runtime_v2.cpp` | 3 | 3 | 17 | 17 | unchanged |
| `Get-CommitRecords` | `tools/check-complexity.ps1` | — | 3 | — | 17 | new cohesive helper/function in scoped file |
| `Get-AstTypeMap` | `tools/check-complexity.ps1` | — | 3 | — | 17 | new cohesive helper/function in scoped file |
| `Invoke-BaselineMode` | `tools/check-complexity.ps1` | — | 3 | — | 17 | new cohesive helper/function in scoped file |
| `Complete-Task1ProtocolV1` | `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1` | — | 3 | — | 16 | new cohesive helper/function in scoped file |
| `Invoke-Task10FixtureStage` | `.github/scripts/engine-protocol-v2-task10-stage-fixture.ps1` | — | 3 | — | 16 | new cohesive helper/function in scoped file |
| `Invoke-Task9FixtureStage` | `.github/scripts/engine-protocol-v2-task9-stage-fixture.ps1` | — | 3 | — | 16 | new cohesive helper/function in scoped file |
| `obs_engine::serialize_property` | `engine/properties.cpp` | 21 | 3 | 104 | 16 | reduced by 18 |
| `obs_engine::read_bool_field` | `engine/protocol_v2.cpp` | 3 | 3 | 16 | 16 | unchanged |
| `obs_engine::read_object_field` | `engine/protocol_v2.cpp` | 3 | 3 | 16 | 16 | unchanged |
| `obs_engine::read_array_field` | `engine/protocol_v2.cpp` | 3 | 3 | 16 | 16 | unchanged |
| `obs_engine::read_bool_field` | `engine/runtime_filter_v2.cpp` | 3 | 3 | 16 | 16 | unchanged |
| `obs_engine::read_object_field` | `engine/runtime_filter_v2.cpp` | 3 | 3 | 16 | 16 | unchanged |
| `obs_engine::read_bool_field` | `engine/runtime_interaction_v2.cpp` | 3 | 3 | 16 | 16 | unchanged |
| `obs_engine::read_object_field` | `engine/runtime_interaction_v2.cpp` | 3 | 3 | 16 | 16 | unchanged |
| `obs_engine::read_object_field` | `engine/runtime_properties_v2.cpp` | 3 | 3 | 16 | 16 | unchanged |
| `obs_engine::read_object_field` | `engine/runtime_source_v2.cpp` | 3 | 3 | 16 | 16 | unchanged |
| `obs_engine::read_object_field` | `engine/runtime_v2.cpp` | 3 | 3 | 16 | 16 | unchanged |
| `obs_engine::Engine::v2_source_get_settings` | `engine/runtime_v2.cpp` | 4 | 3 | 18 | 16 | reduced by 1 |
| `obs_engine::Engine::command_item_remove` | `engine/runtime.cpp` | 3 | 3 | 16 | 16 | unchanged |
| `Get-WorkflowOtherAssessment` | `tools/check-complexity.ps1` | — | 3 | — | 16 | new cohesive helper/function in scoped file |
| `Assert-IdentityMigrationLineage` | `tools/check-complexity.ps1` | — | 3 | — | 16 | new cohesive helper/function in scoped file |
| `Assert-ScriptBodyMeasured` | `tools/check-complexity.tests.ps1` | — | 3 | — | 16 | new cohesive helper/function in scoped file |
| `Invoke-Task11FixtureStage` | `.github/scripts/engine-protocol-v2-task11-stage-fixture.ps1` | — | 3 | — | 15 | new cohesive helper/function in scoped file |
| `Complete-Task2Session` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 3 | — | 15 | new cohesive helper/function in scoped file |
| `Complete-Task3Session` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 3 | — | 15 | new cohesive helper/function in scoped file |
| `Initialize-Task5Protocol` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 3 | — | 15 | new cohesive helper/function in scoped file |
| `Invoke-Task6Subscribe` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 3 | — | 15 | new cohesive helper/function in scoped file |
| `obs_engine::EventDispatcher::stop_and_drain` | `engine/events.cpp` | 3 | 3 | 15 | 15 | unchanged |
| `obs_engine::Engine::v2_filter_kind_defaults` | `engine/runtime_filter_v2.cpp` | 6 | 3 | 19 | 15 | reduced by 3 |
| `obs_engine::remember_timed_out_media_action` | `engine/runtime_media_v2.cpp` | — | 3 | — | 15 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::command_source_defaults` | `engine/runtime.cpp` | 3 | 3 | 15 | 15 | unchanged |
| `start_game_capture` | `plugins/win-capture/plugin-main.c` | — | 3 | — | 15 | new cohesive helper/function in scoped file |
| `obs_module_load` | `plugins/win-capture/plugin-main.c` | 12 | 3 | 53 | 15 | reduced by 9 |
| `New-MetricKeyMap` | `tools/check-complexity.ps1` | — | 3 | — | 15 | new cohesive helper/function in scoped file |
| `Format-FunctionList` | `tools/check-complexity.ps1` | — | 3 | — | 15 | new cohesive helper/function in scoped file |
| `Add-ComparisonExceptions` | `tools/check-complexity.ps1` | — | 3 | — | 15 | new cohesive helper/function in scoped file |
| `Add-ComparisonLimitations` | `tools/check-complexity.ps1` | — | 3 | — | 15 | new cohesive helper/function in scoped file |
| `Get-ReportScriptBodyMetric` | `tools/check-complexity.tests.ps1` | — | 3 | — | 15 | new cohesive helper/function in scoped file |
| `Assert-Task8Capabilities` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 3 | — | 14 | new cohesive helper/function in scoped file |
| `obs_engine::validate_mutation_guard` | `engine/protocol_filter_v2.cpp` | 3 | 3 | 14 | 14 | unchanged |
| `obs_engine::ProtocolWriter::stop` | `engine/protocol.cpp` | 3 | 3 | 14 | 14 | unchanged |
| `Get-AstCyclomaticComplexity` | `tools/check-complexity.ps1` | — | 3 | — | 14 | new cohesive helper/function in scoped file |
| `Get-ExactJsonPropertyValue` | `tools/check-complexity.ps1` | — | 3 | — | 14 | new cohesive helper/function in scoped file |
| `Get-IdentityMigrationBaseline` | `tools/check-complexity.ps1` | — | 3 | — | 14 | new cohesive helper/function in scoped file |
| `Invoke-Task10FixtureRemove` | `.github/scripts/engine-protocol-v2-task10-remove-fixture.ps1` | — | 3 | — | 13 | new cohesive helper/function in scoped file |
| `Read-Event` | `.github/scripts/engine-protocol-v2-task11.ps1` | 13 | 3 | 34 | 13 | reduced by 10 |
| `Invoke-Task9FixtureRemove` | `.github/scripts/engine-protocol-v2-task9-remove-fixture.ps1` | — | 3 | — | 13 | new cohesive helper/function in scoped file |
| `obs_engine::parse_plugin_argument` | `engine/config.cpp` | — | 3 | — | 13 | new cohesive helper/function in scoped file |
| `obs_engine::EventDispatcher::require_resync_due_to_overflow` | `engine/events.cpp` | 4 | 3 | 18 | 13 | reduced by 1 |
| `obs_engine::snapshot_interaction_state` | `engine/runtime_interaction_v2.cpp` | — | 3 | — | 13 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_bind_source_events` | `engine/runtime_source_v2.cpp` | 3 | 3 | 13 | 13 | unchanged |
| `obs_engine::apply_legacy_transform_scalar` | `engine/runtime.cpp` | — | 3 | — | 13 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::validate_source_type` | `engine/runtime.cpp` | 3 | 3 | 13 | 13 | unchanged |
| `Get-WorkflowPowerShellAssessment` | `tools/check-complexity.ps1` | — | 3 | — | 13 | new cohesive helper/function in scoped file |
| `New-PathRecord` | `tools/check-complexity.ps1` | — | 3 | — | 13 | new cohesive helper/function in scoped file |
| `Get-UntrackedPathRecords` | `tools/check-complexity.ps1` | — | 3 | — | 13 | new cohesive helper/function in scoped file |
| `Invoke-ComplexityMode` | `tools/check-complexity.ps1` | — | 3 | — | 13 | new cohesive helper/function in scoped file |
| `Assert-Task10Capabilities` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 3 | — | 12 | new cohesive helper/function in scoped file |
| `Read-SafeSettingsEvent` | `.github/scripts/engine-protocol-v2-task11.ps1` | 13 | 3 | 32 | 12 | reduced by 10 |
| `Initialize-Task3Protocol` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 3 | — | 12 | new cohesive helper/function in scoped file |
| `obs_engine::parse_locale_argument` | `engine/config.cpp` | — | 3 | — | 12 | new cohesive helper/function in scoped file |
| `obs_engine::report_filter_resync` | `engine/runtime_filter_v2.cpp` | — | 3 | — | 12 | new cohesive helper/function in scoped file |
| `obs_engine::report_media_resync` | `engine/runtime_media_v2.cpp` | — | 3 | — | 12 | new cohesive helper/function in scoped file |
| `obs_engine::append_source_settings_event` | `engine/runtime_source_v2.cpp` | — | 3 | — | 12 | new cohesive helper/function in scoped file |
| `obs_engine::apply_transform_rotation` | `engine/runtime_v2.cpp` | — | 3 | — | 12 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_append_item_removal_events` | `engine/runtime_v2.cpp` | — | 3 | — | 12 | new cohesive helper/function in scoped file |
| `Test-ExactStringInList` | `tools/check-complexity.ps1` | — | 3 | — | 12 | new cohesive helper/function in scoped file |
| `Write-Utf8File` | `tools/check-complexity.ps1` | — | 3 | — | 12 | new cohesive helper/function in scoped file |
| `Import-WorkflowYamlSupport` | `tools/check-complexity.ps1` | — | 3 | — | 12 | new cohesive helper/function in scoped file |
| `Get-ChangedPathRecords` | `tools/check-complexity.ps1` | — | 3 | — | 12 | new cohesive helper/function in scoped file |
| `Read-CheckBaselineReport` | `tools/check-complexity.ps1` | — | 3 | — | 12 | new cohesive helper/function in scoped file |
| `Get-Task1ColorType` | `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `Invoke-Task11FixtureRemove` | `.github/scripts/engine-protocol-v2-task11-remove-fixture.ps1` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `Send-Task6Request` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `Send-Task7Request` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `Get-Task7ColorKind` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `Send-V2Request` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 3 | 3 | 11 | 11 | unchanged |
| `Send-Task8Request` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `Send-V2Request` | `.github/scripts/engine-protocol-v2-task9.ps1` | 3 | 3 | 11 | 11 | unchanged |
| `obs_engine::text_info_type_name` | `engine/properties.cpp` | 3 | 3 | 11 | 11 | unchanged |
| `obs_engine::path_type_name` | `engine/properties.cpp` | 3 | 3 | 11 | 11 | unchanged |
| `obs_engine::editable_list_type_name` | `engine/properties.cpp` | 3 | 3 | 11 | 11 | unchanged |
| `obs_engine::serialize_property_details` | `engine/properties.cpp` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `obs_engine::serialize_property_array` | `engine/properties.cpp` | 3 | 3 | 11 | 11 | unchanged |
| `obs_engine::execute_filter_method` | `engine/protocol_filter_v2.cpp` | 20 | 3 | 48 | 11 | reduced by 17 |
| `obs_engine::publish_runtime_events` | `engine/protocol_filter_v2.cpp` | 3 | 3 | 11 | 11 | unchanged |
| `obs_engine::execute_runtime_method` | `engine/protocol_v2.cpp` | 54 | 3 | 116 | 11 | reduced by 51 |
| `obs_engine::publish_runtime_events` | `engine/protocol_v2.cpp` | 3 | 3 | 11 | 11 | unchanged |
| `obs_engine::handle_session_subscribe` | `engine/protocol_v2.cpp` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `obs_engine::parse_v2_request_params` | `engine/protocol_v2.cpp` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `obs_engine::settings_json` | `engine/runtime_filter_v2.cpp` | 3 | 3 | 11 | 11 | unchanged |
| `obs_engine::forget_uncertain_filter_updates_locked` | `engine/runtime_filter_v2.cpp` | 3 | 3 | 11 | 11 | unchanged |
| `obs_engine::Engine::v2_get_source` | `engine/runtime_source_v2.cpp` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_wait_for_event_capture_callbacks` | `engine/runtime_source_v2.cpp` | 3 | 3 | 11 | 11 | unchanged |
| `Get-StagedPathRecords` | `tools/check-complexity.ps1` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `Get-CommitPathRecords` | `tools/check-complexity.ps1` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `Get-PythonExecutable` | `tools/check-complexity.ps1` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `Read-OptionalReport` | `tools/check-complexity.ps1` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `Get-ComplexityAllowlist` | `tools/check-complexity.ps1` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `Get-ExceptionMetricViolation` | `tools/check-complexity.ps1` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `Write-FixtureText` | `tools/check-complexity.tests.ps1` | — | 3 | — | 11 | new cohesive helper/function in scoped file |
| `Assert-Task1SourceSettings` | `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1` | — | 3 | — | 10 | new cohesive helper/function in scoped file |
| `Get-Task10ColorKind` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 3 | — | 10 | new cohesive helper/function in scoped file |
| `Assert-Task11CoreRouting` | `.github/scripts/engine-protocol-v2-task11-core-audit.ps1` | — | 3 | — | 10 | new cohesive helper/function in scoped file |
| `Initialize-Task2Protocol` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 3 | — | 10 | new cohesive helper/function in scoped file |
| `Initialize-Task4Protocol` | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | — | 3 | — | 10 | new cohesive helper/function in scoped file |
| `Get-Task6ColorKind` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 3 | — | 10 | new cohesive helper/function in scoped file |
| `Initialize-Task8Protocol` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 3 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::parse_value_argument` | `engine/config.cpp` | — | 3 | — | 10 | new cohesive helper/function in scoped file |
| `toggle_modified` | `engine/properties_test.cpp` | 3 | 3 | 10 | 10 | unchanged |
| `obs_engine::validate_property_item` | `engine/properties.cpp` | 38 | 3 | 91 | 10 | reduced by 35 |
| `obs_engine::Engine::v2_begin_filter_event_capture` | `engine/runtime_filter_v2.cpp` | 3 | 3 | 10 | 10 | unchanged |
| `obs_engine::parse_mouse_button` | `engine/runtime_interaction_v2.cpp` | — | 3 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_begin_media_event_capture` | `engine/runtime_media_v2.cpp` | 3 | 3 | 10 | 10 | unchanged |
| `obs_engine::append_source_rename_event` | `engine/runtime_source_v2.cpp` | — | 3 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_item_handles_for_scene` | `engine/runtime_v2.cpp` | — | 3 | — | 10 | new cohesive helper/function in scoped file |
| `Get-SameNameCurrentCandidates` | `tools/check-complexity.ps1` | — | 3 | — | 10 | new cohesive helper/function in scoped file |
| `Read-IdentityEntriesForMode` | `tools/check-complexity.ps1` | — | 3 | — | 10 | new cohesive helper/function in scoped file |
| `Assert-EventSequence` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 3 | — | 9 | new cohesive helper/function in scoped file |
| `Assert-EventSequence` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 3 | — | 9 | new cohesive helper/function in scoped file |
| `Assert-Task11KindContract` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 3 | — | 9 | new cohesive helper/function in scoped file |
| `Initialize-Task9Session` | `.github/scripts/engine-protocol-v2-task9.ps1` | — | 3 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::FilterCallbackScope::~FilterCallbackScope` | `engine/runtime_filter_v2.cpp` | 3 | 3 | 9 | 9 | unchanged |
| `obs_engine::route_filter_event_locked` | `engine/runtime_filter_v2.cpp` | — | 3 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::read_filter_observer_generation` | `engine/runtime_filter_v2.cpp` | — | 3 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::filter_update_is_settled` | `engine/runtime_filter_v2.cpp` | — | 3 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_bind_filter_events` | `engine/runtime_filter_v2.cpp` | 3 | 3 | 9 | 9 | unchanged |
| `obs_engine::Engine::v2_wait_for_filter_event_callbacks` | `engine/runtime_filter_v2.cpp` | 3 | 3 | 9 | 9 | unchanged |
| `obs_engine::MediaCallbackScope::~MediaCallbackScope` | `engine/runtime_media_v2.cpp` | 3 | 3 | 9 | 9 | unchanged |
| `obs_engine::Engine::v2_bind_media_events` | `engine/runtime_media_v2.cpp` | 3 | 3 | 9 | 9 | unchanged |
| `obs_engine::Engine::v2_wait_for_media_event_callbacks` | `engine/runtime_media_v2.cpp` | 3 | 3 | 9 | 9 | unchanged |
| `obs_engine::clone_data` | `engine/runtime_source_v2.cpp` | 3 | 3 | 9 | 9 | unchanged |
| `obs_engine::SourceCallbackScope::~SourceCallbackScope` | `engine/runtime_source_v2.cpp` | 3 | 3 | 9 | 9 | unchanged |
| `obs_engine::Engine::remove_items_for_source` | `engine/runtime.cpp` | 3 | 3 | 9 | 9 | unchanged |
| `obs_engine::Engine::remove_items_for_scene` | `engine/runtime.cpp` | 3 | 3 | 9 | 9 | unchanged |
| `Get-WorkflowBlockAssessment` | `tools/check-complexity.ps1` | — | 3 | — | 9 | new cohesive helper/function in scoped file |
| `Assert-Ok` | `.github/scripts/engine-protocol-v2-task10.ps1` | 3 | 3 | 8 | 8 | unchanged |
| `Assert-Task11PublicApi` | `.github/scripts/engine-protocol-v2-task11-core-audit.ps1` | — | 3 | — | 8 | new cohesive helper/function in scoped file |
| `Assert-Ok` | `.github/scripts/engine-protocol-v2-task11.ps1` | 3 | 3 | 8 | 8 | unchanged |
| `Assert-NoLateSettingsEvent` | `.github/scripts/engine-protocol-v2-task11.ps1` | 3 | 3 | 8 | 8 | unchanged |
| `Assert-Ok` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 3 | 3 | 8 | 8 | unchanged |
| `obs_engine::find_list_value_matcher` | `engine/properties.cpp` | — | 3 | — | 8 | new cohesive helper/function in scoped file |
| `obs_engine::classify_filter_method` | `engine/protocol_filter_v2.cpp` | 20 | 3 | 42 | 8 | reduced by 17 |
| `obs_engine::is_mutating` | `engine/protocol_filter_v2.cpp` | 13 | 3 | 20 | 8 | reduced by 10 |
| `obs_engine::classify_method` | `engine/protocol_v2.cpp` | 61 | 3 | 124 | 8 | reduced by 58 |
| `obs_engine::method_is_mutating` | `engine/protocol_v2.cpp` | 24 | 3 | 31 | 8 | reduced by 21 |
| `obs_engine::method_is_runtime` | `engine/protocol_v2.cpp` | 54 | 3 | 61 | 8 | reduced by 51 |
| `obs_engine::method_needs_source_settle` | `engine/protocol_v2.cpp` | 5 | 3 | 12 | 8 | reduced by 2 |
| `obs_engine::read_handle_field` | `engine/runtime_properties_v2.cpp` | 3 | 3 | 8 | 8 | unchanged |
| `obs_engine::read_handle_field` | `engine/runtime_source_v2.cpp` | 3 | 3 | 8 | 8 | unchanged |
| `obs_engine::read_handle_field` | `engine/runtime_v2.cpp` | 3 | 3 | 8 | 8 | unchanged |
| `Invoke-ComplexityRun` | `tools/check-complexity.ps1` | — | 3 | — | 8 | new cohesive helper/function in scoped file |
| `obs_engine::data_to_json` | `engine/events.cpp` | 3 | 3 | 7 | 7 | unchanged |
| `obs_engine::redact_sensitive_property_names` | `engine/properties_sensitive.cpp` | 3 | 3 | 7 | 7 | unchanged |
| `obs_engine::sanitize_property_settings` | `engine/properties.cpp` | 3 | 3 | 7 | 7 | unchanged |
| `obs_engine::clone_data` | `engine/runtime_filter_v2.cpp` | 3 | 3 | 7 | 7 | unchanged |
| `obs_engine::read_source_state_settings` | `engine/runtime_source_v2.cpp` | — | 3 | — | 7 | new cohesive helper/function in scoped file |
| `obs_engine::SourceEventCaptureGate::route_for_current_thread` | `engine/source_event_capture.hpp` | 3 | 3 | 7 | 7 | unchanged |
| `Test-UnscopedExecutablePath` | `tools/check-complexity.ps1` | — | 3 | — | 7 | new cohesive helper/function in scoped file |
| `Assert-CommandEventAfterResponse` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 3 | — | 6 | new cohesive helper/function in scoped file |
| `Get-Task8ColorKind` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 3 | — | 6 | new cohesive helper/function in scoped file |
| `Invoke-Task8ResetSettings` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 3 | — | 6 | new cohesive helper/function in scoped file |
| `obs_engine::validate_group_property` | `engine/properties.cpp` | — | 3 | — | 6 | new cohesive helper/function in scoped file |
| `obs_engine::fail` | `engine/runtime_filter_v2.cpp` | 3 | 3 | 6 | 6 | unchanged |
| `obs_engine::read_handle_field` | `engine/runtime_filter_v2.cpp` | 3 | 3 | 6 | 6 | unchanged |
| `obs_engine::fail` | `engine/runtime_interaction_v2.cpp` | 3 | 3 | 6 | 6 | unchanged |
| `obs_engine::read_handle_field` | `engine/runtime_interaction_v2.cpp` | 3 | 3 | 6 | 6 | unchanged |
| `obs_engine::fail` | `engine/runtime_media_v2.cpp` | 3 | 3 | 6 | 6 | unchanged |
| `obs_engine::read_handle_field` | `engine/runtime_media_v2.cpp` | 3 | 3 | 6 | 6 | unchanged |
| `obs_engine::consume_timed_out_media_action_locked` | `engine/runtime_media_v2.cpp` | — | 3 | — | 6 | new cohesive helper/function in scoped file |
| `obs_engine::fail` | `engine/runtime_properties_v2.cpp` | 3 | 3 | 6 | 6 | unchanged |
| `obs_engine::fail` | `engine/runtime_source_v2.cpp` | 3 | 3 | 6 | 6 | unchanged |
| `obs_engine::append_source_dimensions_event` | `engine/runtime_source_v2.cpp` | — | 3 | — | 6 | new cohesive helper/function in scoped file |
| `obs_engine::fail` | `engine/runtime_v2.cpp` | 3 | 3 | 6 | 6 | unchanged |
| `obs_engine::Engine::allocate_handle` | `engine/runtime.cpp` | 3 | 3 | 6 | 6 | unchanged |
| `Assert-Ok` | `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | 3 | 3 | 5 | 5 | unchanged |
| `Assert-Task2Close` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 3 | — | 5 | new cohesive helper/function in scoped file |
| `Assert-Task3Close` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 3 | — | 5 | new cohesive helper/function in scoped file |
| `Assert-OkAtRevision` | `.github/scripts/engine-protocol-v2-task9.ps1` | 3 | 3 | 5 | 5 | unchanged |
| `check_frame_rate_endpoint` | `engine/properties_test.cpp` | — | 3 | — | 5 | new cohesive helper/function in scoped file |
| `obs_engine::set_nonempty_string` | `engine/properties.cpp` | 3 | 3 | 5 | 5 | unchanged |
| `Invoke-Task8KindDefaults` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 3 | — | 4 | new cohesive helper/function in scoped file |
| `Invoke-Task8InitialSourceList` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 3 | — | 4 | new cohesive helper/function in scoped file |
| `obs_engine::should_discard_filter_batch` | `engine/runtime_filter_v2.cpp` | — | 3 | — | 4 | new cohesive helper/function in scoped file |
| `obs_engine::should_discard_media_batch` | `engine/runtime_media_v2.cpp` | — | 3 | — | 4 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::start` | `engine/runtime.cpp` | 14 | 3 | 63 | 4 | reduced by 11 |
| `setup_fixture` | `engine/properties_test.cpp` | — | 2 | — | 49 | new cohesive helper/function in scoped file |
| `Add-ScopeMarkdown` | `tools/check-complexity.ps1` | — | 2 | — | 37 | new cohesive helper/function in scoped file |
| `New-Fixture` | `tools/check-complexity.tests.ps1` | — | 2 | — | 37 | new cohesive helper/function in scoped file |
| `Add-ComparisonScopeSummary` | `tools/check-complexity.ps1` | — | 2 | — | 29 | new cohesive helper/function in scoped file |
| `Get-WorkflowPowerShellFeatures` | `tools/check-complexity.ps1` | — | 2 | — | 27 | new cohesive helper/function in scoped file |
| `Invoke-ComplexityChecker` | `tools/check-complexity.tests.ps1` | — | 2 | — | 27 | new cohesive helper/function in scoped file |
| `Invoke-Task9UnsupportedCapability` | `.github/scripts/engine-protocol-v2-task9.ps1` | — | 2 | — | 26 | new cohesive helper/function in scoped file |
| `obs_engine::collect_source_state_events` | `engine/runtime_source_v2.cpp` | — | 2 | — | 24 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::reset_video` | `engine/runtime.cpp` | — | 2 | — | 23 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::command_hello` | `engine/runtime.cpp` | — | 2 | — | 23 | new cohesive helper/function in scoped file |
| `Run-CaseD` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 2 | 2 | 20 | 20 | unchanged |
| `Remove-Task10Sources` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 2 | — | 19 | new cohesive helper/function in scoped file |
| `Invoke-Task11Overflow` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 2 | — | 19 | new cohesive helper/function in scoped file |
| `obs_engine::advance_filter_update_generation` | `engine/runtime_filter_v2.cpp` | — | 2 | — | 19 | new cohesive helper/function in scoped file |
| `Invoke-Task11PackageAudit` | `.github/scripts/engine-protocol-v2-task11-package-audit.ps1` | — | 2 | — | 18 | new cohesive helper/function in scoped file |
| `Remove-Task11DuplicateSource` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 2 | — | 18 | new cohesive helper/function in scoped file |
| `New-TestSource` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 2 | 2 | 18 | 18 | unchanged |
| `Add-ComparisonMetricSection` | `tools/check-complexity.ps1` | — | 2 | — | 18 | new cohesive helper/function in scoped file |
| `obs_engine::send_v2_ok` | `engine/protocol_v2.cpp` | 2 | 2 | 17 | 17 | unchanged |
| `Read-TrustedAcceptedBaseline` | `tools/check-complexity.ps1` | — | 2 | — | 17 | new cohesive helper/function in scoped file |
| `New-WorkflowReportPolicy` | `tools/check-complexity.ps1` | — | 2 | — | 17 | new cohesive helper/function in scoped file |
| `New-IfChain` | `tools/check-complexity.tests.ps1` | — | 2 | — | 17 | new cohesive helper/function in scoped file |
| `Invoke-Task10PackageAudit` | `.github/scripts/engine-protocol-v2-task10-package-audit.ps1` | — | 2 | — | 16 | new cohesive helper/function in scoped file |
| `Run-CaseF` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 2 | 2 | 16 | 16 | unchanged |
| `New-TestSource` | `.github/scripts/engine-protocol-v2-task11.ps1` | 2 | 2 | 15 | 15 | unchanged |
| `Run-CaseB` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 2 | 2 | 15 | 15 | unchanged |
| `Run-RaceScenario` | `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | 15 | 2 | 98 | 14 | reduced by 13 |
| `Invoke-Task11BlockingUpdate` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 2 | — | 14 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_media_restart` | `engine/runtime_media_v2.cpp` | 6 | 2 | 24 | 14 | reduced by 4 |
| `obs_engine::Engine::v2_media_next` | `engine/runtime_media_v2.cpp` | 6 | 2 | 24 | 14 | reduced by 4 |
| `obs_engine::Engine::v2_media_previous` | `engine/runtime_media_v2.cpp` | 6 | 2 | 24 | 14 | reduced by 4 |
| `obs_engine::Engine::v2_properties_refresh` | `engine/runtime_properties_v2.cpp` | 2 | 2 | 14 | 14 | unchanged |
| `obs_engine::Engine::v2_source_get_properties` | `engine/runtime_source_v2.cpp` | 3 | 2 | 15 | 14 | reduced by 1 |
| `obs_engine::Engine::v2_source_refresh` | `engine/runtime_source_v2.cpp` | 3 | 2 | 16 | 14 | reduced by 1 |
| `Create-Source` | `.github/scripts/engine-protocol-v2-task10.ps1` | 2 | 2 | 13 | 13 | unchanged |
| `obs_engine::Engine::v2_filter_kind_properties` | `engine/runtime_filter_v2.cpp` | 5 | 2 | 17 | 13 | reduced by 3 |
| `obs_engine::ensure_interaction_state` | `engine/runtime_interaction_v2.cpp` | 2 | 2 | 13 | 13 | unchanged |
| `obs_engine::Engine::v2_media_get_duration` | `engine/runtime_media_v2.cpp` | 4 | 2 | 17 | 13 | reduced by 2 |
| `obs_engine::Engine::v2_media_get_position` | `engine/runtime_media_v2.cpp` | 4 | 2 | 17 | 13 | reduced by 2 |
| `obs_engine::Engine::v2_properties_get` | `engine/runtime_properties_v2.cpp` | 2 | 2 | 13 | 13 | unchanged |
| `obs_engine::Engine::v2_source_get_active` | `engine/runtime_source_v2.cpp` | 3 | 2 | 15 | 13 | reduced by 1 |
| `obs_engine::Engine::v2_source_get_showing` | `engine/runtime_source_v2.cpp` | 3 | 2 | 15 | 13 | reduced by 1 |
| `Assert-ExactJsonShape` | `tools/check-complexity.ps1` | — | 2 | — | 13 | new cohesive helper/function in scoped file |
| `Trust-FixtureBaseline` | `tools/check-complexity.tests.ps1` | — | 2 | — | 13 | new cohesive helper/function in scoped file |
| `obs_engine::release_interaction_keys` | `engine/runtime_interaction_v2.cpp` | — | 2 | — | 12 | new cohesive helper/function in scoped file |
| `obs_source_dosignal_update` | `libobs/obs-internal.h` | 2 | 2 | 12 | 12 | unchanged |
| `Get-OperatorPredicate` | `tools/check-complexity.ps1` | — | 2 | — | 12 | new cohesive helper/function in scoped file |
| `Invoke-FixtureGit` | `tools/check-complexity.tests.ps1` | — | 2 | — | 12 | new cohesive helper/function in scoped file |
| `Write-MigrationDocument` | `tools/check-complexity.tests.ps1` | — | 2 | — | 12 | new cohesive helper/function in scoped file |
| `obs_engine::make_capabilities_array` | `engine/protocol_filter_v2.cpp` | 2 | 2 | 11 | 11 | unchanged |
| `obs_engine::make_capabilities_array` | `engine/protocol_v2.cpp` | 2 | 2 | 11 | 11 | unchanged |
| `obs_engine::set_subscriptions` | `engine/protocol_v2.cpp` | 2 | 2 | 11 | 11 | unchanged |
| `obs_engine::Engine::v2_filter_get` | `engine/runtime_filter_v2.cpp` | 4 | 2 | 16 | 11 | reduced by 2 |
| `Get-ObjectPropertyValue` | `tools/check-complexity.ps1` | — | 2 | — | 11 | new cohesive helper/function in scoped file |
| `Get-IdentityMigrationTargetMetric` | `tools/check-complexity.ps1` | — | 2 | — | 11 | new cohesive helper/function in scoped file |
| `Add-ComparisonSummary` | `tools/check-complexity.ps1` | — | 2 | — | 11 | new cohesive helper/function in scoped file |
| `New-CodePathContext` | `tools/check-complexity.ps1` | — | 2 | — | 11 | new cohesive helper/function in scoped file |
| `obs_engine::append_filter_rename_event` | `engine/runtime_filter_v2.cpp` | — | 2 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_media_get_state` | `engine/runtime_media_v2.cpp` | 4 | 2 | 16 | 10 | reduced by 2 |
| `obs_engine::finalize_property_target` | `engine/runtime_properties_v2.cpp` | — | 2 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_store_source_duplicate` | `engine/runtime_source_v2.cpp` | — | 2 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_begin_event_capture` | `engine/runtime_source_v2.cpp` | 2 | 2 | 10 | 10 | unchanged |
| `obs_engine::Engine::v2_source_get` | `engine/runtime_source_v2.cpp` | 3 | 2 | 12 | 10 | reduced by 1 |
| `obs_engine::Engine::v2_source_get_flags` | `engine/runtime_source_v2.cpp` | 3 | 2 | 12 | 10 | reduced by 1 |
| `obs_engine::Engine::v2_source_get_dimensions` | `engine/runtime_source_v2.cpp` | 3 | 2 | 12 | 10 | reduced by 1 |
| `obs_engine::Engine::v2_source_save_state` | `engine/runtime_source_v2.cpp` | 3 | 2 | 12 | 10 | reduced by 1 |
| `Assert-TrustedBaselineBlob` | `tools/check-complexity.ps1` | — | 2 | — | 10 | new cohesive helper/function in scoped file |
| `Normalize-RepoPath` | `tools/check-complexity.ps1` | — | 2 | — | 10 | new cohesive helper/function in scoped file |
| `Assert-IdentityMigrationNotStale` | `tools/check-complexity.ps1` | — | 2 | — | 10 | new cohesive helper/function in scoped file |
| `Get-StatisticsMetrics` | `tools/check-complexity.ps1` | — | 2 | — | 10 | new cohesive helper/function in scoped file |
| `Assert-CheckerPass` | `tools/check-complexity.tests.ps1` | — | 2 | — | 10 | new cohesive helper/function in scoped file |
| `New-PowerShellTopLevelIfChain` | `tools/check-complexity.tests.ps1` | — | 2 | — | 10 | new cohesive helper/function in scoped file |
| `Read-PendingEvent` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 2 | — | 9 | new cohesive helper/function in scoped file |
| `Read-PendingEvent` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 2 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::EventDispatcher::start` | `engine/events.cpp` | 2 | 2 | 9 | 9 | unchanged |
| `obs_engine::EventDispatcher::invalidate_queued_events_locked` | `engine/events.cpp` | — | 2 | — | 9 | new cohesive helper/function in scoped file |
| `field_is_string` | `engine/host.cpp` | 2 | 2 | 9 | 9 | unchanged |
| `check_validation` | `engine/properties_test.cpp` | — | 2 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::send_ok` | `engine/protocol.cpp` | 2 | 2 | 9 | 9 | unchanged |
| `obs_engine::RevisionState::commit_mutation_unlocked` | `engine/revision.hpp` | 2 | 2 | 9 | 9 | unchanged |
| `obs_engine::append_filter_enabled_event` | `engine/runtime_filter_v2.cpp` | — | 2 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::connect_filter_observer` | `engine/runtime_filter_v2.cpp` | 2 | 2 | 9 | 9 | unchanged |
| `obs_engine::release_interaction_mouse_button` | `engine/runtime_interaction_v2.cpp` | — | 2 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::media_restart_cb` | `engine/runtime_media_v2.cpp` | 2 | 2 | 9 | 9 | unchanged |
| `obs_engine::media_previous_cb` | `engine/runtime_media_v2.cpp` | 2 | 2 | 9 | 9 | unchanged |
| `obs_engine::append_source_active_event` | `engine/runtime_source_v2.cpp` | — | 2 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::append_source_showing_event` | `engine/runtime_source_v2.cpp` | — | 2 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::v2_drain_deferred_source_events` | `engine/runtime_source_v2.cpp` | 2 | 2 | 9 | 9 | unchanged |
| `obs_engine::Engine::v2_flush_deferred_source_events` | `engine/runtime_source_v2.cpp` | 2 | 2 | 9 | 9 | unchanged |
| `Get-WorkflowPaths` | `tools/check-complexity.ps1` | — | 2 | — | 9 | new cohesive helper/function in scoped file |
| `Get-AllFileLines` | `tools/check-complexity.ps1` | — | 2 | — | 9 | new cohesive helper/function in scoped file |
| `Invoke-Task9PackageAudit` | `.github/scripts/engine-protocol-v2-task9-package-audit.ps1` | — | 2 | — | 8 | new cohesive helper/function in scoped file |
| `has_field` | `engine/host.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `check_property` | `engine/properties_test.cpp` | — | 2 | — | 8 | new cohesive helper/function in scoped file |
| `obs_engine::schema_fingerprint` | `engine/properties.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::reject_guard_on_read` | `engine/protocol_filter_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::filter_update_cb` | `engine/runtime_filter_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::filter_rename_cb` | `engine/runtime_filter_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::filter_enabled_cb` | `engine/runtime_filter_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::prune_existing_interaction_state` | `engine/runtime_interaction_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::media_play_cb` | `engine/runtime_media_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::media_pause_cb` | `engine/runtime_media_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::media_stop_cb` | `engine/runtime_media_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::media_next_cb` | `engine/runtime_media_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::media_time_cb` | `engine/runtime_media_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::media_started_cb` | `engine/runtime_media_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::media_ended_cb` | `engine/runtime_media_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::source_update_cb` | `engine/runtime_source_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::source_rename_cb` | `engine/runtime_source_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::source_active_cb` | `engine/runtime_source_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::source_showing_cb` | `engine/runtime_source_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `obs_engine::source_flags_cb` | `engine/runtime_source_v2.cpp` | 2 | 2 | 8 | 8 | unchanged |
| `Invoke-GitLines` | `tools/check-complexity.ps1` | — | 2 | — | 8 | new cohesive helper/function in scoped file |
| `Resolve-Commit` | `tools/check-complexity.ps1` | — | 2 | — | 8 | new cohesive helper/function in scoped file |
| `Test-AstType` | `tools/check-complexity.ps1` | — | 2 | — | 8 | new cohesive helper/function in scoped file |
| `Invoke-Task10FixtureBuild` | `.github/scripts/engine-protocol-v2-task10-build-fixture.ps1` | — | 2 | — | 7 | new cohesive helper/function in scoped file |
| `Invoke-Task11FixtureBuild` | `.github/scripts/engine-protocol-v2-task11-build-fixture.ps1` | — | 2 | — | 7 | new cohesive helper/function in scoped file |
| `Invoke-Task8ConcurrencyFixtureBuild` | `.github/scripts/engine-protocol-v2-task8-concurrency-build-fixture.ps1` | — | 2 | — | 7 | new cohesive helper/function in scoped file |
| `Invoke-Task9FixtureBuild` | `.github/scripts/engine-protocol-v2-task9-build-fixture.ps1` | — | 2 | — | 7 | new cohesive helper/function in scoped file |
| `require` | `engine/events_test.cpp` | 2 | 2 | 7 | 7 | unchanged |
| `harden_dll_search_path` | `engine/host.cpp` | 2 | 2 | 7 | 7 | unchanged |
| `button_clicked` | `engine/properties_test.cpp` | 2 | 2 | 7 | 7 | unchanged |
| `obs_engine::serialize_button_property` | `engine/properties.cpp` | — | 2 | — | 7 | new cohesive helper/function in scoped file |
| `obs_engine::FilterCaptureScope::flush` | `engine/protocol_filter_v2.cpp` | 2 | 2 | 7 | 7 | unchanged |
| `obs_engine::RuntimeEventCaptureScope::flush` | `engine/protocol_v2.cpp` | 2 | 2 | 7 | 7 | unchanged |
| `obs_engine::erase_interaction_state` | `engine/runtime_interaction_v2.cpp` | — | 2 | — | 7 | new cohesive helper/function in scoped file |
| `obs_engine::media_state_name` | `engine/runtime_media_v2.cpp` | 9 | 2 | 23 | 7 | reduced by 7 |
| `obs_engine::Engine::v2_drain_deferred_media_events` | `engine/runtime_media_v2.cpp` | 2 | 2 | 7 | 7 | unchanged |
| `obs_engine::Engine::v2_flush_deferred_media_events` | `engine/runtime_media_v2.cpp` | 2 | 2 | 7 | 7 | unchanged |
| `Get-RepoFilePath` | `tools/check-complexity.ps1` | — | 2 | — | 7 | new cohesive helper/function in scoped file |
| `ConvertTo-FunctionRangeArray` | `tools/check-complexity.ps1` | — | 2 | — | 7 | new cohesive helper/function in scoped file |
| `Get-IdentityDisappearanceMessage` | `tools/check-complexity.ps1` | — | 2 | — | 7 | new cohesive helper/function in scoped file |
| `Assert-NoQueuedEvents` | `.github/scripts/engine-protocol-v2-task11.ps1` | 2 | 2 | 6 | 6 | unchanged |
| `obs_engine::EventDispatcher::enqueue_event_locked` | `engine/events.cpp` | — | 2 | — | 6 | new cohesive helper/function in scoped file |
| `check_sanitized_settings` | `engine/properties_test.cpp` | — | 2 | — | 6 | new cohesive helper/function in scoped file |
| `build_schema` | `engine/properties_test.cpp` | — | 2 | — | 6 | new cohesive helper/function in scoped file |
| `obs_engine::serialize_properties` | `engine/properties.cpp` | 2 | 2 | 6 | 6 | unchanged |
| `obs_engine::find_filter_observer` | `engine/runtime_filter_v2.cpp` | — | 2 | — | 6 | new cohesive helper/function in scoped file |
| `obs_engine::is_action_signal` | `engine/runtime_media_v2.cpp` | 10 | 2 | 17 | 6 | reduced by 8 |
| `obs_engine::action_requires_resync` | `engine/runtime_media_v2.cpp` | 10 | 2 | 17 | 6 | reduced by 8 |
| `obs_engine::append_source_flags_event` | `engine/runtime_source_v2.cpp` | — | 2 | — | 6 | new cohesive helper/function in scoped file |
| `capture_only_mode` | `plugins/win-capture/plugin-main.c` | 2 | 2 | 6 | 6 | unchanged |
| `Assert-RaceEvent` | `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | — | 2 | — | 5 | new cohesive helper/function in scoped file |
| `Assert-CanonicalHandle` | `.github/scripts/engine-protocol-v2-task11.ps1` | 2 | 2 | 5 | 5 | unchanged |
| `Assert-Task6CanonicalHandle` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 2 | — | 5 | new cohesive helper/function in scoped file |
| `ProtocolWriterScope::~ProtocolWriterScope` | `engine/host.cpp` | 2 | 2 | 5 | 5 | unchanged |
| `PropertyFixture::~PropertyFixture` | `engine/properties_test.cpp` | — | 2 | — | 5 | new cohesive helper/function in scoped file |
| `obs_engine::list_string_value_matches` | `engine/properties.cpp` | — | 2 | — | 5 | new cohesive helper/function in scoped file |
| `obs_engine::list_bool_value_matches` | `engine/properties.cpp` | — | 2 | — | 5 | new cohesive helper/function in scoped file |
| `obs_engine::validate_bool_property` | `engine/properties.cpp` | — | 2 | — | 5 | new cohesive helper/function in scoped file |
| `obs_engine::validate_string_property` | `engine/properties.cpp` | — | 2 | — | 5 | new cohesive helper/function in scoped file |
| `obs_engine::validate_object_property` | `engine/properties.cpp` | — | 2 | — | 5 | new cohesive helper/function in scoped file |
| `obs_engine::validate_array_property` | `engine/properties.cpp` | — | 2 | — | 5 | new cohesive helper/function in scoped file |
| `obs_engine::FilterCaptureScope::~FilterCaptureScope` | `engine/protocol_filter_v2.cpp` | 2 | 2 | 5 | 5 | unchanged |
| `obs_engine::RuntimeEventCaptureScope::~RuntimeEventCaptureScope` | `engine/protocol_v2.cpp` | 2 | 2 | 5 | 5 | unchanged |
| `obs_engine::write_json` | `engine/protocol.cpp` | 2 | 2 | 5 | 5 | unchanged |
| `obs_engine::ObsDataDeleter::operator ( )` | `engine/protocol.hpp` | 2 | 2 | 5 | 5 | unchanged |
| `obs_engine::ObsArrayDeleter::operator ( )` | `engine/protocol.hpp` | 2 | 2 | 5 | 5 | unchanged |
| `obs_engine::Engine::v2_drain_deferred_filter_events` | `engine/runtime_filter_v2.cpp` | 2 | 2 | 5 | 5 | unchanged |
| `obs_engine::Engine::v2_flush_deferred_filter_events` | `engine/runtime_filter_v2.cpp` | 2 | 2 | 5 | 5 | unchanged |
| `obs_engine::ObsPropertiesDeleter::operator ( )` | `engine/runtime_properties_v2.cpp` | 2 | 2 | 5 | 5 | unchanged |
| `obs_engine::Engine::PropertyButtonContext::~PropertyButtonContext` | `engine/runtime_properties_v2.cpp` | — | 2 | — | 5 | new cohesive helper/function in scoped file |
| `Get-WorkflowReportBlocks` | `tools/check-complexity.ps1` | — | 2 | — | 5 | new cohesive helper/function in scoped file |
| `obs_engine::safe_string` | `engine/properties.cpp` | 2 | 2 | 4 | 4 | unchanged |
| `obs_engine::number_type_name` | `engine/properties.cpp` | 2 | 2 | 4 | 4 | unchanged |
| `obs_engine::group_type_name` | `engine/properties.cpp` | 2 | 2 | 4 | 4 | unchanged |
| `obs_engine::button_type_name` | `engine/properties.cpp` | 2 | 2 | 4 | 4 | unchanged |
| `obs_engine::is_number` | `engine/properties.cpp` | 2 | 2 | 4 | 4 | unchanged |
| `obs_engine::is_integer` | `engine/properties.cpp` | 2 | 2 | 4 | 4 | unchanged |
| `obs_engine::list_int_value_matches` | `engine/properties.cpp` | — | 2 | — | 4 | new cohesive helper/function in scoped file |
| `obs_engine::list_float_value_matches` | `engine/properties.cpp` | — | 2 | — | 4 | new cohesive helper/function in scoped file |
| `obs_engine::needs_settings_settle` | `engine/protocol_filter_v2.cpp` | 2 | 2 | 4 | 4 | unchanged |
| `obs_engine::commit_filter_resync_revision` | `engine/runtime_filter_v2.cpp` | — | 2 | — | 4 | new cohesive helper/function in scoped file |
| `obs_engine::MediaActionKey::operator ==` | `engine/runtime_media_v2.cpp` | 2 | 2 | 4 | 4 | unchanged |
| `obs_engine::commit_media_resync_revision` | `engine/runtime_media_v2.cpp` | — | 2 | — | 4 | new cohesive helper/function in scoped file |
| `obs_engine::is_bounded_string` | `engine/runtime_properties_v2.cpp` | 2 | 2 | 4 | 4 | unchanged |
| `obs_source_media_play_pause` | `libobs/obs-source.c` | 2 | 2 | 4 | 4 | unchanged |
| `Get-EnforcedScopeMetrics` | `tools/check-complexity.ps1` | — | 2 | — | 4 | new cohesive helper/function in scoped file |
| `Get-EnforcedBaselineMetrics` | `tools/check-complexity.ps1` | — | 2 | — | 4 | new cohesive helper/function in scoped file |
| `Test-AnalyzablePath` | `tools/check-complexity.ps1` | — | 2 | — | 4 | new cohesive helper/function in scoped file |
| `Assert-Task8CanonicalHandle` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 2 | — | 3 | new cohesive helper/function in scoped file |
| `New-ComparisonMarkdown` | `tools/check-complexity.ps1` | — | 1 | — | 50 | new cohesive helper/function in scoped file |
| `Invoke-Task11OrderMutations` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 1 | — | 49 | new cohesive helper/function in scoped file |
| `New-Task1SceneObjects` | `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1` | — | 1 | — | 41 | new cohesive helper/function in scoped file |
| `New-ReportObject` | `tools/check-complexity.ps1` | — | 1 | — | 39 | new cohesive helper/function in scoped file |
| `New-InventoryContext` | `tools/check-complexity.ps1` | — | 1 | — | 39 | new cohesive helper/function in scoped file |
| `New-ComplexityRunContext` | `tools/check-complexity.ps1` | — | 1 | — | 30 | new cohesive helper/function in scoped file |
| `obs_module_load` | `engine/task8_concurrency_source.cpp` | 1 | 1 | 27 | 27 | unchanged |
| `Initialize-RaceScenario` | `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | — | 1 | — | 22 | new cohesive helper/function in scoped file |
| `New-DuplicateMigrationDocument` | `tools/check-complexity.tests.ps1` | — | 1 | — | 21 | new cohesive helper/function in scoped file |
| `Invoke-Task10RemovalOutstanding` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 1 | — | 20 | new cohesive helper/function in scoped file |
| `obs_engine::set_semantic_source_flags` | `engine/runtime_source_settle_v2.cpp` | 1 | 1 | 20 | 20 | unchanged |
| `obs_engine::set_semantic_source_flags` | `engine/runtime_source_v2.cpp` | 1 | 1 | 20 | 20 | unchanged |
| `New-WorkflowDocument` | `tools/check-complexity.tests.ps1` | — | 1 | — | 20 | new cohesive helper/function in scoped file |
| `New-MigrationRecordValues` | `tools/check-complexity.tests.ps1` | — | 1 | — | 20 | new cohesive helper/function in scoped file |
| `Invoke-Task11Scenario` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 1 | — | 18 | new cohesive helper/function in scoped file |
| `Invoke-Task2RequestScenarios` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 1 | — | 18 | new cohesive helper/function in scoped file |
| `obs_engine::send_session_hello` | `engine/protocol_v2.cpp` | — | 1 | — | 18 | new cohesive helper/function in scoped file |
| `obs_engine::make_transform_data` | `engine/runtime_v2.cpp` | 1 | 1 | 18 | 18 | unchanged |
| `Format-ScopeStatTable` | `tools/check-complexity.ps1` | — | 1 | — | 18 | new cohesive helper/function in scoped file |
| `Invoke-Task11LateTimeouts` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 1 | — | 16 | new cohesive helper/function in scoped file |
| `Run-CaseC` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 1 | 1 | 16 | 16 | unchanged |
| `New-MigrationRecord` | `tools/check-complexity.tests.ps1` | — | 1 | — | 16 | new cohesive helper/function in scoped file |
| `Format-StatTable` | `tools/check-complexity.ps1` | — | 1 | — | 15 | new cohesive helper/function in scoped file |
| `obs_engine::print_help` | `engine/config.cpp` | 1 | 1 | 14 | 14 | unchanged |
| `obs_engine::make_source_summary` | `engine/runtime_source_v2.cpp` | 1 | 1 | 14 | 14 | unchanged |
| `Get-ComparisonLabel` | `tools/check-complexity.ps1` | — | 1 | — | 14 | new cohesive helper/function in scoped file |
| `Invoke-Task10Scenario` | `.github/scripts/engine-protocol-v2-task10.ps1` | — | 1 | — | 13 | new cohesive helper/function in scoped file |
| `Invoke-RaceTimeout` | `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | — | 1 | — | 13 | new cohesive helper/function in scoped file |
| `New-Task11State` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 1 | — | 13 | new cohesive helper/function in scoped file |
| `Invoke-Task8SourceScenario` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 1 | — | 13 | new cohesive helper/function in scoped file |
| `send_ready_event` | `engine/host.cpp` | 1 | 1 | 13 | 13 | unchanged |
| `obs_engine::make_filter_summary` | `engine/runtime_filter_v2.cpp` | 1 | 1 | 13 | 13 | unchanged |
| `obs_engine::connect_media_observer` | `engine/runtime_media_v2.cpp` | 1 | 1 | 13 | 13 | unchanged |
| `Commit-FixtureRename` | `tools/check-complexity.tests.ps1` | — | 1 | — | 13 | new cohesive helper/function in scoped file |
| `Run-CaseA` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 1 | 1 | 12 | 12 | unchanged |
| `obs_engine::connect_observer` | `engine/runtime_source_v2.cpp` | 1 | 1 | 11 | 11 | unchanged |
| `Get-FunctionRanges` | `tools/check-complexity.ps1` | — | 1 | — | 11 | new cohesive helper/function in scoped file |
| `Get-SortedScopeMetrics` | `tools/check-complexity.ps1` | — | 1 | — | 11 | new cohesive helper/function in scoped file |
| `Commit-FixtureFile` | `tools/check-complexity.tests.ps1` | — | 1 | — | 11 | new cohesive helper/function in scoped file |
| `Invoke-Task3UnsupportedQuery` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 1 | — | 10 | new cohesive helper/function in scoped file |
| `Invoke-Task6ProtocolScenario` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 1 | — | 10 | new cohesive helper/function in scoped file |
| `obs_engine::handle_session_close` | `engine/protocol_v2.cpp` | — | 1 | — | 10 | new cohesive helper/function in scoped file |
| `obs_module_load` | `engine/task10_media_source.cpp` | 1 | 1 | 10 | 10 | unchanged |
| `obs_source_dosignal_media_action` | `libobs/obs-internal.h` | 1 | 1 | 10 | 10 | unchanged |
| `Get-FunctionKey` | `tools/check-complexity.ps1` | — | 1 | — | 10 | new cohesive helper/function in scoped file |
| `Assert-FunctionMeasured` | `tools/check-complexity.tests.ps1` | — | 1 | — | 10 | new cohesive helper/function in scoped file |
| `Initialize-Task1Diagnostics` | `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1` | — | 1 | — | 9 | new cohesive helper/function in scoped file |
| `Invoke-Task5ProtocolScenario` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 1 | — | 9 | new cohesive helper/function in scoped file |
| `Invoke-Task8Cases` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | — | 1 | — | 9 | new cohesive helper/function in scoped file |
| `obs_engine::EventDispatcher::emit_resync_required` | `engine/events.cpp` | 1 | 1 | 9 | 9 | unchanged |
| `obs_engine::send_error` | `engine/protocol.cpp` | 1 | 1 | 9 | 9 | unchanged |
| `obs_engine::source_update_settle_cb` | `engine/runtime_source_settle_v2.cpp` | 1 | 1 | 9 | 9 | unchanged |
| `obs_engine::make_source_duplicate_data` | `engine/runtime_source_v2.cpp` | — | 1 | — | 9 | new cohesive helper/function in scoped file |
| `Assert-Task11InitialOrder` | `.github/scripts/engine-protocol-v2-task11.ps1` | — | 1 | — | 8 | new cohesive helper/function in scoped file |
| `Invoke-Task4ProtocolScenario` | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | — | 1 | — | 8 | new cohesive helper/function in scoped file |
| `Invoke-Task9Scenario` | `.github/scripts/engine-protocol-v2-task9.ps1` | — | 1 | — | 8 | new cohesive helper/function in scoped file |
| `obs_engine::serialize_int_property` | `engine/properties.cpp` | — | 1 | — | 8 | new cohesive helper/function in scoped file |
| `obs_engine::serialize_float_property` | `engine/properties.cpp` | — | 1 | — | 8 | new cohesive helper/function in scoped file |
| `obs_engine::serialize_list_property` | `engine/properties.cpp` | — | 1 | — | 8 | new cohesive helper/function in scoped file |
| `obs_engine::add_validation_issue` | `engine/properties.cpp` | 1 | 1 | 8 | 8 | unchanged |
| `obs_engine::make_filter_settings_result` | `engine/runtime_filter_v2.cpp` | — | 1 | — | 8 | new cohesive helper/function in scoped file |
| `obs_engine::make_dimensions_data` | `engine/runtime_source_v2.cpp` | 1 | 1 | 8 | 8 | unchanged |
| `obs_engine::make_item_identity` | `engine/runtime_v2.cpp` | 1 | 1 | 8 | 8 | unchanged |
| `Test-PathAtRef` | `tools/check-complexity.ps1` | — | 1 | — | 8 | new cohesive helper/function in scoped file |
| `Send-Task2Request` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 1 | — | 7 | new cohesive helper/function in scoped file |
| `Send-Task3Request` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 1 | — | 7 | new cohesive helper/function in scoped file |
| `Send-Task4Request` | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | — | 1 | — | 7 | new cohesive helper/function in scoped file |
| `Send-Task5Request` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 1 | — | 7 | new cohesive helper/function in scoped file |
| `Invoke-Task7KindSchema` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 1 | — | 7 | new cohesive helper/function in scoped file |
| `Invoke-Task7ReadOnlyKindChecks` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 1 | — | 7 | new cohesive helper/function in scoped file |
| `Invoke-Task8BridgeAudit` | `.github/scripts/engine-protocol-v2-task8-concurrency-bridge-audit.ps1` | — | 1 | — | 7 | new cohesive helper/function in scoped file |
| `SourceEventBridgeScope::SourceEventBridgeScope` | `engine/host.cpp` | 1 | 1 | 7 | 7 | unchanged |
| `obs_log_handler` | `engine/host.cpp` | 1 | 1 | 7 | 7 | unchanged |
| `obs_engine::set_frame_rate` | `engine/properties.cpp` | 1 | 1 | 7 | 7 | unchanged |
| `obs_engine::serialize_group_property` | `engine/properties.cpp` | — | 1 | — | 7 | new cohesive helper/function in scoped file |
| `obs_engine::handle_engine_capabilities` | `engine/protocol_v2.cpp` | — | 1 | — | 7 | new cohesive helper/function in scoped file |
| `obs_engine::make_filter_event_data` | `engine/runtime_filter_v2.cpp` | — | 1 | — | 7 | new cohesive helper/function in scoped file |
| `obs_engine::result_matches_filter_update` | `engine/runtime_filter_v2.cpp` | 1 | 1 | 7 | 7 | unchanged |
| `obs_engine::make_state_data` | `engine/runtime_media_v2.cpp` | 1 | 1 | 7 | 7 | unchanged |
| `obs_engine::make_action_data` | `engine/runtime_media_v2.cpp` | 1 | 1 | 7 | 7 | unchanged |
| `obs_engine::append_event` | `engine/runtime_source_v2.cpp` | 1 | 1 | 7 | 7 | unchanged |
| `obs_engine::make_flags_data` | `engine/runtime_source_v2.cpp` | 1 | 1 | 7 | 7 | unchanged |
| `obs_engine::append_event` | `engine/runtime_v2.cpp` | 1 | 1 | 7 | 7 | unchanged |
| `obs_module_load` | `engine/task11_filter_source.cpp` | 1 | 1 | 7 | 7 | unchanged |
| `obs_module_unload` | `plugins/win-capture/plugin-main.c` | 1 | 1 | 7 | 7 | unchanged |
| `New-EmptyIdentityMaps` | `tools/check-complexity.ps1` | — | 1 | — | 7 | new cohesive helper/function in scoped file |
| `New-PathExclusionResult` | `tools/check-complexity.ps1` | — | 1 | — | 7 | new cohesive helper/function in scoped file |
| `Prepare-FixtureBaseline` | `tools/check-complexity.tests.ps1` | — | 1 | — | 7 | new cohesive helper/function in scoped file |
| `Invoke-Task1ProtocolV1Scenario` | `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1` | — | 1 | — | 6 | new cohesive helper/function in scoped file |
| `Invoke-Task11CoreAudit` | `.github/scripts/engine-protocol-v2-task11-core-audit.ps1` | — | 1 | — | 6 | new cohesive helper/function in scoped file |
| `Initialize-Task2Diagnostics` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 1 | — | 6 | new cohesive helper/function in scoped file |
| `Initialize-Task3Diagnostics` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 1 | — | 6 | new cohesive helper/function in scoped file |
| `Invoke-Task3CapabilityQuery` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 1 | — | 6 | new cohesive helper/function in scoped file |
| `Invoke-Task3ProtocolScenario` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 1 | — | 6 | new cohesive helper/function in scoped file |
| `Initialize-Task4Diagnostics` | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | — | 1 | — | 6 | new cohesive helper/function in scoped file |
| `Initialize-Task5Diagnostics` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 1 | — | 6 | new cohesive helper/function in scoped file |
| `Assert-Ping` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 1 | 1 | 6 | 6 | unchanged |
| `obs_engine::collect_sensitive_property_names` | `engine/properties_sensitive.cpp` | 1 | 1 | 6 | 6 | unchanged |
| `obs_engine::serialize_path_property` | `engine/properties.cpp` | — | 1 | — | 6 | new cohesive helper/function in scoped file |
| `obs_engine::serialize_editable_list_property` | `engine/properties.cpp` | — | 1 | — | 6 | new cohesive helper/function in scoped file |
| `obs_engine::set_parse_error` | `engine/protocol_v2.cpp` | 1 | 1 | 6 | 6 | unchanged |
| `obs_engine::send_subscription_state` | `engine/protocol_v2.cpp` | 1 | 1 | 6 | 6 | unchanged |
| `obs_engine::send_session_ping` | `engine/protocol_v2.cpp` | — | 1 | — | 6 | new cohesive helper/function in scoped file |
| `obs_engine::ProtocolWriter::write_direct` | `engine/protocol.cpp` | 1 | 1 | 6 | 6 | unchanged |
| `obs_engine::clear_deferred_filter_events` | `engine/runtime_filter_v2.cpp` | 1 | 1 | 6 | 6 | unchanged |
| `obs_engine::make_source_result` | `engine/runtime_interaction_v2.cpp` | 1 | 1 | 6 | 6 | unchanged |
| `obs_engine::MediaActionKeyHash::operator ( )` | `engine/runtime_media_v2.cpp` | 1 | 1 | 6 | 6 | unchanged |
| `obs_engine::clear_deferred_media_events` | `engine/runtime_media_v2.cpp` | 1 | 1 | 6 | 6 | unchanged |
| `obs_engine::clear_deferred_source_events` | `engine/runtime_source_v2.cpp` | 1 | 1 | 6 | 6 | unchanged |
| `obs_engine::make_source_event_data` | `engine/runtime_source_v2.cpp` | — | 1 | — | 6 | new cohesive helper/function in scoped file |
| `obs_engine::Engine::release_item` | `engine/runtime.cpp` | 1 | 1 | 6 | 6 | unchanged |
| `Get-ScriptBodyWarnings` | `tools/check-complexity.ps1` | — | 1 | — | 6 | new cohesive helper/function in scoped file |
| `Invoke-Task2ProtocolScenario` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 1 | — | 5 | new cohesive helper/function in scoped file |
| `Invoke-Task6SceneSourceSetup` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 1 | — | 5 | new cohesive helper/function in scoped file |
| `Invoke-Task6ItemTransform` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 1 | — | 5 | new cohesive helper/function in scoped file |
| `Invoke-Task6SettingsMutation` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 1 | — | 5 | new cohesive helper/function in scoped file |
| `Invoke-Task8KindMetadata` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 1 | — | 5 | new cohesive helper/function in scoped file |
| `reset_output` | `engine/events_test.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `output_lines` | `engine/events_test.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::write_json_line` | `engine/events_test.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::is_event_name_character` | `engine/events.cpp` | — | 1 | — | 5 | new cohesive helper/function in scoped file |
| `obs_engine::EventDispatcher::subscriptions` | `engine/events.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `ProtocolWriterScope::start` | `engine/host.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `fail` | `engine/properties_test.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::set_capabilities` | `engine/protocol_filter_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::FilterCaptureScope::FilterCaptureScope` | `engine/protocol_filter_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::set_capabilities` | `engine/protocol_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::protocol_writer` | `engine/protocol.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::RevisionState::commit_mutation` | `engine/revision.hpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::reset_result` | `engine/runtime_filter_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::set_handle` | `engine/runtime_filter_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::clear_uncertain_filter_updates` | `engine/runtime_filter_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::Engine::v2_filter_patch_settings` | `engine/runtime_filter_v2.cpp` | 14 | 1 | 44 | 5 | reduced by 13 |
| `obs_engine::Engine::v2_filter_replace_settings` | `engine/runtime_filter_v2.cpp` | 13 | 1 | 42 | 5 | reduced by 12 |
| `obs_engine::reset_result` | `engine/runtime_interaction_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::set_handle` | `engine/runtime_interaction_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::count_pressed_mouse_buttons` | `engine/runtime_interaction_v2.cpp` | — | 1 | — | 5 | new cohesive helper/function in scoped file |
| `obs_engine::reset_result` | `engine/runtime_media_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::set_handle` | `engine/runtime_media_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::reset_result` | `engine/runtime_properties_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::set_handle` | `engine/runtime_properties_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::mark_source_settlement_lost` | `engine/runtime_source_settle_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::reset_result` | `engine/runtime_source_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::set_handle` | `engine/runtime_source_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::reset_result` | `engine/runtime_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::set_handle` | `engine/runtime_v2.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::SourceEventCaptureGate::begin` | `engine/source_event_capture.hpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::SourceEventCaptureGate::end` | `engine/source_event_capture.hpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_module_load` | `engine/task9_interaction_source.cpp` | 1 | 1 | 5 | 5 | unchanged |
| `obs_engine::is_safe_identifier_character` | `engine/validation.hpp` | — | 1 | — | 5 | new cohesive helper/function in scoped file |
| `Read-JsonFile` | `tools/check-complexity.ps1` | — | 1 | — | 5 | new cohesive helper/function in scoped file |
| `Invoke-Task6ItemLifecycle` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 1 | — | 4 | new cohesive helper/function in scoped file |
| `Invoke-Task8KindQueries` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 1 | — | 4 | new cohesive helper/function in scoped file |
| `Invoke-Task8ActivityChecks` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 1 | — | 4 | new cohesive helper/function in scoped file |
| `Invoke-Task8ReplaceAndSave` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 1 | — | 4 | new cohesive helper/function in scoped file |
| `Invoke-Task8ResetAndLoad` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 1 | — | 4 | new cohesive helper/function in scoped file |
| `contains` | `engine/events_test.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::EventDispatcher::~EventDispatcher` | `engine/events.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `SourceEventBridgeScope::~SourceEventBridgeScope` | `engine/host.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::serialize_color_property` | `engine/properties.cpp` | — | 1 | — | 4 | new cohesive helper/function in scoped file |
| `obs_engine::RuntimeEventCaptureScope::RuntimeEventCaptureScope` | `engine/protocol_v2.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::start_protocol_writer` | `engine/protocol.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::stop_protocol_writer` | `engine/protocol.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::write_json_line` | `engine/protocol.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::RevisionState::MutationGuard::current` | `engine/revision.hpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::RevisionState::MutationGuard::matches` | `engine/revision.hpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::RevisionState::MutationGuard::can_commit_mutation` | `engine/revision.hpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::RevisionState::MutationGuard::commit_mutation` | `engine/revision.hpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::RevisionState::current` | `engine/revision.hpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::RevisionState::matches` | `engine/revision.hpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::RevisionState::can_commit_mutation` | `engine/revision.hpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::RevisionState::lock_mutation` | `engine/revision.hpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::RevisionState::max_revision` | `engine/revision.hpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::append_event` | `engine/runtime_filter_v2.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::FilterCallbackScope::accepted` | `engine/runtime_filter_v2.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::FilterCallbackScope::suppressed` | `engine/runtime_filter_v2.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::Engine::v2_filter_move_up` | `engine/runtime_filter_v2.cpp` | 7 | 1 | 26 | 4 | reduced by 6 |
| `obs_engine::Engine::v2_filter_move_down` | `engine/runtime_filter_v2.cpp` | 7 | 1 | 26 | 4 | reduced by 6 |
| `obs_engine::Engine::v2_filter_move_top` | `engine/runtime_filter_v2.cpp` | 7 | 1 | 26 | 4 | reduced by 6 |
| `obs_engine::Engine::v2_filter_move_bottom` | `engine/runtime_filter_v2.cpp` | 7 | 1 | 26 | 4 | reduced by 6 |
| `obs_engine::append_event` | `engine/runtime_media_v2.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::MediaCallbackScope::accepted` | `engine/runtime_media_v2.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::MediaCallbackScope::suppressed` | `engine/runtime_media_v2.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::SourceCallbackScope::accepted` | `engine/runtime_source_v2.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::SourceCallbackScope::suppressed` | `engine/runtime_source_v2.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::Engine::~Engine` | `engine/runtime.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_engine::SourceEventCaptureGate::active` | `engine/source_event_capture.hpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_module_description` | `engine/task8_concurrency_source.cpp` | 1 | 1 | 4 | 4 | unchanged |
| `obs_source_update` | `libobs/obs-source.c` | 1 | 1 | 4 | 4 | unchanged |
| `obs_source_update_tracked` | `libobs/obs-source.c` | 1 | 1 | 4 | 4 | unchanged |
| `obs_source_reset_settings` | `libobs/obs-source.c` | 1 | 1 | 4 | 4 | unchanged |
| `obs_source_reset_settings_tracked` | `libobs/obs-source.c` | 1 | 1 | 4 | 4 | unchanged |
| `obs_source_media_restart` | `libobs/obs-source.c` | 1 | 1 | 4 | 4 | unchanged |
| `obs_source_media_stop` | `libobs/obs-source.c` | 1 | 1 | 4 | 4 | unchanged |
| `obs_source_media_next` | `libobs/obs-source.c` | 1 | 1 | 4 | 4 | unchanged |
| `obs_source_media_previous` | `libobs/obs-source.c` | 1 | 1 | 4 | 4 | unchanged |
| `obs_source_media_set_time` | `libobs/obs-source.c` | 1 | 1 | 4 | 4 | unchanged |
| `Test-CurrentPath` | `tools/check-complexity.ps1` | — | 1 | — | 4 | new cohesive helper/function in scoped file |
| `Assert-UniqueFunctionIdentities` | `tools/check-complexity.ps1` | — | 1 | — | 4 | new cohesive helper/function in scoped file |
| `Get-SortedFunctions` | `tools/check-complexity.ps1` | — | 1 | — | 4 | new cohesive helper/function in scoped file |
| `Get-SortedScriptBodies` | `tools/check-complexity.ps1` | — | 1 | — | 4 | new cohesive helper/function in scoped file |
| `Get-FixtureHead` | `tools/check-complexity.tests.ps1` | — | 1 | — | 4 | new cohesive helper/function in scoped file |
| `Fail` | `.github/scripts/engine-protocol-v2-task10.ps1` | 1 | 1 | 3 | 3 | unchanged |
| `Fail` | `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | 1 | 1 | 3 | 3 | unchanged |
| `Fail` | `.github/scripts/engine-protocol-v2-task11.ps1` | 1 | 1 | 3 | 3 | unchanged |
| `Get-Task3CapabilityNames` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 1 | — | 3 | new cohesive helper/function in scoped file |
| `Get-Task5SubscriptionPatterns` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 1 | — | 3 | new cohesive helper/function in scoped file |
| `Fail` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 1 | 1 | 3 | 3 | unchanged |
| `New-ExactMap` | `tools/check-complexity.ps1` | — | 1 | — | 3 | new cohesive helper/function in scoped file |
| `obs_engine::EventDispatcher::EventDispatcher` | `engine/events.cpp` | 1 | 1 | 1 | 1 | unchanged |
| `obs_engine::RevisionState::MutationGuard::MutationGuard` | `engine/revision.hpp` | 1 | 1 | 1 | 1 | unchanged |
| `obs_engine::Engine::Engine` | `engine/runtime.cpp` | 1 | 1 | 1 | 1 | unchanged |

## PowerShell script-body comparison

| Function | File | Before CC | After CC | Before NLOC | After NLOC | Notes |
|---|---|---:|---:|---:|---:|---|
| `<script-body>` | `tools/check-complexity.tests.ps1` | — | 5 | — | 364 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | 3 | 3 | 22 | 22 | unchanged |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task11.ps1` | 80 | 2 | 605 | 23 | reduced by 78 |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task10.ps1` | 63 | 2 | 443 | 21 | reduced by 61 |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 9 | 2 | 32 | 17 | reduced by 7 |
| `<script-body>` | `tools/check-complexity.ps1` | — | 1 | — | 46 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task9.ps1` | 26 | 1 | 265 | 14 | reduced by 25 |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1` | — | 1 | — | 9 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1` | — | 1 | — | 9 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task2-framing.ps1` | — | 1 | — | 8 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task3-capabilities.ps1` | — | 1 | — | 8 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task4-revisions.ps1` | — | 1 | — | 8 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1` | — | 1 | — | 8 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1` | — | 1 | — | 8 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1` | — | 1 | — | 7 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task11-core-audit.ps1` | — | 1 | — | 3 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task8-concurrency-bridge-audit.ps1` | — | 1 | — | 3 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task1-footprint.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task10-build-fixture.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task10-package-audit.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task10-remove-fixture.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task10-stage-fixture.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task11-build-fixture.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task11-package-audit.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task11-remove-fixture.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task11-stage-fixture.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task5-event-queue-policy.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task7-properties-bridge.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task8-concurrency-build-fixture.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task8-concurrency-capture-routing.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task8-concurrency-run.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task9-build-fixture.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task9-package-audit.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task9-remove-fixture.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |
| `<script-body>` | `.github/scripts/engine-protocol-v2-task9-stage-fixture.ps1` | — | 1 | — | 2 | new cohesive helper/function in scoped file |

## Top remaining functions

- `obs_source_destroy_defer` — `libobs/obs-source.c:813-893`, CC 13, NLOC 65, params 1
- `Assert-Task11LiveFilterReads` — `.github/scripts/engine-protocol-v2-task11.ps1:410-459`, CC 10, NLOC 50, params 1
- `Get-OperatorBlameLines` — `tools/check-complexity.ps1:727-768`, CC 10, NLOC 41, params 1
- `obs_engine::Engine::v2_scene_create` — `engine/runtime_v2.cpp:457-499`, CC 10, NLOC 37, params 3
- `obs_engine::Engine::v2_sync_source_observers` — `engine/runtime_source_v2.cpp:931-968`, CC 10, NLOC 35, params 0
- `obs_engine::Engine::v2_source_duplicate` — `engine/runtime_source_v2.cpp:1071-1109`, CC 10, NLOC 35, params 3
- `Get-WorkflowRunBlocks` — `tools/check-complexity.ps1:343-378`, CC 10, NLOC 35, params 1
- `obs_engine::collect_filter_signal` — `engine/runtime_filter_v2.cpp:935-972`, CC 10, NLOC 34, params 3
- `obs_engine::Engine::v2_sync_filter_registry` — `engine/runtime_filter_v2.cpp:1428-1461`, CC 10, NLOC 34, params 1
- `obs_engine::take_deferred_source_events` — `engine/runtime_source_v2.cpp:441-478`, CC 10, NLOC 34, params 3
- `obs_engine::Engine::v2_item_set_transform` — `engine/runtime_v2.cpp:604-643`, CC 10, NLOC 34, params 3
- `Get-NonBlankLineCount` — `tools/check-complexity.ps1:1021-1055`, CC 10, NLOC 34, params 1
- `obs_engine::Engine::v2_settle_filter_mutation` — `engine/runtime_filter_v2.cpp:1599-1630`, CC 10, NLOC 30, params 3
- `initialize_source_mutexes` — `libobs/obs-source.c:217-247`, CC 10, NLOC 30, params 1
- `obs_source_update_internal` — `libobs/obs-source.c:1132-1165`, CC 10, NLOC 30, params 5
- `Get-WorkflowPowerShellAstMetrics` — `tools/check-complexity.ps1:447-477`, CC 10, NLOC 30, params 1
- `obs_engine::Engine::v2_filter_set_order` — `engine/runtime_filter_v2.cpp:1990-2018`, CC 10, NLOC 29, params 3
- `obs_engine::prepare_v2_request` — `engine/protocol_v2.cpp:809-836`, CC 10, NLOC 27, params 7
- `obs_engine::read_line_limited` — `engine/protocol.cpp:116-145`, CC 10, NLOC 26, params 1
- `Invoke-Task7LiveRead` — `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1:199-224`, CC 10, NLOC 24, params 1
- `Invoke-Task5InitialSubscriptionChecks` — `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1:96-120`, CC 10, NLOC 23, params 1
- `Invoke-Task6SettingsPatch` — `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1:353-375`, CC 10, NLOC 23, params 1
- `obs_engine::read_mouse_button_input` — `engine/runtime_interaction_v2.cpp:559-577`, CC 10, NLOC 18, params 4
- `Get-PostHistoryRecreatedPaths` — `tools/check-complexity.ps1:2851-2869`, CC 10, NLOC 18, params 1
- `Assert-Task2Hello` — `.github/scripts/engine-protocol-v2-task2-framing.ps1:62-78`, CC 10, NLOC 17, params 1
- `obs_engine::filter_settings_event_matches` — `engine/runtime_filter_v2.cpp:1061-1077`, CC 10, NLOC 17, params 4
- `Get-WorkflowInterpreter` — `tools/check-complexity.ps1:498-512`, CC 10, NLOC 14, params 1
- `Invoke-Task11FilterMutations` — `.github/scripts/engine-protocol-v2-task11.ps1:461-538`, CC 9, NLOC 75, params 1
- `Invoke-Task5EventQueuePolicy` — `.github/scripts/engine-protocol-v2-task5-event-queue-policy.ps1:3-65`, CC 9, NLOC 56, params 1
- `Invoke-Task7PropertiesBridge` — `.github/scripts/engine-protocol-v2-task7-properties-bridge.ps1:3-65`, CC 9, NLOC 56, params 1
- `Get-PathScopeState` — `tools/check-complexity.ps1:2922-2969`, CC 9, NLOC 47, params 1
- `New-AutomaticIdentity` — `tools/check-complexity.ps1:1639-1680`, CC 9, NLOC 41, params 1
- `obs_engine::Engine::v2_properties_invoke_button` — `engine/runtime_properties_v2.cpp:539-586`, CC 9, NLOC 40, params 3
- `obs_engine::Engine::v2_filter_prepare_parent_removal` — `engine/runtime_filter_v2.cpp:1544-1581`, CC 9, NLOC 36, params 2
- `Get-UnmigratedIdentityViolations` — `tools/check-complexity.ps1:1769-1805`, CC 9, NLOC 36, params 1
- `obs_engine::Engine::v2_interaction_mouse_move` — `engine/runtime_interaction_v2.cpp:466-501`, CC 9, NLOC 35, params 3
- `process_media_action` — `libobs/obs-source.c:1398-1431`, CC 9, NLOC 34, params 2
- `obs_engine::Engine::command_source_create` — `engine/runtime.cpp:311-346`, CC 9, NLOC 32, params 2
- `obs_source_media_action_enqueue` — `libobs/obs-source.c:5924-5958`, CC 9, NLOC 32, params 4
- `Get-PathScopeLines` — `tools/check-complexity.ps1:2971-3003`, CC 9, NLOC 32, params 1
- `Invoke-Task6Transform` — `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1:289-318`, CC 9, NLOC 30, params 1
- `obs_engine::handle_filter_request` — `engine/protocol_filter_v2.cpp:412-450`, CC 9, NLOC 30, params 5
- `obs_engine::Engine::command_item_transform` — `engine/runtime.cpp:547-578`, CC 9, NLOC 30, params 2
- `Update-PostAcceptedPathProvenance` — `tools/check-complexity.ps1:866-896`, CC 9, NLOC 30, params 1
- `Get-IdentityMigrationCurrent` — `tools/check-complexity.ps1:1383-1411`, CC 9, NLOC 28, params 1
- `obs_engine::publish_deferred_source_snapshot` — `engine/runtime_source_v2.cpp:480-508`, CC 9, NLOC 27, params 2
- `obs_engine::canonicalize_source_result` — `engine/runtime_source_settle_v2.cpp:228-256`, CC 9, NLOC 26, params 2
- `Read-IdentityMigrationEntries` — `tools/check-complexity.ps1:1317-1343`, CC 9, NLOC 26, params 1
- `obs_engine::Engine::v2_settle_source_mutation` — `engine/runtime_source_settle_v2.cpp:314-340`, CC 9, NLOC 25, params 2
- `Assert-TrustedBaselineScope` — `tools/check-complexity.ps1:145-170`, CC 9, NLOC 25, params 1
- `check_dynamic_properties` — `engine/properties_test.cpp:217-239`, CC 9, NLOC 21, params 1
- `obs_engine::Engine::v2_media_toggle_pause` — `engine/runtime_media_v2.cpp:1114-1135`, CC 9, NLOC 21, params 3
- `obs_engine::strip_inline_list_items` — `engine/runtime_properties_v2.cpp:124-145`, CC 9, NLOC 21, params 1
- `Assert-TrustedBaselineReportShape` — `tools/check-complexity.ps1:124-143`, CC 9, NLOC 19, params 1
- `obs_engine::collect_sensitive_recursive` — `engine/properties_sensitive.cpp:6-22`, CC 9, NLOC 17, params 2
- `obs_engine::Engine::v2_read_source_create_options` — `engine/runtime_v2.cpp:240-256`, CC 9, NLOC 17, params 5
- `Invoke-Task8LoadState` — `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1:263-278`, CC 9, NLOC 16, params 2
- `obs_engine::read_candidate_params` — `engine/runtime_properties_v2.cpp:169-183`, CC 9, NLOC 15, params 7
- `Assert-Task11Ordering` — `.github/scripts/engine-protocol-v2-task11-core-audit.ps1:15-28`, CC 9, NLOC 14, params 1
- `Invoke-Task8SourceRemoval` — `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1:285-298`, CC 9, NLOC 12, params 2
- `obs_engine::parse_u32` — `engine/config.cpp:18-31`, CC 9, NLOC 12, params 4
- `Invoke-Task8SourceIdentity` — `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1:167-176`, CC 9, NLOC 10, params 2
- `Initialize-Task10Sources` — `.github/scripts/engine-protocol-v2-task10.ps1:210-268`, CC 8, NLOC 56, params 1
- `obs_engine::Engine::v2_source_remove` — `engine/runtime_v2.cpp:411-455`, CC 8, NLOC 39, params 3
- `main` — `engine/host.cpp:241-283`, CC 8, NLOC 38, params 2
- `obs_engine::Engine::v2_properties_get_list_items` — `engine/runtime_properties_v2.cpp:443-482`, CC 8, NLOC 37, params 3
- `obs_engine::Engine::v2_source_get_missing_files` — `engine/runtime_source_v2.cpp:1277-1315`, CC 8, NLOC 37, params 3
- `New-WorkflowRunBlockRecord` — `tools/check-complexity.ps1:305-341`, CC 8, NLOC 36, params 1
- `obs_engine::Engine::v2_filter_duplicate` — `engine/runtime_filter_v2.cpp:1840-1874`, CC 8, NLOC 35, params 3
- `obs_engine::take_deferred_media_events` — `engine/runtime_media_v2.cpp:311-347`, CC 8, NLOC 34, params 3
- `obs_engine::take_deferred_filter_events` — `engine/runtime_filter_v2.cpp:479-514`, CC 8, NLOC 33, params 3
- `obs_engine::publish_media_events` — `engine/runtime_media_v2.cpp:519-556`, CC 8, NLOC 33, params 5
- `obs_engine::Engine::v2_item_create` — `engine/runtime_v2.cpp:542-581`, CC 8, NLOC 33, params 3
- `Invoke-Task10PlayPause` — `.github/scripts/engine-protocol-v2-task10.ps1:270-303`, CC 8, NLOC 32, params 1
- `obs_engine::Engine::v2_apply_filter_settings` — `engine/runtime_filter_v2.cpp:1897-1930`, CC 8, NLOC 32, params 4
- `Invoke-CheckMode` — `tools/check-complexity.ps1:3477-3509`, CC 8, NLOC 32, params 1
- `obs_engine::serialize_property_list_items` — `engine/properties.cpp:581-612`, CC 8, NLOC 31, params 1
- `obs_engine::parse_subscription_list` — `engine/protocol_v2.cpp:493-521`, CC 8, NLOC 29, params 3
- `Invoke-Task6SourceRemoval` — `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1:425-453`, CC 8, NLOC 28, params 2
- `obs_engine::Engine::v2_add_filter_observer` — `engine/runtime_filter_v2.cpp:1312-1341`, CC 8, NLOC 28, params 1
- `obs_engine::Engine::command_scene_create` — `engine/runtime.cpp:439-468`, CC 8, NLOC 27, params 2
- `Get-AstDecisionWeight` — `tools/check-complexity.ps1:1100-1127`, CC 8, NLOC 27, params 1
- `obs_engine::decode_utf8_scalars` — `engine/runtime_interaction_v2.cpp:266-291`, CC 8, NLOC 26, params 3
- `Read-EngineMessage` — `.github/scripts/engine-protocol-v2-task11.ps1:49-73`, CC 8, NLOC 25, params 1
- `test_state_overflow_requires_resync` — `engine/events_test.cpp:94-119`, CC 8, NLOC 25, params 0
- `obs_engine::Engine::v2_filter_rename` — `engine/runtime_filter_v2.cpp:1814-1838`, CC 8, NLOC 25, params 3
- `obs_engine::Engine::v2_collect_media_observer_changes` — `engine/runtime_media_v2.cpp:981-1005`, CC 8, NLOC 25, params 2
- `New-OperatorCodeCommitRecords` — `tools/check-complexity.ps1:3181-3206`, CC 8, NLOC 25, params 1
- `obs_engine::Engine::v2_filter_register_source_filters` — `engine/runtime_filter_v2.cpp:1286-1310`, CC 8, NLOC 24, params 2
- `obs_engine::publish_media_batch` — `engine/runtime_media_v2.cpp:368-391`, CC 8, NLOC 24, params 3
- `obs_engine::publish_source_events` — `engine/runtime_source_v2.cpp:563-590`, CC 8, NLOC 24, params 3
- `obs_engine::Engine::v2_source_rename` — `engine/runtime_source_v2.cpp:1111-1136`, CC 8, NLOC 23, params 3
- `Test-MetricInScope` — `tools/check-complexity.ps1:1830-1852`, CC 8, NLOC 22, params 1
- `obs_engine::decode_utf8_lead` — `engine/runtime_interaction_v2.cpp:233-251`, CC 8, NLOC 19, params 3
- `obs_engine::wait_for_media_action` — `engine/runtime_media_v2.cpp:605-623`, CC 8, NLOC 19, params 5
- `Invoke-Task7LiveRefresh` — `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1:226-243`, CC 8, NLOC 17, params 1
- `check_frame_rate_schema` — `engine/properties_test.cpp:179-195`, CC 8, NLOC 17, params 1
- `Stop-Task11AfterFailure` — `.github/scripts/engine-protocol-v2-task11.ps1:930-944`, CC 8, NLOC 15, params 1
- `obs_engine::Engine::v2_prepare_filter_settlement` — `engine/runtime_filter_v2.cpp:1583-1597`, CC 8, NLOC 15, params 5
- `Invoke-Task8Rename` — `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1:198-211`, CC 8, NLOC 14, params 1
- `obs_engine::parse_handle_text` — `engine/runtime_filter_v2.cpp:180-193`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_interaction_v2.cpp:149-162`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_media_v2.cpp:145-160`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_properties_v2.cpp:94-107`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_source_settle_v2.cpp:56-69`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_source_v2.cpp:110-123`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_v2.cpp:85-100`, CC 8, NLOC 14, params 2
- `obs_engine::read_mouse_wheel_input` — `engine/runtime_interaction_v2.cpp:616-628`, CC 8, NLOC 13, params 4
- `Stop-Task10AfterFailure` — `.github/scripts/engine-protocol-v2-task10.ps1:643-654`, CC 8, NLOC 12, params 1
- `Assert-Task8BridgePreRequestOrdering` — `.github/scripts/engine-protocol-v2-task8-concurrency-bridge-audit.ps1:24-35`, CC 8, NLOC 12, params 1
- `Assert-Task8BridgePostRequestOrdering` — `.github/scripts/engine-protocol-v2-task8-concurrency-bridge-audit.ps1:37-48`, CC 8, NLOC 12, params 1
- `Stop-Task8AfterFailure` — `.github/scripts/engine-protocol-v2-task8-concurrency.ps1:307-318`, CC 8, NLOC 12, params 1
- `check_telemetry_output` — `engine/events_test.cpp:185-196`, CC 8, NLOC 12, params 1
- `obs_engine::is_valid_utf8_scalar` — `engine/runtime_interaction_v2.cpp:253-264`, CC 8, NLOC 12, params 2
- `Assert-Task3Hello` — `.github/scripts/engine-protocol-v2-task3-capabilities.ps1:65-75`, CC 8, NLOC 11, params 1
- `obs_engine::batch_matches_filter_update` — `engine/runtime_filter_v2.cpp:1087-1096`, CC 8, NLOC 10, params 5
- `Invoke-Task10ToggleAndSeek` — `.github/scripts/engine-protocol-v2-task10.ps1:305-356`, CC 7, NLOC 49, params 1
- `obs_engine::Engine::handle` — `engine/runtime.cpp:165-211`, CC 7, NLOC 44, params 1
- `New-PowerShellScriptBodyMetric` — `tools/check-complexity.ps1:1901-1944`, CC 7, NLOC 43, params 1
- `Invoke-Task11SourceDuplicate` — `.github/scripts/engine-protocol-v2-task11.ps1:666-705`, CC 7, NLOC 40, params 1
- `New-ScopeContext` — `tools/check-complexity.ps1:3087-3127`, CC 7, NLOC 40, params 1
- `obs_engine::Engine::v2_prepare_filter_shutdown` — `engine/runtime_filter_v2.cpp:1506-1542`, CC 7, NLOC 36, params 0
- `dispatch_request` — `engine/host.cpp:156-191`, CC 7, NLOC 35, params 5
- `Invoke-Task6SourceCreation` — `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1:226-259`, CC 7, NLOC 33, params 1
- `obs_engine::Engine::v2_scene_remove` — `engine/runtime_v2.cpp:501-540`, CC 7, NLOC 33, params 3
- `obs_engine::Engine::command_scene_add` — `engine/runtime.cpp:493-528`, CC 7, NLOC 33, params 2
- `obs_engine::EventDispatcher::subscribe` — `engine/events.cpp:102-134`, CC 7, NLOC 31, params 2
- `obs_engine::settle_deferred_source_update` — `engine/runtime_source_settle_v2.cpp:181-226`, CC 7, NLOC 31, params 5
- `Merge-IdentityMigrationMaps` — `tools/check-complexity.ps1:1473-1504`, CC 7, NLOC 31, params 1
- `Invoke-WorkflowExecutableAudit` — `tools/check-complexity.ps1:583-613`, CC 7, NLOC 30, params 1
- `obs_engine::queue_deferred_media_events_locked` — `engine/runtime_media_v2.cpp:412-439`, CC 7, NLOC 28, params 6
- `Find-BeforeMetric` — `tools/check-complexity.ps1:2363-2391`, CC 7, NLOC 28, params 1
- `obs_engine::Engine::v2_filter_set_enabled` — `engine/runtime_filter_v2.cpp:1944-1970`, CC 7, NLOC 27, params 3
- `obs_engine::Engine::v2_end_event_capture` — `engine/runtime_source_v2.cpp:883-909`, CC 7, NLOC 26, params 0
- `obs_engine::apply_transform_vector` — `engine/runtime_v2.cpp:154-180`, CC 7, NLOC 26, params 10
- `obs_engine::Engine::prepare_startup_environment` — `engine/runtime.cpp:80-108`, CC 7, NLOC 26, params 0
- `obs_engine::handle_session_method` — `engine/protocol_v2.cpp:662-685`, CC 7, NLOC 24, params 4
- `obs_engine::mark_filter_settlement_lost` — `engine/runtime_filter_v2.cpp:1188-1216`, CC 7, NLOC 24, params 4
- `obs_engine::Engine::v2_end_filter_event_capture` — `engine/runtime_filter_v2.cpp:1249-1272`, CC 7, NLOC 24, params 0
- `obs_engine::Engine::v2_end_media_event_capture` — `engine/runtime_media_v2.cpp:938-963`, CC 7, NLOC 24, params 0
- `obs_engine::EventDispatcher::run` — `engine/events.cpp:311-333`, CC 7, NLOC 23, params 0
- `run_protocol_loop` — `engine/host.cpp:193-216`, CC 7, NLOC 23, params 4
- `obs_engine::wait_for_filter_update` — `engine/runtime_filter_v2.cpp:1146-1168`, CC 7, NLOC 23, params 8
- `obs_source_init` — `libobs/obs-source.c:250-277`, CC 7, NLOC 23, params 1
- `obs_engine::Engine::load_runtime_modules` — `engine/runtime.cpp:135-158`, CC 7, NLOC 22, params 0
- `New-InclusionBaselineMap` — `tools/check-complexity.ps1:2714-2736`, CC 7, NLOC 22, params 1
- `obs_engine::collect_media_signal` — `engine/runtime_media_v2.cpp:718-742`, CC 7, NLOC 21, params 3
- `obs_engine::collect_source_signal` — `engine/runtime_source_v2.cpp:684-708`, CC 7, NLOC 21, params 2
- `Get-WorkingTreeRecreatedPaths` — `tools/check-complexity.ps1:814-835`, CC 7, NLOC 21, params 1
- `Get-NonCyclomaticCategory` — `tools/check-complexity.ps1:3234-3255`, CC 7, NLOC 21, params 1
- `obs_engine::resolve_property_schema` — `engine/properties.cpp:633-653`, CC 7, NLOC 20, params 4
- `obs_engine::route_filter_payload_locked` — `engine/runtime_filter_v2.cpp:677-696`, CC 7, NLOC 20, params 1
- `obs_engine::promote_deferred_filter_update` — `engine/runtime_filter_v2.cpp:1098-1117`, CC 7, NLOC 20, params 6
- `obs_engine::parse_modifiers` — `engine/runtime_interaction_v2.cpp:212-231`, CC 7, NLOC 19, params 3
- `obs_engine::normalize_kind_entry` — `engine/runtime_source_settle_v2.cpp:292-310`, CC 7, NLOC 19, params 1
- `media_action_callback_available` — `libobs/obs-source.c:5881-5899`, CC 7, NLOC 19, params 2
- `obs_engine::is_event_name` — `engine/events.cpp:20-38`, CC 7, NLOC 18, params 1
- `obs_engine::read_filter_settings_input` — `engine/runtime_filter_v2.cpp:254-271`, CC 7, NLOC 18, params 5
- `obs_engine::resolve_uncertain_filter_updates_locked` — `engine/runtime_filter_v2.cpp:432-450`, CC 7, NLOC 18, params 3
- `obs_engine::promote_deferred_media_action_locked` — `engine/runtime_media_v2.cpp:558-576`, CC 7, NLOC 18, params 5
- `Read-Event` — `.github/scripts/engine-protocol-v2-task10.ps1:114-130`, CC 7, NLOC 17, params 3
- `Read-StateEvent` — `.github/scripts/engine-protocol-v2-task8-concurrency.ps1:53-69`, CC 7, NLOC 17, params 3
- `obs_engine::validate_property_patch` — `engine/properties.cpp:614-631`, CC 7, NLOC 17, params 2
- `Invoke-Task4StaleClose` — `.github/scripts/engine-protocol-v2-task4-revisions.ps1:101-116`, CC 7, NLOC 16, params 1
- `Get-CeilingMetricViolations` — `tools/check-complexity.ps1:3378-3394`, CC 7, NLOC 16, params 1
- `check_list_schema` — `engine/properties_test.cpp:157-171`, CC 7, NLOC 15, params 1
- `obs_engine::batch_matches_source_update` — `engine/runtime_source_settle_v2.cpp:120-134`, CC 7, NLOC 15, params 3
- `Invoke-Task8Duplicate` — `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1:213-225`, CC 7, NLOC 13, params 1
- `obs_engine::parse_v2_request_options` — `engine/protocol_v2.cpp:795-807`, CC 7, NLOC 13, params 3
- `obs_engine::read_text_input` — `engine/runtime_interaction_v2.cpp:710-721`, CC 7, NLOC 12, params 5
- `Assert-Task7SchemaProperties` — `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1:108-118`, CC 7, NLOC 11, params 1
- `Assert-Task5StoppingEvent` — `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1:174-183`, CC 7, NLOC 10, params 1
- `Invoke-Task8ReplaceSettings` — `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1:232-241`, CC 7, NLOC 10, params 1
- `check_eviction_output` — `engine/events_test.cpp:59-68`, CC 7, NLOC 10, params 1
- `obs_engine::read_key_input` — `engine/runtime_interaction_v2.cpp:700-708`, CC 7, NLOC 9, params 3
- `Invoke-Task8SaveState` — `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1:243-249`, CC 7, NLOC 7, params 2
- `Invoke-Task8CaptureRouting` — `.github/scripts/engine-protocol-v2-task8-concurrency-capture-routing.ps1:3-79`, CC 6, NLOC 59, params 1
- `New-HistoryContext` — `tools/check-complexity.ps1:2770-2819`, CC 6, NLOC 49, params 1
- `Start-EngineCase` — `.github/scripts/engine-protocol-v2-task8-concurrency.ps1:71-121`, CC 6, NLOC 44, params 1
- `Get-PowerShellFileMetrics` — `tools/check-complexity.ps1:1946-1986`, CC 6, NLOC 40, params 1
- `Invoke-Task10RestartAndStop` — `.github/scripts/engine-protocol-v2-task10.ps1:358-396`, CC 6, NLOC 37, params 1
- `obs_engine::Engine::v2_build_property_target` — `engine/runtime_properties_v2.cpp:358-398`, CC 6, NLOC 37, params 6
- `obs_engine::Engine::v2_source_create` — `engine/runtime_v2.cpp:311-353`, CC 6, NLOC 37, params 3
- `Write-Task1Diagnostics` — `.github/scripts/engine-protocol-v2-task1-protocol-v1.ps1:197-233`, CC 6, NLOC 34, params 1
- `obs_engine::Engine::v2_properties_validate` — `engine/runtime_properties_v2.cpp:484-522`, CC 6, NLOC 33, params 3
- `Get-LizardRows` — `tools/check-complexity.ps1:954-988`, CC 6, NLOC 32, params 1
- `obs_engine::EventDispatcher::emit` — `engine/events.cpp:335-367`, CC 6, NLOC 31, params 1
- `obs_engine::Engine::v2_filter_list` — `engine/runtime_filter_v2.cpp:1712-1743`, CC 6, NLOC 31, params 3
- `obs_engine::Engine::v2_prepare_media_shutdown` — `engine/runtime_media_v2.cpp:1029-1061`, CC 6, NLOC 31, params 0
- `obs_engine::Engine::v2_prepare_shutdown` — `engine/runtime_source_v2.cpp:970-1002`, CC 6, NLOC 31, params 0
- `Invoke-RaceNewerRequest` — `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1:220-251`, CC 6, NLOC 30, params 2
- `Initialize-Task11FilterGraph` — `.github/scripts/engine-protocol-v2-task11.ps1:379-408`, CC 6, NLOC 30, params 1
- `obs_engine::publish_filter_events` — `engine/runtime_filter_v2.cpp:708-740`, CC 6, NLOC 30, params 7
- `obs_engine::Engine::v2_media_set_position` — `engine/runtime_media_v2.cpp:1243-1272`, CC 6, NLOC 29, params 3
- `obs_engine::Engine::v2_source_load_state` — `engine/runtime_source_v2.cpp:1343-1374`, CC 6, NLOC 29, params 3
- `obs_engine::Engine::v2_add_source_observer` — `engine/runtime_source_v2.cpp:816-844`, CC 6, NLOC 28, params 3
- `Invoke-Task7PropertiesSmoke` — `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1:286-312`, CC 6, NLOC 27, params 1
- `obs_engine::Engine::v2_filter_kind_list` — `engine/runtime_filter_v2.cpp:1632-1658`, CC 6, NLOC 27, params 2
- `obs_engine::Engine::v2_filter_create` — `engine/runtime_filter_v2.cpp:1757-1787`, CC 6, NLOC 27, params 3
- `Invoke-RaceAction` — `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1:190-218`, CC 6, NLOC 26, params 3
- `obs_engine::handle_v2_request` — `engine/protocol_v2.cpp:888-919`, CC 6, NLOC 26, params 4
- `obs_engine::Engine::v2_add_media_observer` — `engine/runtime_media_v2.cpp:879-905`, CC 6, NLOC 26, params 3
- `obs_engine::Engine::v2_source_kind_list` — `engine/runtime_v2.cpp:258-286`, CC 6, NLOC 26, params 2
- `Complete-Task6Session` — `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1:472-496`, CC 6, NLOC 25, params 1
- `Invoke-Task6RuntimeSmoke` — `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1:540-564`, CC 6, NLOC 25, params 1
- `obs_engine::handle_runtime_method` — `engine/protocol_v2.cpp:721-750`, CC 6, NLOC 25, params 8
- `obs_engine::Engine::command_source_types` — `engine/runtime.cpp:269-293`, CC 6, NLOC 25, params 1
- `obs_engine::parse_args` — `engine/config.cpp:100-125`, CC 6, NLOC 24, params 3
- `test_ordered_resync_preserves_queued_event` — `engine/events_test.cpp:144-167`, CC 6, NLOC 24, params 0
- `obs_engine::Engine::v2_filter_order_data` — `engine/runtime_filter_v2.cpp:802-826`, CC 6, NLOC 24, params 3
- `Validate-Allowlist` — `tools/check-complexity.ps1:1218-1242`, CC 6, NLOC 24, params 1
- `Run-CaseE` — `.github/scripts/engine-protocol-v2-task8-concurrency.ps1:251-276`, CC 6, NLOC 23, params 1
- `Complete-Task5Session` — `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1:185-206`, CC 6, NLOC 22, params 1
- `test_telemetry_policy` — `engine/events_test.cpp:198-221`, CC 6, NLOC 22, params 0
- `obs_engine::Engine::v2_register_attached_filter` — `engine/runtime_filter_v2.cpp:779-800`, CC 6, NLOC 22, params 4
- `obs_source_deferred_update` — `libobs/obs-source.c:1167-1190`, CC 6, NLOC 22, params 1
- `Get-HistoricalPathAliases` — `tools/check-complexity.ps1:918-940`, CC 6, NLOC 22, params 1
- `Invoke-Task6ItemCreation` — `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1:267-287`, CC 6, NLOC 21, params 1
- `obs_engine::Engine::v2_build_source_kind_property_target` — `engine/runtime_properties_v2.cpp:270-290`, CC 6, NLOC 21, params 6
- `obs_engine::Engine::v2_build_filter_kind_property_target` — `engine/runtime_properties_v2.cpp:314-334`, CC 6, NLOC 21, params 6
- `obs_engine::read_settings_json` — `engine/runtime_source_settle_v2.cpp:88-108`, CC 6, NLOC 21, params 2
- `obs_engine::Engine::v2_normalize_source_kind_metadata` — `engine/runtime_source_settle_v2.cpp:342-364`, CC 6, NLOC 21, params 1
- `Get-WorkflowOperatorLines` — `tools/check-complexity.ps1:380-405`, CC 6, NLOC 21, params 1
- `obs_engine::read_finite_double` — `engine/protocol.cpp:216-235`, CC 6, NLOC 20, params 6
- `obs_engine::Engine::v2_register_filter` — `engine/runtime_filter_v2.cpp:758-777`, CC 6, NLOC 20, params 3
- `obs_engine::Engine::v2_prepare_property_button` — `engine/runtime_properties_v2.cpp:336-356`, CC 6, NLOC 20, params 3
- `Assert-JsonUniqueProperties` — `tools/check-complexity.ps1:1345-1365`, CC 6, NLOC 20, params 1
- `obs_engine::collect_filter_ref` — `engine/runtime_filter_v2.cpp:833-851`, CC 6, NLOC 19, params 2
- `obs_engine::collect_filter_state_events` — `engine/runtime_filter_v2.cpp:913-933`, CC 6, NLOC 19, params 4
- `obs_engine::Engine::v2_source_kind_defaults` — `engine/runtime_v2.cpp:288-309`, CC 6, NLOC 19, params 3
- `obs_engine::apply_legacy_transform_alignment` — `engine/runtime.cpp:51-69`, CC 6, NLOC 19, params 3
- `obs_engine::Engine::v2_sync_media_observers` — `engine/runtime_media_v2.cpp:1007-1027`, CC 6, NLOC 18, params 0
- `Get-PathLanguage` — `tools/check-complexity.ps1:223-241`, CC 6, NLOC 18, params 1
- `Assert-IdentityMigrationFunctionIdentity` — `tools/check-complexity.ps1:1560-1578`, CC 6, NLOC 18, params 1
- `Assert-Task11FilterObserver` — `.github/scripts/engine-protocol-v2-task11-core-audit.ps1:30-46`, CC 6, NLOC 17, params 1
- `Assert-Task3HelloCapabilities` — `.github/scripts/engine-protocol-v2-task3-capabilities.ps1:77-93`, CC 6, NLOC 17, params 2
- `Complete-Task4Session` — `.github/scripts/engine-protocol-v2-task4-revisions.ps1:128-144`, CC 6, NLOC 17, params 1
- `Read-Task6StateEvent` — `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1:66-82`, CC 6, NLOC 17, params 2
- `obs_engine::apply_transform_alignment` — `engine/runtime_v2.cpp:195-212`, CC 6, NLOC 17, params 4
- `Get-WorkflowLexicalFeatures` — `tools/check-complexity.ps1:479-496`, CC 6, NLOC 17, params 1
- `Invoke-Task7ListItems` — `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1:120-135`, CC 6, NLOC 16, params 1
- `Invoke-Task8SourceSmoke` — `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1:340-355`, CC 6, NLOC 16, params 1
- `obs_engine::read_subscription_entry` — `engine/protocol_v2.cpp:476-491`, CC 6, NLOC 16, params 3
- `register_capture_sources` — `plugins/win-capture/plugin-main.c:132-151`, CC 6, NLOC 16, params 1
- `Get-ReportStatistics` — `tools/check-complexity.ps1:2132-2148`, CC 6, NLOC 16, params 1
- `setup_telemetry_policy` — `engine/events_test.cpp:169-183`, CC 6, NLOC 15, params 2
- `obs_engine::FilterCallbackScope::FilterCallbackScope` — `engine/runtime_filter_v2.cpp:349-363`, CC 6, NLOC 15, params 2
- `obs_engine::MediaCallbackScope::MediaCallbackScope` — `engine/runtime_media_v2.cpp:238-253`, CC 6, NLOC 15, params 2
- `obs_engine::collect_media_state_events` — `engine/runtime_media_v2.cpp:699-716`, CC 6, NLOC 15, params 4
- `obs_engine::SourceCallbackScope::SourceCallbackScope` — `engine/runtime_source_v2.cpp:385-399`, CC 6, NLOC 15, params 2
- `Invoke-Task7Validate` — `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1:148-161`, CC 6, NLOC 14, params 1
- `obs_engine::read_key_text` — `engine/runtime_interaction_v2.cpp:674-687`, CC 6, NLOC 14, params 3
- `obs_engine::erase_password_settings` — `engine/properties.cpp:154-166`, CC 6, NLOC 13, params 2
- `obs_engine::is_safe_identifier` — `engine/validation.hpp:14-27`, CC 6, NLOC 13, params 2
- `Stop-Task10Engine` — `.github/scripts/engine-protocol-v2-task10.ps1:656-667`, CC 6, NLOC 12, params 1
- `Stop-Task11Engine` — `.github/scripts/engine-protocol-v2-task11.ps1:946-957`, CC 6, NLOC 12, params 1
- `Stop-Task9Engine` — `.github/scripts/engine-protocol-v2-task9.ps1:374-385`, CC 6, NLOC 12, params 1
- `obs_engine::route_filter_uncertainty_locked` — `engine/runtime_filter_v2.cpp:664-675`, CC 6, NLOC 12, params 1
- `obs_engine::route_queued_media_event_locked` — `engine/runtime_media_v2.cpp:441-452`, CC 6, NLOC 12, params 7
- `obs_engine::read_media_position` — `engine/runtime_media_v2.cpp:1230-1241`, CC 6, NLOC 12, params 4
- `obs_engine::find_unversioned_input_id` — `engine/runtime_source_settle_v2.cpp:279-290`, CC 6, NLOC 12, params 1
- `check_pattern_cases` — `engine/events_test.cpp:39-49`, CC 6, NLOC 11, params 0
- `obs_engine::request_handle` — `engine/protocol.cpp:204-214`, CC 6, NLOC 11, params 4
- `obs_engine::read_source_state_input` — `engine/runtime_source_v2.cpp:329-339`, CC 6, NLOC 11, params 4
- `obs_engine::prepare_runtime_result` — `engine/protocol_v2.cpp:695-704`, CC 6, NLOC 10, params 6
- `obs_engine::read_property_target` — `engine/runtime_properties_v2.cpp:208-217`, CC 6, NLOC 10, params 4
- `obs_engine::validate_button_target` — `engine/runtime_properties_v2.cpp:219-228`, CC 6, NLOC 10, params 3
- `main` — `engine/events_test.cpp:235-243`, CC 6, NLOC 9, params 0
- `check_default_button` — `engine/properties_test.cpp:241-249`, CC 6, NLOC 9, params 1
- `check_url_button` — `engine/properties_test.cpp:251-258`, CC 6, NLOC 8, params 1
- `obs_engine::point_inside_source` — `engine/runtime_interaction_v2.cpp:314-321`, CC 6, NLOC 8, params 3
- `Invoke-Task8KindGet` — `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1:128-134`, CC 6, NLOC 7, params 1
- `Invoke-Task8ActivityState` — `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1:183-189`, CC 6, NLOC 7, params 1
- `Invoke-Task8LiveProperties` — `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1:191-196`, CC 6, NLOC 6, params 1
- `obs_engine::key_matches` — `engine/runtime_interaction_v2.cpp:293-298`, CC 6, NLOC 6, params 4

## Remaining functions with CC > 7

- `obs_source_destroy_defer` — `libobs/obs-source.c:813-893`, CC 13, NLOC 65, params 1
- `Assert-Task11LiveFilterReads` — `.github/scripts/engine-protocol-v2-task11.ps1:410-459`, CC 10, NLOC 50, params 1
- `Get-OperatorBlameLines` — `tools/check-complexity.ps1:727-768`, CC 10, NLOC 41, params 1
- `obs_engine::Engine::v2_scene_create` — `engine/runtime_v2.cpp:457-499`, CC 10, NLOC 37, params 3
- `obs_engine::Engine::v2_sync_source_observers` — `engine/runtime_source_v2.cpp:931-968`, CC 10, NLOC 35, params 0
- `obs_engine::Engine::v2_source_duplicate` — `engine/runtime_source_v2.cpp:1071-1109`, CC 10, NLOC 35, params 3
- `Get-WorkflowRunBlocks` — `tools/check-complexity.ps1:343-378`, CC 10, NLOC 35, params 1
- `obs_engine::collect_filter_signal` — `engine/runtime_filter_v2.cpp:935-972`, CC 10, NLOC 34, params 3
- `obs_engine::Engine::v2_sync_filter_registry` — `engine/runtime_filter_v2.cpp:1428-1461`, CC 10, NLOC 34, params 1
- `obs_engine::take_deferred_source_events` — `engine/runtime_source_v2.cpp:441-478`, CC 10, NLOC 34, params 3
- `obs_engine::Engine::v2_item_set_transform` — `engine/runtime_v2.cpp:604-643`, CC 10, NLOC 34, params 3
- `Get-NonBlankLineCount` — `tools/check-complexity.ps1:1021-1055`, CC 10, NLOC 34, params 1
- `obs_engine::Engine::v2_settle_filter_mutation` — `engine/runtime_filter_v2.cpp:1599-1630`, CC 10, NLOC 30, params 3
- `initialize_source_mutexes` — `libobs/obs-source.c:217-247`, CC 10, NLOC 30, params 1
- `obs_source_update_internal` — `libobs/obs-source.c:1132-1165`, CC 10, NLOC 30, params 5
- `Get-WorkflowPowerShellAstMetrics` — `tools/check-complexity.ps1:447-477`, CC 10, NLOC 30, params 1
- `obs_engine::Engine::v2_filter_set_order` — `engine/runtime_filter_v2.cpp:1990-2018`, CC 10, NLOC 29, params 3
- `obs_engine::prepare_v2_request` — `engine/protocol_v2.cpp:809-836`, CC 10, NLOC 27, params 7
- `obs_engine::read_line_limited` — `engine/protocol.cpp:116-145`, CC 10, NLOC 26, params 1
- `Invoke-Task7LiveRead` — `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1:199-224`, CC 10, NLOC 24, params 1
- `Invoke-Task5InitialSubscriptionChecks` — `.github/scripts/engine-protocol-v2-task5-subscriptions.ps1:96-120`, CC 10, NLOC 23, params 1
- `Invoke-Task6SettingsPatch` — `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1:353-375`, CC 10, NLOC 23, params 1
- `obs_engine::read_mouse_button_input` — `engine/runtime_interaction_v2.cpp:559-577`, CC 10, NLOC 18, params 4
- `Get-PostHistoryRecreatedPaths` — `tools/check-complexity.ps1:2851-2869`, CC 10, NLOC 18, params 1
- `Assert-Task2Hello` — `.github/scripts/engine-protocol-v2-task2-framing.ps1:62-78`, CC 10, NLOC 17, params 1
- `obs_engine::filter_settings_event_matches` — `engine/runtime_filter_v2.cpp:1061-1077`, CC 10, NLOC 17, params 4
- `Get-WorkflowInterpreter` — `tools/check-complexity.ps1:498-512`, CC 10, NLOC 14, params 1
- `Invoke-Task11FilterMutations` — `.github/scripts/engine-protocol-v2-task11.ps1:461-538`, CC 9, NLOC 75, params 1
- `Invoke-Task5EventQueuePolicy` — `.github/scripts/engine-protocol-v2-task5-event-queue-policy.ps1:3-65`, CC 9, NLOC 56, params 1
- `Invoke-Task7PropertiesBridge` — `.github/scripts/engine-protocol-v2-task7-properties-bridge.ps1:3-65`, CC 9, NLOC 56, params 1
- `Get-PathScopeState` — `tools/check-complexity.ps1:2922-2969`, CC 9, NLOC 47, params 1
- `New-AutomaticIdentity` — `tools/check-complexity.ps1:1639-1680`, CC 9, NLOC 41, params 1
- `obs_engine::Engine::v2_properties_invoke_button` — `engine/runtime_properties_v2.cpp:539-586`, CC 9, NLOC 40, params 3
- `obs_engine::Engine::v2_filter_prepare_parent_removal` — `engine/runtime_filter_v2.cpp:1544-1581`, CC 9, NLOC 36, params 2
- `Get-UnmigratedIdentityViolations` — `tools/check-complexity.ps1:1769-1805`, CC 9, NLOC 36, params 1
- `obs_engine::Engine::v2_interaction_mouse_move` — `engine/runtime_interaction_v2.cpp:466-501`, CC 9, NLOC 35, params 3
- `process_media_action` — `libobs/obs-source.c:1398-1431`, CC 9, NLOC 34, params 2
- `obs_engine::Engine::command_source_create` — `engine/runtime.cpp:311-346`, CC 9, NLOC 32, params 2
- `obs_source_media_action_enqueue` — `libobs/obs-source.c:5924-5958`, CC 9, NLOC 32, params 4
- `Get-PathScopeLines` — `tools/check-complexity.ps1:2971-3003`, CC 9, NLOC 32, params 1
- `Invoke-Task6Transform` — `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1:289-318`, CC 9, NLOC 30, params 1
- `obs_engine::handle_filter_request` — `engine/protocol_filter_v2.cpp:412-450`, CC 9, NLOC 30, params 5
- `obs_engine::Engine::command_item_transform` — `engine/runtime.cpp:547-578`, CC 9, NLOC 30, params 2
- `Update-PostAcceptedPathProvenance` — `tools/check-complexity.ps1:866-896`, CC 9, NLOC 30, params 1
- `Get-IdentityMigrationCurrent` — `tools/check-complexity.ps1:1383-1411`, CC 9, NLOC 28, params 1
- `obs_engine::publish_deferred_source_snapshot` — `engine/runtime_source_v2.cpp:480-508`, CC 9, NLOC 27, params 2
- `obs_engine::canonicalize_source_result` — `engine/runtime_source_settle_v2.cpp:228-256`, CC 9, NLOC 26, params 2
- `Read-IdentityMigrationEntries` — `tools/check-complexity.ps1:1317-1343`, CC 9, NLOC 26, params 1
- `obs_engine::Engine::v2_settle_source_mutation` — `engine/runtime_source_settle_v2.cpp:314-340`, CC 9, NLOC 25, params 2
- `Assert-TrustedBaselineScope` — `tools/check-complexity.ps1:145-170`, CC 9, NLOC 25, params 1
- `check_dynamic_properties` — `engine/properties_test.cpp:217-239`, CC 9, NLOC 21, params 1
- `obs_engine::Engine::v2_media_toggle_pause` — `engine/runtime_media_v2.cpp:1114-1135`, CC 9, NLOC 21, params 3
- `obs_engine::strip_inline_list_items` — `engine/runtime_properties_v2.cpp:124-145`, CC 9, NLOC 21, params 1
- `Assert-TrustedBaselineReportShape` — `tools/check-complexity.ps1:124-143`, CC 9, NLOC 19, params 1
- `obs_engine::collect_sensitive_recursive` — `engine/properties_sensitive.cpp:6-22`, CC 9, NLOC 17, params 2
- `obs_engine::Engine::v2_read_source_create_options` — `engine/runtime_v2.cpp:240-256`, CC 9, NLOC 17, params 5
- `Invoke-Task8LoadState` — `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1:263-278`, CC 9, NLOC 16, params 2
- `obs_engine::read_candidate_params` — `engine/runtime_properties_v2.cpp:169-183`, CC 9, NLOC 15, params 7
- `Assert-Task11Ordering` — `.github/scripts/engine-protocol-v2-task11-core-audit.ps1:15-28`, CC 9, NLOC 14, params 1
- `Invoke-Task8SourceRemoval` — `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1:285-298`, CC 9, NLOC 12, params 2
- `obs_engine::parse_u32` — `engine/config.cpp:18-31`, CC 9, NLOC 12, params 4
- `Invoke-Task8SourceIdentity` — `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1:167-176`, CC 9, NLOC 10, params 2
- `Initialize-Task10Sources` — `.github/scripts/engine-protocol-v2-task10.ps1:210-268`, CC 8, NLOC 56, params 1
- `obs_engine::Engine::v2_source_remove` — `engine/runtime_v2.cpp:411-455`, CC 8, NLOC 39, params 3
- `main` — `engine/host.cpp:241-283`, CC 8, NLOC 38, params 2
- `obs_engine::Engine::v2_properties_get_list_items` — `engine/runtime_properties_v2.cpp:443-482`, CC 8, NLOC 37, params 3
- `obs_engine::Engine::v2_source_get_missing_files` — `engine/runtime_source_v2.cpp:1277-1315`, CC 8, NLOC 37, params 3
- `New-WorkflowRunBlockRecord` — `tools/check-complexity.ps1:305-341`, CC 8, NLOC 36, params 1
- `obs_engine::Engine::v2_filter_duplicate` — `engine/runtime_filter_v2.cpp:1840-1874`, CC 8, NLOC 35, params 3
- `obs_engine::take_deferred_media_events` — `engine/runtime_media_v2.cpp:311-347`, CC 8, NLOC 34, params 3
- `obs_engine::take_deferred_filter_events` — `engine/runtime_filter_v2.cpp:479-514`, CC 8, NLOC 33, params 3
- `obs_engine::publish_media_events` — `engine/runtime_media_v2.cpp:519-556`, CC 8, NLOC 33, params 5
- `obs_engine::Engine::v2_item_create` — `engine/runtime_v2.cpp:542-581`, CC 8, NLOC 33, params 3
- `Invoke-Task10PlayPause` — `.github/scripts/engine-protocol-v2-task10.ps1:270-303`, CC 8, NLOC 32, params 1
- `obs_engine::Engine::v2_apply_filter_settings` — `engine/runtime_filter_v2.cpp:1897-1930`, CC 8, NLOC 32, params 4
- `Invoke-CheckMode` — `tools/check-complexity.ps1:3477-3509`, CC 8, NLOC 32, params 1
- `obs_engine::serialize_property_list_items` — `engine/properties.cpp:581-612`, CC 8, NLOC 31, params 1
- `obs_engine::parse_subscription_list` — `engine/protocol_v2.cpp:493-521`, CC 8, NLOC 29, params 3
- `Invoke-Task6SourceRemoval` — `.github/scripts/engine-protocol-v2-task6-runtime-smoke.ps1:425-453`, CC 8, NLOC 28, params 2
- `obs_engine::Engine::v2_add_filter_observer` — `engine/runtime_filter_v2.cpp:1312-1341`, CC 8, NLOC 28, params 1
- `obs_engine::Engine::command_scene_create` — `engine/runtime.cpp:439-468`, CC 8, NLOC 27, params 2
- `Get-AstDecisionWeight` — `tools/check-complexity.ps1:1100-1127`, CC 8, NLOC 27, params 1
- `obs_engine::decode_utf8_scalars` — `engine/runtime_interaction_v2.cpp:266-291`, CC 8, NLOC 26, params 3
- `Read-EngineMessage` — `.github/scripts/engine-protocol-v2-task11.ps1:49-73`, CC 8, NLOC 25, params 1
- `test_state_overflow_requires_resync` — `engine/events_test.cpp:94-119`, CC 8, NLOC 25, params 0
- `obs_engine::Engine::v2_filter_rename` — `engine/runtime_filter_v2.cpp:1814-1838`, CC 8, NLOC 25, params 3
- `obs_engine::Engine::v2_collect_media_observer_changes` — `engine/runtime_media_v2.cpp:981-1005`, CC 8, NLOC 25, params 2
- `New-OperatorCodeCommitRecords` — `tools/check-complexity.ps1:3181-3206`, CC 8, NLOC 25, params 1
- `obs_engine::Engine::v2_filter_register_source_filters` — `engine/runtime_filter_v2.cpp:1286-1310`, CC 8, NLOC 24, params 2
- `obs_engine::publish_media_batch` — `engine/runtime_media_v2.cpp:368-391`, CC 8, NLOC 24, params 3
- `obs_engine::publish_source_events` — `engine/runtime_source_v2.cpp:563-590`, CC 8, NLOC 24, params 3
- `obs_engine::Engine::v2_source_rename` — `engine/runtime_source_v2.cpp:1111-1136`, CC 8, NLOC 23, params 3
- `Test-MetricInScope` — `tools/check-complexity.ps1:1830-1852`, CC 8, NLOC 22, params 1
- `obs_engine::decode_utf8_lead` — `engine/runtime_interaction_v2.cpp:233-251`, CC 8, NLOC 19, params 3
- `obs_engine::wait_for_media_action` — `engine/runtime_media_v2.cpp:605-623`, CC 8, NLOC 19, params 5
- `Invoke-Task7LiveRefresh` — `.github/scripts/engine-protocol-v2-task7-properties-smoke.ps1:226-243`, CC 8, NLOC 17, params 1
- `check_frame_rate_schema` — `engine/properties_test.cpp:179-195`, CC 8, NLOC 17, params 1
- `Stop-Task11AfterFailure` — `.github/scripts/engine-protocol-v2-task11.ps1:930-944`, CC 8, NLOC 15, params 1
- `obs_engine::Engine::v2_prepare_filter_settlement` — `engine/runtime_filter_v2.cpp:1583-1597`, CC 8, NLOC 15, params 5
- `Invoke-Task8Rename` — `.github/scripts/engine-protocol-v2-task8-source-smoke.ps1:198-211`, CC 8, NLOC 14, params 1
- `obs_engine::parse_handle_text` — `engine/runtime_filter_v2.cpp:180-193`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_interaction_v2.cpp:149-162`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_media_v2.cpp:145-160`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_properties_v2.cpp:94-107`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_source_settle_v2.cpp:56-69`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_source_v2.cpp:110-123`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_v2.cpp:85-100`, CC 8, NLOC 14, params 2
- `obs_engine::read_mouse_wheel_input` — `engine/runtime_interaction_v2.cpp:616-628`, CC 8, NLOC 13, params 4
- `Stop-Task10AfterFailure` — `.github/scripts/engine-protocol-v2-task10.ps1:643-654`, CC 8, NLOC 12, params 1
- `Assert-Task8BridgePreRequestOrdering` — `.github/scripts/engine-protocol-v2-task8-concurrency-bridge-audit.ps1:24-35`, CC 8, NLOC 12, params 1
- `Assert-Task8BridgePostRequestOrdering` — `.github/scripts/engine-protocol-v2-task8-concurrency-bridge-audit.ps1:37-48`, CC 8, NLOC 12, params 1
- `Stop-Task8AfterFailure` — `.github/scripts/engine-protocol-v2-task8-concurrency.ps1:307-318`, CC 8, NLOC 12, params 1
- `check_telemetry_output` — `engine/events_test.cpp:185-196`, CC 8, NLOC 12, params 1
- `obs_engine::is_valid_utf8_scalar` — `engine/runtime_interaction_v2.cpp:253-264`, CC 8, NLOC 12, params 2
- `Assert-Task3Hello` — `.github/scripts/engine-protocol-v2-task3-capabilities.ps1:65-75`, CC 8, NLOC 11, params 1
- `obs_engine::batch_matches_filter_update` — `engine/runtime_filter_v2.cpp:1087-1096`, CC 8, NLOC 10, params 5

## Remaining functions with CC > 10

- `obs_source_destroy_defer` — `libobs/obs-source.c:813-893`, CC 13, NLOC 65, params 1

## Intentional exceptions

| File | Function | Measured CC | Reason | Date/task | Reviewer note |
|---|---|---:|---|---|---|
| `libobs/obs-source.c` | `obs_source_destroy_defer` | 13 | Upstream source destruction pipeline; the operator-attributed change is only the accepted deferred_update_mutex destruction line. Further extraction would rewrite lifetime/resource/mutex teardown ordering and create metric-only risk. | 2026-08-30 Phase-1 complexity hardening | Review item: preserve exact libobs teardown order; no metric-only rewrite. |

See `complexity-ownership-inventory.md` for the complete Git-derived attribution scope.