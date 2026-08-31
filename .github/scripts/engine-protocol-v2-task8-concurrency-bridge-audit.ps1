$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Task8BridgeMarkers([string] $Source, [string] $Protocol) {
  foreach ($Required in @(
      'SourceEventCaptureRoute::Capture'
      'SourceEventCaptureRoute::Defer'
      'SourceCallbackScope'
      'direct_callbacks_inflight'
      'v2_drain_deferred_source_events'
      'v2_flush_deferred_source_events'
      'result_has_source_event(*state.capture, "source.removed", batch.handle)'
  )) {
    if (-not $Source.Contains($Required)) {
      throw "Production source bridge is missing required concurrency guard: $Required"
    }
  }
  if ($Source.Contains('result_has_event(*state.capture') -or
      $Source.Contains('result_has_event(*state->capture')) {
    throw 'Global event-name-only source capture deduplication reappeared.'
  }
}

function Assert-Task8BridgePreRequestOrdering([string] $Protocol) {
  $CaptureIndex = $Protocol.IndexOf('capture.emplace(engine, runtime_result);')
  $GuardIndex = $Protocol.IndexOf('mutation_guard.emplace(revisions.lock_mutation());')
  $DrainIndex = $Protocol.IndexOf('engine.v2_drain_deferred_source_events(*mutation_guard);')
  $ValidateIndex = $Protocol.IndexOf('if (!validate_revision_guard(request, method, guarded_revision))')
  if ($CaptureIndex -lt 0 -or $GuardIndex -lt 0 -or $CaptureIndex -gt $GuardIndex) {
    throw 'Runtime source capture must be established before the revision mutation guard.'
  }
  if ($DrainIndex -lt 0 -or $ValidateIndex -lt 0 -or $DrainIndex -lt $GuardIndex -or $DrainIndex -gt $ValidateIndex) {
    throw 'Pre-request deferred source events must drain after guard acquisition and before ifRevision validation.'
  }
}

function Assert-Task8BridgePostRequestOrdering([string] $Protocol) {
  $SyncIndex = $Protocol.LastIndexOf('engine.v2_sync_source_observers();')
  $ResponseIndex = $Protocol.LastIndexOf('send_v2_ok(request.id, result.data.get(), revision);')
  $PublishIndex = $Protocol.LastIndexOf('publish_runtime_events(events, revision, result);')
  $FlushIndex = $Protocol.LastIndexOf('capture->flush(*mutation_guard);')
  if ($SyncIndex -lt 0 -or $ResponseIndex -lt 0 -or $PublishIndex -lt 0 -or $FlushIndex -lt 0) {
    throw 'Runtime response/event/deferred-flush ordering markers were not found.'
  }
  if ($SyncIndex -gt $ResponseIndex -or $ResponseIndex -gt $PublishIndex -or $PublishIndex -gt $FlushIndex) {
    throw 'Required ordering is observer sync -> response -> request events -> deferred async events.'
  }
}

function Invoke-Task8BridgeAudit {
  $Source = Get-Content 'engine/runtime_source_v2.cpp' -Raw
  $Protocol = Get-Content 'engine/protocol_v2.cpp' -Raw
  Assert-Task8BridgeMarkers $Source $Protocol
  Assert-Task8BridgePreRequestOrdering $Protocol
  Assert-Task8BridgePostRequestOrdering $Protocol
}

Invoke-Task8BridgeAudit
