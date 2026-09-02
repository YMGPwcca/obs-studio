# Streaming namespace v1

`streaming.*` is a single convenience role over an explicitly created,
service-backed encoded `output.*` object. It does not duplicate the Output or
Service and does not emit `streaming.started`, `streaming.stopped`,
`streaming.reconnecting`, or `streaming.reconnected`; those lifecycle events
remain canonical `output.*` events.

## Methods

* `streaming.getConfig` returns `{configured, output}`.
* `streaming.configure` assigns an existing service-required encoded Output.
* `streaming.unconfigure` clears the role while the Output is inactive.
* `streaming.start`, `streaming.stop`, and `streaming.forceStop` delegate to
  the Output state machine.
* `streaming.getState` aggregates Output state; `streaming.getStats` delegates
  the read-only Output stats snapshot.
* `streaming.getService` and `streaming.setService` expose the canonical
  Output-Service binding and enforce Output protocol compatibility.
* `streaming.getReconnectState` and `streaming.getLastError` delegate to the
  Output policy/error bridge.

Before start, the Service must be initialized, bound to the Output, protocol
compatible, and contain at least one supported stream credential (stream key,
password, or bearer token). Credential values never appear in responses,
events, errors, or logs.

The deterministic CI lane uses the Task 23/25/26 fixtures for exact role and
callback coverage. Physical acceptance is separate and requires a real
packaged network Output transmitting to a local loopback receiver.
