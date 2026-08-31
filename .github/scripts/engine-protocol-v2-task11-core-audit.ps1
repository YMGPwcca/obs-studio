$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Task11CoreRouting {
  $CMake = Get-Content -Raw 'engine/CMakeLists.txt'
  if (-not $CMake.Contains('handle_v2_request=handle_v2_request_core')) {
    throw 'Task 11 router does not isolate the accepted Task 10 protocol core.'
  }
  $Core = Get-Content -Raw 'engine/protocol_v2.cpp'
  if (-not $Core.Contains('bool handle_v2_request(Engine &engine')) {
    throw 'The isolated Task 10 protocol core entry point is missing.'
  }
}

function Assert-Task11Ordering {
  $Router = Get-Content -Raw 'engine/protocol_filter_v2.cpp'
  $Begin = $Router.IndexOf('engine_.v2_begin_event_capture(result);')
  $Wait = $Router.IndexOf('engine_.v2_wait_for_event_capture_callbacks();')
  $Capture = $Router.IndexOf('capture.emplace(engine, result);')
  $Lock = $Router.IndexOf('guard.emplace(revisions.lock_mutation());')
  $Drain = $Router.IndexOf('engine.v2_drain_deferred_source_events(*guard);')
  if ($Begin -lt 0 -or $Wait -lt 0 -or $Capture -lt 0 -or $Lock -lt 0 -or $Drain -lt 0) {
    throw 'Task 11 capture/wait/lock/deferred-drain markers are incomplete.'
  }
  if ($Begin -gt $Wait -or $Capture -gt $Lock -or $Lock -gt $Drain) {
    throw 'Task 11 callback retirement and mutation ordering regressed.'
  }
}

function Assert-Task11FilterObserver {
  $Filter = Get-Content -Raw 'engine/runtime_filter_v2.cpp'
  foreach ($Forbidden in @('FilterUpdateWaiter', 'filter_update_settle_cb')) {
    if ($Filter.Contains($Forbidden)) {
      throw "Per-request filter signal waiter leaked into Task 11: $Forbidden"
    }
  }
  $PermanentConnect = ([regex]::Matches($Filter, 'signal_handler_connect\(handler, "update", filter_update_cb, &observer\)')).Count
  if ($PermanentConnect -ne 1) {
    throw "Expected exactly one permanent filter update observer connection in source; found $PermanentConnect."
  }
  foreach ($Forbidden in @('obs_filter_get_parent', 'obs_filter_get_target')) {
    if ($Filter.Contains($Forbidden)) {
      throw "Task 11 production code used a forbidden filter lifetime/waiter API: $Forbidden"
    }
  }
}

function Assert-Task11PublicApi {
  $PublicObs = Get-Content -Raw 'libobs/obs.h'
  foreach ($PrivateApi in @('obs_source_update_tracked', 'obs_source_reset_settings_tracked')) {
    if ($PublicObs.Contains($PrivateApi)) {
      throw "Task 11 private update API leaked into the public obs.h header: $PrivateApi"
    }
  }
}

function Invoke-Task11CoreAudit {
  Assert-Task11CoreRouting
  Assert-Task11Ordering
  Assert-Task11FilterObserver
  Assert-Task11PublicApi
}

Invoke-Task11CoreAudit
