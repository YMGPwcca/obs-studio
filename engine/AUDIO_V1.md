# Engine Protocol v2 — `audio.v1`

Task 21 exposes live source-audio controls and bounded meter telemetry. Audio
identity is the existing canonical Source handle; the engine does not create a
second AudioSource object.

## Capabilities and target validation

The engine advertises `audio.v1` and the method capabilities for the complete
Task-21 surface. Source methods require `params.source` to be a canonical
positive decimal string and require live `OBS_SOURCE_AUDIO` output flags.
Malformed handles return `bad_request`, unknown handles return `not_found`, and
non-audio Sources return `unsupported_capability`.

## Source state

`audio.get` returns:

```json
{
  "source":"1",
  "muted":false,
  "volumeMul":1.0,
  "volumeDb":0.0,
  "volumeDbFloored":false,
  "balance":0.5,
  "syncOffsetNs":0,
  "monitoringEnabled":false,
  "tracks":[{"track":1},{"track":2}],
  "pushToTalk":{"enabled":false,"delayMs":0},
  "pushToMute":{"enabled":false,"delayMs":0},
  "speakerLayout":"stereo",
  "selfMonitoringAllowed":true
}
```

The `tracks` array contains logical one-based track objects because the current
libobs `obs_data_array_t` wire container stores object entries; `track` values
are the semantic values `1..MAX_AUDIO_MIXES` and are translated to the internal
mixer bitmask. Results are sorted and unique.

`muted` is the user mute state returned by `obs_source_muted`. The effective
audio mix may also be silent because the Source is disabled, volume is zero, or
PTT/PTM is currently gating it.

## Mutations

The methods are:

```text
audio.setMute              {source, muted}
audio.toggleMute           {source}
audio.getVolume            {source}
audio.setVolume            {source, volumeMul}
audio.setVolumeDb          {source, volumeDb}
audio.setBalance           {source, balance}
audio.setSyncOffset        {source, syncOffsetNs}
audio.getMonitoringEnabled {source}
audio.setMonitoringEnabled {source, monitoringEnabled}
audio.getTracks            {source}
audio.setTracks             {source, tracks:[{track:1}, ...]}
audio.getPushToTalk        {source}
audio.setPushToTalk         {source, enabled, delayMs}
audio.getPushToMute         {source}
audio.setPushToMute          {source, enabled, delayMs}
```

Volume multipliers are finite and bounded to `0..64`. dB values are finite and
bounded to `-192..36`. A zero multiplier is represented by the finite floor
`volumeDb:-192` and `volumeDbFloored:true`; JSON never contains NaN or infinity.
The engine uses libobs's current `obs_mul_to_db`/`obs_db_to_mul` conversion and
reads the actual multiplier back after setting it. One actual change emits one
`audio.volumeChanged`, regardless of whether the request used multiplier or dB
units.

Balance is normalized to `[0,1]` and is supported only for a stereo Source;
other layouts return `unsupported_capability`. `syncOffsetNs` is signed and
bounded to `+/-604800000000000` nanoseconds (seven days); overflow is rejected.
Monitoring uses the current boolean API. Enabling a Source with
`OBS_SOURCE_DO_NOT_SELF_MONITOR` returns `unsupported_capability`.

PTT/PTM updates require both `enabled` and `delayMs` (`0..3600000`) and apply
the pair as one protocol mutation. The event is consolidated as
`audio.gatingChanged` with both canonical PTT and PTM objects. No deprecated
monitoring-type enum is exposed.

Successful canonical mutations consume one revision and command-owned events
use that revision after the response. Equal values are successful no-ops with
no revision/event. Source audio callbacks outside a request receive their own
revision. Audio callbacks never write protocol output directly.

## Monitoring devices

Methods:

```text
audio.listMonitoringDevices
audio.getMonitoringDevice
audio.setMonitoringDevice {deviceId}
```

The result entries contain `deviceId`, display `name`, and `isDefault`. Device
IDs are stable runtime identities supplied by the platform; display names are
not identities. `default` is the explicit default ID. Enumeration and reads are
read-only. Setting an actual device emits
`audio.monitoringDeviceChanged`; unavailable monitoring returns
`unsupported_capability`.

## Meter subscriptions

`audio.subscribeMeters` is a non-revisioned telemetry-control operation. The
current JSON container form is:

```json
{
  "sources":[{"source":"1"},{"source":"2"}],
  "maxHz":20,
  "peakMode":"sample"
}
```

`sources` is bounded to 64 unique audio Source handles, `maxHz` is `1..240`,
and `peakMode` is `sample` or `truePeak`. The response returns an ephemeral
`meterSubscription` token. `audio.unsubscribeMeters` takes that token and is
idempotent only for a live token; an unknown token is `not_found`.

Each meter sample is delivered as opt-in telemetry event:

```json
{
  "meterSubscription":"1",
  "source":"1",
  "channelCount":2,
  "magnitudeDb":[{"value":-9.2},{"value":-10.1}],
  "peakDb":[{"value":-3.0},{"value":-3.1}],
  "inputPeakDb":[{"value":-3.0},{"value":-3.1}]
}
```

The values are finite dB snapshots. Negative infinity is clamped to the
documented finite floor `-192`. The audio callback only performs bounded atomic
stores. A video-tick handoff applies the requested maximum rate and the common
dispatcher may coalesce/drop telemetry under pressure. Meter callbacks are
detached before Source removal, unsubscribe, or shutdown; Source removal never
leaves an event containing an invalid live Source handle.

## Events

Canonical events:

```text
audio.volumeChanged
audio.muteChanged
audio.balanceChanged
audio.syncOffsetChanged
audio.monitoringChanged
audio.tracksChanged
audio.gatingChanged
audio.monitoringDeviceChanged
```

Telemetry:

```text
audio.meter
```

Audio state events are normalized from libobs source signal calldata. PTT/PTM
callbacks are emitted while libobs holds the Source audio mutex, so the bridge
uses the signal payload and does not call a getter from that callback.

## Lifetime and errors

Source handles remain owned by the existing source registry. Meter tokens are
session-local and ephemeral. The stable errors are `bad_request`, `not_found`,
`unsupported_capability`, `revision_conflict`, `obs_error`, and
`internal_error`. No raw audio samples, pointers, platform handles, or
unbounded callback data cross NDJSON.
