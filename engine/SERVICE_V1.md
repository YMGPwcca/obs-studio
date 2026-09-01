# Service v1

The Service namespace exposes Engine-owned `obs_service_t` contexts with
ephemeral decimal handles. Service settings may contain provider credentials,
but the protocol never returns accepted secret values.

`service.getSettings`, `service.get`, `service.getProperties`, settings events,
and kind defaults return sanitized settings plus a `secrets` object containing
only `{set:true|false}` metadata. The redactor combines `OBS_TEXT_PASSWORD`
property metadata with libobs's typed connect-info accessors for stream key,
username, password, encryption passphrase, and bearer token. Common provider
field names are removed as an additional defense; field-name heuristics are
not the primary classification mechanism.

Secret input is accepted in `create`, `patchSettings`, and `replaceSettings`.
Patch preserves omitted values. `clearSecrets` is an array of objects such as
`[{"name":"key"}]` and explicitly removes those fields. Replace uses exact
whole-settings semantics, so omitted secrets are cleared. No secret is echoed
in responses, events, errors, logs, or snapshots.

`service.getEncoderRecommendations` clones candidate video/audio settings,
lets the Service adjust those clones, and returns the recommendations. It is
read-only with respect to live Encoder objects.

Service rename uses the libobs context name setter. Removal rejects a bound or
active Service with `object_in_use`; Output binding and active-state revisions
are completed by the Output namespace.
