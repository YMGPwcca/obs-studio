# Phase-1 Cyclomatic Complexity Baseline

Accepted production checkpoint: `3fc2e678d10809a4dca8b28107710534160803ab`
Ownership base: `bcd53e2914c68a62b2a9387a7e8ee3b59d1fd1df` (merge-base of `origin/master` and accepted HEAD)

## Ownership and measurement scope

Operator-authored commits in lineage: **64**
Operator-authored commits touching scoped executable code: **29**
Current executable files in scope: **41**

The complete authorship and file inventory is in `complexity-ownership-inventory.json` and `.md`. Scope was derived from Git history and current `git blame`; it was not inferred from filenames alone.

## Analyzer

- C/C++: lizard 1.24.0 (C/C++ parser, CSV output, default CCN)
- PowerShell: PowerShell 7.6.4 System.Management.Automation.Language.Parser AST
- CMake/YAML/JSON/Markdown/license files are recorded for review but are not included in function-level CC statistics.

## Baseline summary

| Measure | Value |
|---|---:|
| Scoped functions | 543 |
| Average CC | 5.703 |
| Median CC | 4 |
| 90th percentile CC (nearest rank) | 13 |
| Maximum CC | 61 |
| Functions with CC > 5 | 171 |
| Functions with CC > 7 | 115 |
| Functions with CC > 10 | 71 |

The p90 uses nearest-rank: `ceil(0.90 * N)`. Statistics exclude PowerShell top-level script bodies, which are reported separately because cyclomatic complexity is a function-level metric.

## Functions with CC > 5

- `obs_engine::classify_method` — `engine/protocol_v2.cpp:158-281`, CC 61, NLOC 124, params 1
- `main` — `engine/properties_test.cpp:48-224`, CC 54, NLOC 156, params 0
- `obs_engine::execute_runtime_method` — `engine/protocol_v2.cpp:390-505`, CC 54, NLOC 116, params 5
- `obs_engine::method_is_runtime` — `engine/protocol_v2.cpp:315-375`, CC 54, NLOC 61, params 1
- `obs_engine::handle_v2_request` — `engine/protocol_v2.cpp:803-956`, CC 39, NLOC 138, params 4
- `obs_engine::validate_property_item` — `engine/properties.cpp:350-443`, CC 38, NLOC 91, params 3
- `obs_engine::publish_filter_events` — `engine/runtime_filter_v2.cpp:466-579`, CC 34, NLOC 106, params 7
- `obs_engine::publish_media_events` — `engine/runtime_media_v2.cpp:420-507`, CC 31, NLOC 82, params 5
- `obs_engine::Engine::v2_sync_filter_observers` — `engine/runtime_filter_v2.cpp:1125-1225`, CC 28, NLOC 97, params 0
- `obs_engine::Engine::v2_build_property_target` — `engine/runtime_properties_v2.cpp:210-299`, CC 27, NLOC 86, params 6
- `obs_engine::Engine::v2_item_set_transform` — `engine/runtime_v2.cpp:529-617`, CC 25, NLOC 79, params 3
- `obs_engine::collect_media_signal` — `engine/runtime_media_v2.cpp:586-649`, CC 24, NLOC 57, params 3
- `obs_engine::method_is_mutating` — `engine/protocol_v2.cpp:283-313`, CC 24, NLOC 31, params 1
- `obs_engine::collect_source_signal` — `engine/runtime_source_v2.cpp:495-572`, CC 23, NLOC 66, params 2
- `obs_engine::collect_filter_signal` — `engine/runtime_filter_v2.cpp:648-736`, CC 22, NLOC 81, params 3
- `obs_engine::Engine::v2_interaction_key` — `engine/runtime_interaction_v2.cpp:535-604`, CC 22, NLOC 64, params 3
- `obs_engine::Engine::v2_interaction_mouse_button` — `engine/runtime_interaction_v2.cpp:433-497`, CC 22, NLOC 60, params 3
- `obs_engine::serialize_property` — `engine/properties.cpp:188-295`, CC 21, NLOC 104, params 3
- `obs_engine::Engine::v2_filter_create` — `engine/runtime_filter_v2.cpp:1461-1529`, CC 21, NLOC 64, params 3
- `obs_engine::Engine::v2_source_load_state` — `engine/runtime_source_v2.cpp:1224-1274`, CC 21, NLOC 48, params 3
- `obs_engine::decode_utf8_scalars` — `engine/runtime_interaction_v2.cpp:233-272`, CC 21, NLOC 40, params 3
- `obs_source_media_action_enqueue` — `libobs/obs-source.c:5858-5924`, CC 20, NLOC 64, params 4
- `obs_engine::execute_filter_method` — `engine/protocol_filter_v2.cpp:213-260`, CC 20, NLOC 48, params 5
- `obs_engine::classify_filter_method` — `engine/protocol_filter_v2.cpp:144-185`, CC 20, NLOC 42, params 1
- `obs_engine::Engine::command_item_transform` — `engine/runtime.cpp:502-570`, CC 18, NLOC 65, params 2
- `obs_engine::Engine::v2_properties_invoke_button` — `engine/runtime_properties_v2.cpp:440-513`, CC 18, NLOC 63, params 3
- `obs_engine::Engine::v2_filter_duplicate` — `engine/runtime_filter_v2.cpp:1591-1650`, CC 18, NLOC 60, params 3
- `obs_engine::settle_deferred_filter_update` — `engine/runtime_filter_v2.cpp:883-936`, CC 18, NLOC 50, params 6
- `obs_engine::Engine::handle` — `engine/runtime.cpp:118-166`, CC 18, NLOC 46, params 1
- `test_telemetry_policy` — `engine/events_test.cpp:153-194`, CC 18, NLOC 40, params 0
- `obs_engine::parse_v2_request` — `engine/protocol_v2.cpp:724-762`, CC 18, NLOC 39, params 3
- `obs_engine::Engine::v2_sync_media_observers` — `engine/runtime_media_v2.cpp:846-908`, CC 17, NLOC 59, params 0
- `obs_engine::settle_media_action` — `engine/runtime_media_v2.cpp:530-584`, CC 17, NLOC 53, params 7
- `obs_engine::handle_filter_request` — `engine/protocol_filter_v2.cpp:378-438`, CC 17, NLOC 51, params 5
- `main` — `engine/host.cpp:195-273`, CC 16, NLOC 69, params 2
- `obs_engine::publish_source_events` — `engine/runtime_source_v2.cpp:435-493`, CC 16, NLOC 50, params 3
- `obs_engine::Engine::v2_settle_filter_mutation` — `engine/runtime_filter_v2.cpp:1304-1341`, CC 16, NLOC 36, params 3
- `Run-RaceScenario` — `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1:145-257`, CC 15, NLOC 98, params 2
- `obs_engine::Engine::v2_sync_source_observers` — `engine/runtime_source_v2.cpp:738-799`, CC 15, NLOC 58, params 0
- `obs_engine::parse_args` — `engine/config.cpp:35-85`, CC 15, NLOC 48, params 3
- `obs_source_init` — `libobs/obs-source.c:218-270`, CC 15, NLOC 47, params 1
- `obs_engine::publish_deferred_media_snapshot` — `engine/runtime_media_v2.cpp:375-418`, CC 15, NLOC 42, params 2
- `obs_source_update_internal` — `libobs/obs-source.c:1099-1146`, CC 15, NLOC 42, params 5
- `obs_engine::Engine::start` — `engine/runtime.cpp:46-116`, CC 14, NLOC 63, params 0
- `obs_engine::Engine::v2_source_duplicate` — `engine/runtime_source_v2.cpp:904-967`, CC 14, NLOC 54, params 3
- `obs_engine::Engine::v2_filter_patch_settings` — `engine/runtime_filter_v2.cpp:1673-1716`, CC 14, NLOC 44, params 3
- `obs_engine::take_deferred_media_events` — `engine/runtime_media_v2.cpp:333-373`, CC 14, NLOC 38, params 3
- `obs_engine::property_type_name` — `engine/properties.cpp:17-49`, CC 14, NLOC 33, params 1
- `obs_engine::Engine::v2_media_toggle_pause` — `engine/runtime_media_v2.cpp:1027-1060`, CC 14, NLOC 33, params 3
- `obs_engine::is_event_name` — `engine/events.cpp:14-35`, CC 14, NLOC 20, params 1
- `obs_engine::is_safe_identifier` — `engine/validation.hpp:7-22`, CC 14, NLOC 15, params 2
- `obs_source_destroy_defer` — `libobs/obs-source.c:806-886`, CC 13, NLOC 65, params 1
- `obs_engine::Engine::v2_source_create` — `engine/runtime_v2.cpp:209-263`, CC 13, NLOC 47, params 3
- `obs_engine::Engine::v2_filter_replace_settings` — `engine/runtime_filter_v2.cpp:1718-1759`, CC 13, NLOC 42, params 3
- `obs_engine::take_deferred_filter_events` — `engine/runtime_filter_v2.cpp:381-424`, CC 13, NLOC 41, params 3
- `obs_engine::Engine::v2_filter_register_source_filters` — `engine/runtime_filter_v2.cpp:1036-1078`, CC 13, NLOC 41, params 2
- `obs_engine::Engine::v2_media_set_position` — `engine/runtime_media_v2.cpp:1205-1244`, CC 13, NLOC 39, params 3
- `Read-Event` — `.github/scripts/engine-protocol-v2-task11.ps1:102-135`, CC 13, NLOC 34, params 4
- `Read-SafeSettingsEvent` — `.github/scripts/engine-protocol-v2-task11.ps1:192-223`, CC 13, NLOC 32, params 3
- `test_patterns_and_overlap` — `engine/events_test.cpp:39-70`, CC 13, NLOC 30, params 0
- `obs_engine::is_mutating` — `engine/protocol_filter_v2.cpp:187-206`, CC 13, NLOC 20, params 1
- `obs_module_load` — `plugins/win-capture/plugin-main.c:112-176`, CC 12, NLOC 53, params 0
- `obs_engine::EventDispatcher::publish` — `engine/events.cpp:171-229`, CC 12, NLOC 52, params 4
- `obs_engine::parse_subscription_list` — `engine/protocol_v2.cpp:613-650`, CC 12, NLOC 38, params 3
- `obs_engine::publish_deferred_filter_snapshot` — `engine/runtime_filter_v2.cpp:426-464`, CC 12, NLOC 38, params 2
- `obs_engine::Engine::v2_filter_set_order` — `engine/runtime_filter_v2.cpp:1807-1839`, CC 12, NLOC 33, params 3
- `obs_engine::list_value_is_enabled` — `engine/properties.cpp:316-348`, CC 12, NLOC 32, params 2
- `process_media_actions` — `libobs/obs-source.c:1379-1428`, CC 11, NLOC 45, params 1
- `obs_engine::EventDispatcher::run` — `engine/events.cpp:280-317`, CC 11, NLOC 35, params 0
- `Read-Event` — `.github/scripts/engine-protocol-v2-task10.ps1:87-116`, CC 11, NLOC 30, params 3
- `test_state_prefers_telemetry_eviction` — `engine/events_test.cpp:99-126`, CC 11, NLOC 27, params 0
- `obs_engine::Engine::v2_interaction_reset` — `engine/runtime_interaction_v2.cpp:638-705`, CC 10, NLOC 62, params 3
- `obs_engine::Engine::v2_scene_remove` — `engine/runtime_v2.cpp:416-465`, CC 10, NLOC 43, params 3
- `obs_engine::Engine::v2_scene_create` — `engine/runtime_v2.cpp:372-414`, CC 10, NLOC 37, params 3
- `obs_engine::Engine::v2_interaction_mouse_wheel` — `engine/runtime_interaction_v2.cpp:499-533`, CC 10, NLOC 34, params 3
- `obs_engine::take_deferred_source_events` — `engine/runtime_source_v2.cpp:366-403`, CC 10, NLOC 34, params 3
- `obs_engine::Engine::v2_interaction_text` — `engine/runtime_interaction_v2.cpp:606-636`, CC 10, NLOC 30, params 3
- `obs_engine::Engine::v2_filter_rename` — `engine/runtime_filter_v2.cpp:1561-1589`, CC 10, NLOC 29, params 3
- `obs_engine::read_line_limited` — `engine/protocol.cpp:116-145`, CC 10, NLOC 26, params 1
- `obs_engine::filter_settings_event_matches` — `engine/runtime_filter_v2.cpp:825-841`, CC 10, NLOC 17, params 4
- `obs_engine::is_action_signal` — `engine/runtime_media_v2.cpp:229-245`, CC 10, NLOC 17, params 1
- `obs_engine::action_requires_resync` — `engine/runtime_media_v2.cpp:247-263`, CC 10, NLOC 17, params 1
- `obs_engine::Engine::v2_source_remove` — `engine/runtime_v2.cpp:325-370`, CC 9, NLOC 40, params 3
- `obs_engine::Engine::v2_source_get_missing_files` — `engine/runtime_source_v2.cpp:1152-1192`, CC 9, NLOC 39, params 3
- `obs_engine::Engine::v2_filter_prepare_parent_removal` — `engine/runtime_filter_v2.cpp:1265-1302`, CC 9, NLOC 36, params 2
- `obs_engine::Engine::v2_interaction_mouse_move` — `engine/runtime_interaction_v2.cpp:396-431`, CC 9, NLOC 35, params 3
- `obs_engine::Engine::command_source_create` — `engine/runtime.cpp:266-301`, CC 9, NLOC 32, params 2
- `obs_engine::publish_deferred_source_snapshot` — `engine/runtime_source_v2.cpp:405-433`, CC 9, NLOC 27, params 2
- `obs_engine::canonicalize_source_result` — `engine/runtime_source_settle_v2.cpp:228-256`, CC 9, NLOC 26, params 2
- `obs_engine::Engine::v2_settle_source_mutation` — `engine/runtime_source_settle_v2.cpp:314-340`, CC 9, NLOC 25, params 2
- `obs_engine::Engine::v2_source_rename` — `engine/runtime_source_v2.cpp:969-996`, CC 9, NLOC 25, params 3
- `obs_engine::media_state_name` — `engine/runtime_media_v2.cpp:174-196`, CC 9, NLOC 23, params 1
- `obs_engine::strip_inline_list_items` — `engine/runtime_properties_v2.cpp:124-145`, CC 9, NLOC 21, params 1
- `obs_engine::collect_sensitive_recursive` — `engine/properties_sensitive.cpp:6-22`, CC 9, NLOC 17, params 2
- `obs_engine::read_candidate_params` — `engine/runtime_properties_v2.cpp:169-183`, CC 9, NLOC 15, params 7
- `obs_engine::parse_u32` — `engine/config.cpp:18-31`, CC 9, NLOC 12, params 4
- `obs_engine::Engine::v2_properties_get_list_items` — `engine/runtime_properties_v2.cpp:344-383`, CC 8, NLOC 37, params 3
- `obs_engine::Engine::v2_item_create` — `engine/runtime_v2.cpp:467-506`, CC 8, NLOC 33, params 3
- `obs_engine::serialize_property_list_items` — `engine/properties.cpp:482-513`, CC 8, NLOC 31, params 1
- `obs_engine::Engine::v2_media_play` — `engine/runtime_media_v2.cpp:961-992`, CC 8, NLOC 31, params 3
- `obs_engine::Engine::v2_media_pause` — `engine/runtime_media_v2.cpp:994-1025`, CC 8, NLOC 31, params 3
- `obs_engine::Engine::v2_media_stop` — `engine/runtime_media_v2.cpp:1062-1092`, CC 8, NLOC 30, params 3
- `obs_engine::Engine::command_scene_create` — `engine/runtime.cpp:394-423`, CC 8, NLOC 27, params 2
- `Read-Until-Resync` — `.github/scripts/engine-protocol-v2-task11.ps1:156-181`, CC 8, NLOC 26, params 1
- `Read-Until-Resync` — `.github/scripts/engine-protocol-v2-task10.ps1:118-142`, CC 8, NLOC 25, params 1
- `Read-EngineMessage` — `.github/scripts/engine-protocol-v2-task11.ps1:40-64`, CC 8, NLOC 25, params 1
- `test_state_overflow_requires_resync` — `engine/events_test.cpp:72-97`, CC 8, NLOC 25, params 0
- `obs_engine::parse_handle_text` — `engine/runtime_filter_v2.cpp:180-193`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_interaction_v2.cpp:149-162`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_media_v2.cpp:144-159`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_properties_v2.cpp:94-107`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_source_settle_v2.cpp:56-69`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_source_v2.cpp:110-123`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_v2.cpp:85-100`, CC 8, NLOC 14, params 2
- `obs_engine::batch_matches_filter_update` — `engine/runtime_filter_v2.cpp:851-860`, CC 8, NLOC 10, params 5
- `obs_engine::Engine::v2_prepare_filter_shutdown` — `engine/runtime_filter_v2.cpp:1227-1263`, CC 7, NLOC 36, params 0
- `dispatch_request` — `engine/host.cpp:156-191`, CC 7, NLOC 35, params 5
- `obs_engine::Engine::v2_filter_list` — `engine/runtime_filter_v2.cpp:1409-1442`, CC 7, NLOC 33, params 3
- `obs_engine::Engine::command_scene_add` — `engine/runtime.cpp:448-483`, CC 7, NLOC 33, params 2
- `obs_engine::EventDispatcher::subscribe` — `engine/events.cpp:99-131`, CC 7, NLOC 31, params 2
- `obs_engine::settle_deferred_source_update` — `engine/runtime_source_settle_v2.cpp:181-226`, CC 7, NLOC 31, params 5
- `obs_engine::Engine::v2_filter_set_enabled` — `engine/runtime_filter_v2.cpp:1761-1787`, CC 7, NLOC 27, params 3
- `obs_engine::Engine::v2_filter_move_up` — `engine/runtime_filter_v2.cpp:1841-1866`, CC 7, NLOC 26, params 3
- `obs_engine::Engine::v2_filter_move_down` — `engine/runtime_filter_v2.cpp:1868-1893`, CC 7, NLOC 26, params 3
- `obs_engine::Engine::v2_filter_move_top` — `engine/runtime_filter_v2.cpp:1895-1920`, CC 7, NLOC 26, params 3
- `obs_engine::Engine::v2_filter_move_bottom` — `engine/runtime_filter_v2.cpp:1922-1947`, CC 7, NLOC 26, params 3
- `obs_engine::Engine::v2_end_event_capture` — `engine/runtime_source_v2.cpp:690-716`, CC 7, NLOC 26, params 0
- `obs_engine::mark_filter_settlement_lost` — `engine/runtime_filter_v2.cpp:938-966`, CC 7, NLOC 24, params 4
- `obs_engine::Engine::v2_end_filter_event_capture` — `engine/runtime_filter_v2.cpp:999-1022`, CC 7, NLOC 24, params 0
- `obs_engine::Engine::v2_end_media_event_capture` — `engine/runtime_media_v2.cpp:803-828`, CC 7, NLOC 24, params 0
- `obs_engine::resolve_property_schema` — `engine/properties.cpp:534-554`, CC 7, NLOC 20, params 4
- `obs_engine::promote_deferred_filter_update` — `engine/runtime_filter_v2.cpp:862-881`, CC 7, NLOC 20, params 6
- `obs_engine::parse_modifiers` — `engine/runtime_interaction_v2.cpp:212-231`, CC 7, NLOC 19, params 3
- `obs_engine::normalize_kind_entry` — `engine/runtime_source_settle_v2.cpp:292-310`, CC 7, NLOC 19, params 1
- `obs_engine::resolve_uncertain_filter_updates_locked` — `engine/runtime_filter_v2.cpp:334-352`, CC 7, NLOC 18, params 3
- `obs_engine::promote_deferred_media_action_locked` — `engine/runtime_media_v2.cpp:509-527`, CC 7, NLOC 18, params 5
- `Read-StateEvent` — `.github/scripts/engine-protocol-v2-task8-concurrency.ps1:53-69`, CC 7, NLOC 17, params 3
- `obs_engine::validate_property_patch` — `engine/properties.cpp:515-532`, CC 7, NLOC 17, params 2
- `obs_engine::batch_matches_source_update` — `engine/runtime_source_settle_v2.cpp:120-134`, CC 7, NLOC 15, params 3
- `Start-EngineCase` — `.github/scripts/engine-protocol-v2-task8-concurrency.ps1:71-121`, CC 6, NLOC 44, params 1
- `obs_engine::Engine::v2_properties_validate` — `engine/runtime_properties_v2.cpp:385-423`, CC 6, NLOC 33, params 3
- `obs_engine::EventDispatcher::emit` — `engine/events.cpp:319-351`, CC 6, NLOC 31, params 1
- `obs_engine::Engine::v2_prepare_media_shutdown` — `engine/runtime_media_v2.cpp:910-942`, CC 6, NLOC 31, params 0
- `obs_engine::Engine::v2_prepare_shutdown` — `engine/runtime_source_v2.cpp:801-833`, CC 6, NLOC 31, params 0
- `obs_engine::Engine::v2_source_patch_settings` — `engine/runtime_v2.cpp:287-323`, CC 6, NLOC 28, params 3
- `obs_engine::Engine::v2_filter_kind_list` — `engine/runtime_filter_v2.cpp:1343-1369`, CC 6, NLOC 27, params 2
- `obs_engine::Engine::v2_source_kind_list` — `engine/runtime_v2.cpp:156-184`, CC 6, NLOC 26, params 2
- `obs_engine::Engine::command_source_types` — `engine/runtime.cpp:224-248`, CC 6, NLOC 25, params 1
- `test_ordered_resync_preserves_queued_event` — `engine/events_test.cpp:128-151`, CC 6, NLOC 24, params 0
- `obs_engine::Engine::v2_filter_order_data` — `engine/runtime_filter_v2.cpp:597-621`, CC 6, NLOC 24, params 3
- `obs_engine::Engine::v2_media_restart` — `engine/runtime_media_v2.cpp:1094-1117`, CC 6, NLOC 24, params 3
- `obs_engine::Engine::v2_media_next` — `engine/runtime_media_v2.cpp:1119-1142`, CC 6, NLOC 24, params 3
- `obs_engine::Engine::v2_media_previous` — `engine/runtime_media_v2.cpp:1144-1167`, CC 6, NLOC 24, params 3
- `obs_engine::Engine::v2_source_replace_settings` — `engine/runtime_source_v2.cpp:998-1024`, CC 6, NLOC 24, params 3
- `Run-CaseE` — `.github/scripts/engine-protocol-v2-task8-concurrency.ps1:251-276`, CC 6, NLOC 23, params 1
- `obs_source_deferred_update` — `libobs/obs-source.c:1148-1171`, CC 6, NLOC 22, params 1
- `obs_engine::read_settings_json` — `engine/runtime_source_settle_v2.cpp:88-108`, CC 6, NLOC 21, params 2
- `obs_engine::Engine::v2_normalize_source_kind_metadata` — `engine/runtime_source_settle_v2.cpp:342-364`, CC 6, NLOC 21, params 1
- `obs_engine::read_finite_double` — `engine/protocol.cpp:216-235`, CC 6, NLOC 20, params 6
- `obs_engine::collect_filter_ref` — `engine/runtime_filter_v2.cpp:628-646`, CC 6, NLOC 19, params 2
- `obs_engine::Engine::v2_filter_kind_defaults` — `engine/runtime_filter_v2.cpp:1371-1389`, CC 6, NLOC 19, params 3
- `obs_engine::Engine::v2_source_kind_defaults` — `engine/runtime_v2.cpp:186-207`, CC 6, NLOC 19, params 3
- `obs_engine::FilterCallbackScope::FilterCallbackScope` — `engine/runtime_filter_v2.cpp:272-286`, CC 6, NLOC 15, params 2
- `obs_engine::MediaCallbackScope::MediaCallbackScope` — `engine/runtime_media_v2.cpp:275-290`, CC 6, NLOC 15, params 2
- `obs_engine::SourceCallbackScope::SourceCallbackScope` — `engine/runtime_source_v2.cpp:310-324`, CC 6, NLOC 15, params 2
- `obs_engine::erase_password_settings` — `engine/properties.cpp:160-172`, CC 6, NLOC 13, params 2
- `obs_engine::find_unversioned_input_id` — `engine/runtime_source_settle_v2.cpp:279-290`, CC 6, NLOC 12, params 1
- `obs_engine::request_handle` — `engine/protocol.cpp:204-214`, CC 6, NLOC 11, params 4
- `main` — `engine/events_test.cpp:208-216`, CC 6, NLOC 9, params 0
- `obs_engine::point_inside_source` — `engine/runtime_interaction_v2.cpp:295-302`, CC 6, NLOC 8, params 3
- `obs_engine::key_matches` — `engine/runtime_interaction_v2.cpp:274-279`, CC 6, NLOC 6, params 4

## Functions with CC > 7

- `obs_engine::classify_method` — `engine/protocol_v2.cpp:158-281`, CC 61, NLOC 124, params 1
- `main` — `engine/properties_test.cpp:48-224`, CC 54, NLOC 156, params 0
- `obs_engine::execute_runtime_method` — `engine/protocol_v2.cpp:390-505`, CC 54, NLOC 116, params 5
- `obs_engine::method_is_runtime` — `engine/protocol_v2.cpp:315-375`, CC 54, NLOC 61, params 1
- `obs_engine::handle_v2_request` — `engine/protocol_v2.cpp:803-956`, CC 39, NLOC 138, params 4
- `obs_engine::validate_property_item` — `engine/properties.cpp:350-443`, CC 38, NLOC 91, params 3
- `obs_engine::publish_filter_events` — `engine/runtime_filter_v2.cpp:466-579`, CC 34, NLOC 106, params 7
- `obs_engine::publish_media_events` — `engine/runtime_media_v2.cpp:420-507`, CC 31, NLOC 82, params 5
- `obs_engine::Engine::v2_sync_filter_observers` — `engine/runtime_filter_v2.cpp:1125-1225`, CC 28, NLOC 97, params 0
- `obs_engine::Engine::v2_build_property_target` — `engine/runtime_properties_v2.cpp:210-299`, CC 27, NLOC 86, params 6
- `obs_engine::Engine::v2_item_set_transform` — `engine/runtime_v2.cpp:529-617`, CC 25, NLOC 79, params 3
- `obs_engine::collect_media_signal` — `engine/runtime_media_v2.cpp:586-649`, CC 24, NLOC 57, params 3
- `obs_engine::method_is_mutating` — `engine/protocol_v2.cpp:283-313`, CC 24, NLOC 31, params 1
- `obs_engine::collect_source_signal` — `engine/runtime_source_v2.cpp:495-572`, CC 23, NLOC 66, params 2
- `obs_engine::collect_filter_signal` — `engine/runtime_filter_v2.cpp:648-736`, CC 22, NLOC 81, params 3
- `obs_engine::Engine::v2_interaction_key` — `engine/runtime_interaction_v2.cpp:535-604`, CC 22, NLOC 64, params 3
- `obs_engine::Engine::v2_interaction_mouse_button` — `engine/runtime_interaction_v2.cpp:433-497`, CC 22, NLOC 60, params 3
- `obs_engine::serialize_property` — `engine/properties.cpp:188-295`, CC 21, NLOC 104, params 3
- `obs_engine::Engine::v2_filter_create` — `engine/runtime_filter_v2.cpp:1461-1529`, CC 21, NLOC 64, params 3
- `obs_engine::Engine::v2_source_load_state` — `engine/runtime_source_v2.cpp:1224-1274`, CC 21, NLOC 48, params 3
- `obs_engine::decode_utf8_scalars` — `engine/runtime_interaction_v2.cpp:233-272`, CC 21, NLOC 40, params 3
- `obs_source_media_action_enqueue` — `libobs/obs-source.c:5858-5924`, CC 20, NLOC 64, params 4
- `obs_engine::execute_filter_method` — `engine/protocol_filter_v2.cpp:213-260`, CC 20, NLOC 48, params 5
- `obs_engine::classify_filter_method` — `engine/protocol_filter_v2.cpp:144-185`, CC 20, NLOC 42, params 1
- `obs_engine::Engine::command_item_transform` — `engine/runtime.cpp:502-570`, CC 18, NLOC 65, params 2
- `obs_engine::Engine::v2_properties_invoke_button` — `engine/runtime_properties_v2.cpp:440-513`, CC 18, NLOC 63, params 3
- `obs_engine::Engine::v2_filter_duplicate` — `engine/runtime_filter_v2.cpp:1591-1650`, CC 18, NLOC 60, params 3
- `obs_engine::settle_deferred_filter_update` — `engine/runtime_filter_v2.cpp:883-936`, CC 18, NLOC 50, params 6
- `obs_engine::Engine::handle` — `engine/runtime.cpp:118-166`, CC 18, NLOC 46, params 1
- `test_telemetry_policy` — `engine/events_test.cpp:153-194`, CC 18, NLOC 40, params 0
- `obs_engine::parse_v2_request` — `engine/protocol_v2.cpp:724-762`, CC 18, NLOC 39, params 3
- `obs_engine::Engine::v2_sync_media_observers` — `engine/runtime_media_v2.cpp:846-908`, CC 17, NLOC 59, params 0
- `obs_engine::settle_media_action` — `engine/runtime_media_v2.cpp:530-584`, CC 17, NLOC 53, params 7
- `obs_engine::handle_filter_request` — `engine/protocol_filter_v2.cpp:378-438`, CC 17, NLOC 51, params 5
- `main` — `engine/host.cpp:195-273`, CC 16, NLOC 69, params 2
- `obs_engine::publish_source_events` — `engine/runtime_source_v2.cpp:435-493`, CC 16, NLOC 50, params 3
- `obs_engine::Engine::v2_settle_filter_mutation` — `engine/runtime_filter_v2.cpp:1304-1341`, CC 16, NLOC 36, params 3
- `Run-RaceScenario` — `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1:145-257`, CC 15, NLOC 98, params 2
- `obs_engine::Engine::v2_sync_source_observers` — `engine/runtime_source_v2.cpp:738-799`, CC 15, NLOC 58, params 0
- `obs_engine::parse_args` — `engine/config.cpp:35-85`, CC 15, NLOC 48, params 3
- `obs_source_init` — `libobs/obs-source.c:218-270`, CC 15, NLOC 47, params 1
- `obs_engine::publish_deferred_media_snapshot` — `engine/runtime_media_v2.cpp:375-418`, CC 15, NLOC 42, params 2
- `obs_source_update_internal` — `libobs/obs-source.c:1099-1146`, CC 15, NLOC 42, params 5
- `obs_engine::Engine::start` — `engine/runtime.cpp:46-116`, CC 14, NLOC 63, params 0
- `obs_engine::Engine::v2_source_duplicate` — `engine/runtime_source_v2.cpp:904-967`, CC 14, NLOC 54, params 3
- `obs_engine::Engine::v2_filter_patch_settings` — `engine/runtime_filter_v2.cpp:1673-1716`, CC 14, NLOC 44, params 3
- `obs_engine::take_deferred_media_events` — `engine/runtime_media_v2.cpp:333-373`, CC 14, NLOC 38, params 3
- `obs_engine::property_type_name` — `engine/properties.cpp:17-49`, CC 14, NLOC 33, params 1
- `obs_engine::Engine::v2_media_toggle_pause` — `engine/runtime_media_v2.cpp:1027-1060`, CC 14, NLOC 33, params 3
- `obs_engine::is_event_name` — `engine/events.cpp:14-35`, CC 14, NLOC 20, params 1
- `obs_engine::is_safe_identifier` — `engine/validation.hpp:7-22`, CC 14, NLOC 15, params 2
- `obs_source_destroy_defer` — `libobs/obs-source.c:806-886`, CC 13, NLOC 65, params 1
- `obs_engine::Engine::v2_source_create` — `engine/runtime_v2.cpp:209-263`, CC 13, NLOC 47, params 3
- `obs_engine::Engine::v2_filter_replace_settings` — `engine/runtime_filter_v2.cpp:1718-1759`, CC 13, NLOC 42, params 3
- `obs_engine::take_deferred_filter_events` — `engine/runtime_filter_v2.cpp:381-424`, CC 13, NLOC 41, params 3
- `obs_engine::Engine::v2_filter_register_source_filters` — `engine/runtime_filter_v2.cpp:1036-1078`, CC 13, NLOC 41, params 2
- `obs_engine::Engine::v2_media_set_position` — `engine/runtime_media_v2.cpp:1205-1244`, CC 13, NLOC 39, params 3
- `Read-Event` — `.github/scripts/engine-protocol-v2-task11.ps1:102-135`, CC 13, NLOC 34, params 4
- `Read-SafeSettingsEvent` — `.github/scripts/engine-protocol-v2-task11.ps1:192-223`, CC 13, NLOC 32, params 3
- `test_patterns_and_overlap` — `engine/events_test.cpp:39-70`, CC 13, NLOC 30, params 0
- `obs_engine::is_mutating` — `engine/protocol_filter_v2.cpp:187-206`, CC 13, NLOC 20, params 1
- `obs_module_load` — `plugins/win-capture/plugin-main.c:112-176`, CC 12, NLOC 53, params 0
- `obs_engine::EventDispatcher::publish` — `engine/events.cpp:171-229`, CC 12, NLOC 52, params 4
- `obs_engine::parse_subscription_list` — `engine/protocol_v2.cpp:613-650`, CC 12, NLOC 38, params 3
- `obs_engine::publish_deferred_filter_snapshot` — `engine/runtime_filter_v2.cpp:426-464`, CC 12, NLOC 38, params 2
- `obs_engine::Engine::v2_filter_set_order` — `engine/runtime_filter_v2.cpp:1807-1839`, CC 12, NLOC 33, params 3
- `obs_engine::list_value_is_enabled` — `engine/properties.cpp:316-348`, CC 12, NLOC 32, params 2
- `process_media_actions` — `libobs/obs-source.c:1379-1428`, CC 11, NLOC 45, params 1
- `obs_engine::EventDispatcher::run` — `engine/events.cpp:280-317`, CC 11, NLOC 35, params 0
- `Read-Event` — `.github/scripts/engine-protocol-v2-task10.ps1:87-116`, CC 11, NLOC 30, params 3
- `test_state_prefers_telemetry_eviction` — `engine/events_test.cpp:99-126`, CC 11, NLOC 27, params 0
- `obs_engine::Engine::v2_interaction_reset` — `engine/runtime_interaction_v2.cpp:638-705`, CC 10, NLOC 62, params 3
- `obs_engine::Engine::v2_scene_remove` — `engine/runtime_v2.cpp:416-465`, CC 10, NLOC 43, params 3
- `obs_engine::Engine::v2_scene_create` — `engine/runtime_v2.cpp:372-414`, CC 10, NLOC 37, params 3
- `obs_engine::Engine::v2_interaction_mouse_wheel` — `engine/runtime_interaction_v2.cpp:499-533`, CC 10, NLOC 34, params 3
- `obs_engine::take_deferred_source_events` — `engine/runtime_source_v2.cpp:366-403`, CC 10, NLOC 34, params 3
- `obs_engine::Engine::v2_interaction_text` — `engine/runtime_interaction_v2.cpp:606-636`, CC 10, NLOC 30, params 3
- `obs_engine::Engine::v2_filter_rename` — `engine/runtime_filter_v2.cpp:1561-1589`, CC 10, NLOC 29, params 3
- `obs_engine::read_line_limited` — `engine/protocol.cpp:116-145`, CC 10, NLOC 26, params 1
- `obs_engine::filter_settings_event_matches` — `engine/runtime_filter_v2.cpp:825-841`, CC 10, NLOC 17, params 4
- `obs_engine::is_action_signal` — `engine/runtime_media_v2.cpp:229-245`, CC 10, NLOC 17, params 1
- `obs_engine::action_requires_resync` — `engine/runtime_media_v2.cpp:247-263`, CC 10, NLOC 17, params 1
- `obs_engine::Engine::v2_source_remove` — `engine/runtime_v2.cpp:325-370`, CC 9, NLOC 40, params 3
- `obs_engine::Engine::v2_source_get_missing_files` — `engine/runtime_source_v2.cpp:1152-1192`, CC 9, NLOC 39, params 3
- `obs_engine::Engine::v2_filter_prepare_parent_removal` — `engine/runtime_filter_v2.cpp:1265-1302`, CC 9, NLOC 36, params 2
- `obs_engine::Engine::v2_interaction_mouse_move` — `engine/runtime_interaction_v2.cpp:396-431`, CC 9, NLOC 35, params 3
- `obs_engine::Engine::command_source_create` — `engine/runtime.cpp:266-301`, CC 9, NLOC 32, params 2
- `obs_engine::publish_deferred_source_snapshot` — `engine/runtime_source_v2.cpp:405-433`, CC 9, NLOC 27, params 2
- `obs_engine::canonicalize_source_result` — `engine/runtime_source_settle_v2.cpp:228-256`, CC 9, NLOC 26, params 2
- `obs_engine::Engine::v2_settle_source_mutation` — `engine/runtime_source_settle_v2.cpp:314-340`, CC 9, NLOC 25, params 2
- `obs_engine::Engine::v2_source_rename` — `engine/runtime_source_v2.cpp:969-996`, CC 9, NLOC 25, params 3
- `obs_engine::media_state_name` — `engine/runtime_media_v2.cpp:174-196`, CC 9, NLOC 23, params 1
- `obs_engine::strip_inline_list_items` — `engine/runtime_properties_v2.cpp:124-145`, CC 9, NLOC 21, params 1
- `obs_engine::collect_sensitive_recursive` — `engine/properties_sensitive.cpp:6-22`, CC 9, NLOC 17, params 2
- `obs_engine::read_candidate_params` — `engine/runtime_properties_v2.cpp:169-183`, CC 9, NLOC 15, params 7
- `obs_engine::parse_u32` — `engine/config.cpp:18-31`, CC 9, NLOC 12, params 4
- `obs_engine::Engine::v2_properties_get_list_items` — `engine/runtime_properties_v2.cpp:344-383`, CC 8, NLOC 37, params 3
- `obs_engine::Engine::v2_item_create` — `engine/runtime_v2.cpp:467-506`, CC 8, NLOC 33, params 3
- `obs_engine::serialize_property_list_items` — `engine/properties.cpp:482-513`, CC 8, NLOC 31, params 1
- `obs_engine::Engine::v2_media_play` — `engine/runtime_media_v2.cpp:961-992`, CC 8, NLOC 31, params 3
- `obs_engine::Engine::v2_media_pause` — `engine/runtime_media_v2.cpp:994-1025`, CC 8, NLOC 31, params 3
- `obs_engine::Engine::v2_media_stop` — `engine/runtime_media_v2.cpp:1062-1092`, CC 8, NLOC 30, params 3
- `obs_engine::Engine::command_scene_create` — `engine/runtime.cpp:394-423`, CC 8, NLOC 27, params 2
- `Read-Until-Resync` — `.github/scripts/engine-protocol-v2-task11.ps1:156-181`, CC 8, NLOC 26, params 1
- `Read-Until-Resync` — `.github/scripts/engine-protocol-v2-task10.ps1:118-142`, CC 8, NLOC 25, params 1
- `Read-EngineMessage` — `.github/scripts/engine-protocol-v2-task11.ps1:40-64`, CC 8, NLOC 25, params 1
- `test_state_overflow_requires_resync` — `engine/events_test.cpp:72-97`, CC 8, NLOC 25, params 0
- `obs_engine::parse_handle_text` — `engine/runtime_filter_v2.cpp:180-193`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_interaction_v2.cpp:149-162`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_media_v2.cpp:144-159`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_properties_v2.cpp:94-107`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_source_settle_v2.cpp:56-69`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_source_v2.cpp:110-123`, CC 8, NLOC 14, params 2
- `obs_engine::parse_handle_text` — `engine/runtime_v2.cpp:85-100`, CC 8, NLOC 14, params 2
- `obs_engine::batch_matches_filter_update` — `engine/runtime_filter_v2.cpp:851-860`, CC 8, NLOC 10, params 5

## Functions with CC > 10

- `obs_engine::classify_method` — `engine/protocol_v2.cpp:158-281`, CC 61, NLOC 124, params 1
- `main` — `engine/properties_test.cpp:48-224`, CC 54, NLOC 156, params 0
- `obs_engine::execute_runtime_method` — `engine/protocol_v2.cpp:390-505`, CC 54, NLOC 116, params 5
- `obs_engine::method_is_runtime` — `engine/protocol_v2.cpp:315-375`, CC 54, NLOC 61, params 1
- `obs_engine::handle_v2_request` — `engine/protocol_v2.cpp:803-956`, CC 39, NLOC 138, params 4
- `obs_engine::validate_property_item` — `engine/properties.cpp:350-443`, CC 38, NLOC 91, params 3
- `obs_engine::publish_filter_events` — `engine/runtime_filter_v2.cpp:466-579`, CC 34, NLOC 106, params 7
- `obs_engine::publish_media_events` — `engine/runtime_media_v2.cpp:420-507`, CC 31, NLOC 82, params 5
- `obs_engine::Engine::v2_sync_filter_observers` — `engine/runtime_filter_v2.cpp:1125-1225`, CC 28, NLOC 97, params 0
- `obs_engine::Engine::v2_build_property_target` — `engine/runtime_properties_v2.cpp:210-299`, CC 27, NLOC 86, params 6
- `obs_engine::Engine::v2_item_set_transform` — `engine/runtime_v2.cpp:529-617`, CC 25, NLOC 79, params 3
- `obs_engine::collect_media_signal` — `engine/runtime_media_v2.cpp:586-649`, CC 24, NLOC 57, params 3
- `obs_engine::method_is_mutating` — `engine/protocol_v2.cpp:283-313`, CC 24, NLOC 31, params 1
- `obs_engine::collect_source_signal` — `engine/runtime_source_v2.cpp:495-572`, CC 23, NLOC 66, params 2
- `obs_engine::collect_filter_signal` — `engine/runtime_filter_v2.cpp:648-736`, CC 22, NLOC 81, params 3
- `obs_engine::Engine::v2_interaction_key` — `engine/runtime_interaction_v2.cpp:535-604`, CC 22, NLOC 64, params 3
- `obs_engine::Engine::v2_interaction_mouse_button` — `engine/runtime_interaction_v2.cpp:433-497`, CC 22, NLOC 60, params 3
- `obs_engine::serialize_property` — `engine/properties.cpp:188-295`, CC 21, NLOC 104, params 3
- `obs_engine::Engine::v2_filter_create` — `engine/runtime_filter_v2.cpp:1461-1529`, CC 21, NLOC 64, params 3
- `obs_engine::Engine::v2_source_load_state` — `engine/runtime_source_v2.cpp:1224-1274`, CC 21, NLOC 48, params 3
- `obs_engine::decode_utf8_scalars` — `engine/runtime_interaction_v2.cpp:233-272`, CC 21, NLOC 40, params 3
- `obs_source_media_action_enqueue` — `libobs/obs-source.c:5858-5924`, CC 20, NLOC 64, params 4
- `obs_engine::execute_filter_method` — `engine/protocol_filter_v2.cpp:213-260`, CC 20, NLOC 48, params 5
- `obs_engine::classify_filter_method` — `engine/protocol_filter_v2.cpp:144-185`, CC 20, NLOC 42, params 1
- `obs_engine::Engine::command_item_transform` — `engine/runtime.cpp:502-570`, CC 18, NLOC 65, params 2
- `obs_engine::Engine::v2_properties_invoke_button` — `engine/runtime_properties_v2.cpp:440-513`, CC 18, NLOC 63, params 3
- `obs_engine::Engine::v2_filter_duplicate` — `engine/runtime_filter_v2.cpp:1591-1650`, CC 18, NLOC 60, params 3
- `obs_engine::settle_deferred_filter_update` — `engine/runtime_filter_v2.cpp:883-936`, CC 18, NLOC 50, params 6
- `obs_engine::Engine::handle` — `engine/runtime.cpp:118-166`, CC 18, NLOC 46, params 1
- `test_telemetry_policy` — `engine/events_test.cpp:153-194`, CC 18, NLOC 40, params 0
- `obs_engine::parse_v2_request` — `engine/protocol_v2.cpp:724-762`, CC 18, NLOC 39, params 3
- `obs_engine::Engine::v2_sync_media_observers` — `engine/runtime_media_v2.cpp:846-908`, CC 17, NLOC 59, params 0
- `obs_engine::settle_media_action` — `engine/runtime_media_v2.cpp:530-584`, CC 17, NLOC 53, params 7
- `obs_engine::handle_filter_request` — `engine/protocol_filter_v2.cpp:378-438`, CC 17, NLOC 51, params 5
- `main` — `engine/host.cpp:195-273`, CC 16, NLOC 69, params 2
- `obs_engine::publish_source_events` — `engine/runtime_source_v2.cpp:435-493`, CC 16, NLOC 50, params 3
- `obs_engine::Engine::v2_settle_filter_mutation` — `engine/runtime_filter_v2.cpp:1304-1341`, CC 16, NLOC 36, params 3
- `Run-RaceScenario` — `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1:145-257`, CC 15, NLOC 98, params 2
- `obs_engine::Engine::v2_sync_source_observers` — `engine/runtime_source_v2.cpp:738-799`, CC 15, NLOC 58, params 0
- `obs_engine::parse_args` — `engine/config.cpp:35-85`, CC 15, NLOC 48, params 3
- `obs_source_init` — `libobs/obs-source.c:218-270`, CC 15, NLOC 47, params 1
- `obs_engine::publish_deferred_media_snapshot` — `engine/runtime_media_v2.cpp:375-418`, CC 15, NLOC 42, params 2
- `obs_source_update_internal` — `libobs/obs-source.c:1099-1146`, CC 15, NLOC 42, params 5
- `obs_engine::Engine::start` — `engine/runtime.cpp:46-116`, CC 14, NLOC 63, params 0
- `obs_engine::Engine::v2_source_duplicate` — `engine/runtime_source_v2.cpp:904-967`, CC 14, NLOC 54, params 3
- `obs_engine::Engine::v2_filter_patch_settings` — `engine/runtime_filter_v2.cpp:1673-1716`, CC 14, NLOC 44, params 3
- `obs_engine::take_deferred_media_events` — `engine/runtime_media_v2.cpp:333-373`, CC 14, NLOC 38, params 3
- `obs_engine::property_type_name` — `engine/properties.cpp:17-49`, CC 14, NLOC 33, params 1
- `obs_engine::Engine::v2_media_toggle_pause` — `engine/runtime_media_v2.cpp:1027-1060`, CC 14, NLOC 33, params 3
- `obs_engine::is_event_name` — `engine/events.cpp:14-35`, CC 14, NLOC 20, params 1
- `obs_engine::is_safe_identifier` — `engine/validation.hpp:7-22`, CC 14, NLOC 15, params 2
- `obs_source_destroy_defer` — `libobs/obs-source.c:806-886`, CC 13, NLOC 65, params 1
- `obs_engine::Engine::v2_source_create` — `engine/runtime_v2.cpp:209-263`, CC 13, NLOC 47, params 3
- `obs_engine::Engine::v2_filter_replace_settings` — `engine/runtime_filter_v2.cpp:1718-1759`, CC 13, NLOC 42, params 3
- `obs_engine::take_deferred_filter_events` — `engine/runtime_filter_v2.cpp:381-424`, CC 13, NLOC 41, params 3
- `obs_engine::Engine::v2_filter_register_source_filters` — `engine/runtime_filter_v2.cpp:1036-1078`, CC 13, NLOC 41, params 2
- `obs_engine::Engine::v2_media_set_position` — `engine/runtime_media_v2.cpp:1205-1244`, CC 13, NLOC 39, params 3
- `Read-Event` — `.github/scripts/engine-protocol-v2-task11.ps1:102-135`, CC 13, NLOC 34, params 4
- `Read-SafeSettingsEvent` — `.github/scripts/engine-protocol-v2-task11.ps1:192-223`, CC 13, NLOC 32, params 3
- `test_patterns_and_overlap` — `engine/events_test.cpp:39-70`, CC 13, NLOC 30, params 0
- `obs_engine::is_mutating` — `engine/protocol_filter_v2.cpp:187-206`, CC 13, NLOC 20, params 1
- `obs_module_load` — `plugins/win-capture/plugin-main.c:112-176`, CC 12, NLOC 53, params 0
- `obs_engine::EventDispatcher::publish` — `engine/events.cpp:171-229`, CC 12, NLOC 52, params 4
- `obs_engine::parse_subscription_list` — `engine/protocol_v2.cpp:613-650`, CC 12, NLOC 38, params 3
- `obs_engine::publish_deferred_filter_snapshot` — `engine/runtime_filter_v2.cpp:426-464`, CC 12, NLOC 38, params 2
- `obs_engine::Engine::v2_filter_set_order` — `engine/runtime_filter_v2.cpp:1807-1839`, CC 12, NLOC 33, params 3
- `obs_engine::list_value_is_enabled` — `engine/properties.cpp:316-348`, CC 12, NLOC 32, params 2
- `process_media_actions` — `libobs/obs-source.c:1379-1428`, CC 11, NLOC 45, params 1
- `obs_engine::EventDispatcher::run` — `engine/events.cpp:280-317`, CC 11, NLOC 35, params 0
- `Read-Event` — `.github/scripts/engine-protocol-v2-task10.ps1:87-116`, CC 11, NLOC 30, params 3
- `test_state_prefers_telemetry_eviction` — `engine/events_test.cpp:99-126`, CC 11, NLOC 27, params 0

## Top 25 scoped functions

| Rank | Function | File | Lines | NLOC | CC | Params |
|---:|---|---|---:|---:|---:|---:|
| 1 | `obs_engine::classify_method` | `engine/protocol_v2.cpp` | 158-281 | 124 | 61 | 1 |
| 2 | `main` | `engine/properties_test.cpp` | 48-224 | 156 | 54 | 0 |
| 3 | `obs_engine::execute_runtime_method` | `engine/protocol_v2.cpp` | 390-505 | 116 | 54 | 5 |
| 4 | `obs_engine::method_is_runtime` | `engine/protocol_v2.cpp` | 315-375 | 61 | 54 | 1 |
| 5 | `obs_engine::handle_v2_request` | `engine/protocol_v2.cpp` | 803-956 | 138 | 39 | 4 |
| 6 | `obs_engine::validate_property_item` | `engine/properties.cpp` | 350-443 | 91 | 38 | 3 |
| 7 | `obs_engine::publish_filter_events` | `engine/runtime_filter_v2.cpp` | 466-579 | 106 | 34 | 7 |
| 8 | `obs_engine::publish_media_events` | `engine/runtime_media_v2.cpp` | 420-507 | 82 | 31 | 5 |
| 9 | `obs_engine::Engine::v2_sync_filter_observers` | `engine/runtime_filter_v2.cpp` | 1125-1225 | 97 | 28 | 0 |
| 10 | `obs_engine::Engine::v2_build_property_target` | `engine/runtime_properties_v2.cpp` | 210-299 | 86 | 27 | 6 |
| 11 | `obs_engine::Engine::v2_item_set_transform` | `engine/runtime_v2.cpp` | 529-617 | 79 | 25 | 3 |
| 12 | `obs_engine::collect_media_signal` | `engine/runtime_media_v2.cpp` | 586-649 | 57 | 24 | 3 |
| 13 | `obs_engine::method_is_mutating` | `engine/protocol_v2.cpp` | 283-313 | 31 | 24 | 1 |
| 14 | `obs_engine::collect_source_signal` | `engine/runtime_source_v2.cpp` | 495-572 | 66 | 23 | 2 |
| 15 | `obs_engine::collect_filter_signal` | `engine/runtime_filter_v2.cpp` | 648-736 | 81 | 22 | 3 |
| 16 | `obs_engine::Engine::v2_interaction_key` | `engine/runtime_interaction_v2.cpp` | 535-604 | 64 | 22 | 3 |
| 17 | `obs_engine::Engine::v2_interaction_mouse_button` | `engine/runtime_interaction_v2.cpp` | 433-497 | 60 | 22 | 3 |
| 18 | `obs_engine::serialize_property` | `engine/properties.cpp` | 188-295 | 104 | 21 | 3 |
| 19 | `obs_engine::Engine::v2_filter_create` | `engine/runtime_filter_v2.cpp` | 1461-1529 | 64 | 21 | 3 |
| 20 | `obs_engine::Engine::v2_source_load_state` | `engine/runtime_source_v2.cpp` | 1224-1274 | 48 | 21 | 3 |
| 21 | `obs_engine::decode_utf8_scalars` | `engine/runtime_interaction_v2.cpp` | 233-272 | 40 | 21 | 3 |
| 22 | `obs_source_media_action_enqueue` | `libobs/obs-source.c` | 5858-5924 | 64 | 20 | 4 |
| 23 | `obs_engine::execute_filter_method` | `engine/protocol_filter_v2.cpp` | 213-260 | 48 | 20 | 5 |
| 24 | `obs_engine::classify_filter_method` | `engine/protocol_filter_v2.cpp` | 144-185 | 42 | 20 | 1 |
| 25 | `obs_engine::Engine::command_item_transform` | `engine/runtime.cpp` | 502-570 | 65 | 18 | 2 |

## PowerShell top-level script bodies (reported separately)

| File | NLOC | CC |
|---|---:|---:|
| `.github/scripts/engine-protocol-v2-task10.ps1` | 443 | 63 |
| `.github/scripts/engine-protocol-v2-task11-timeout-race.ps1` | 22 | 3 |
| `.github/scripts/engine-protocol-v2-task11.ps1` | 605 | 80 |
| `.github/scripts/engine-protocol-v2-task8-concurrency.ps1` | 32 | 9 |
| `.github/scripts/engine-protocol-v2-task9.ps1` | 265 | 26 |
