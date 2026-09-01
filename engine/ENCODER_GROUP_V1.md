# EncoderGroup v1

`encoderGroup.*` exposes the existing libobs `obs_encoder_group_t` object.
The Engine owns one ephemeral decimal handle per group and keeps a parallel
membership registry because libobs does not expose safe public enumeration.
libobs itself holds strong references to every member; the Engine retains its
normal Encoder reference as well.

An Encoder belongs to zero or one group. `encoderGroup.add` rejects duplicate
membership and membership in another group. Add/remove membership is accepted
only while the group and Encoder are inactive and uninitialized; a libobs
rejection is returned as `invalid_state` rather than reported as success.

`encoderGroup.remove` requires an empty, inactive group. Members must first be
detached with `encoderGroup.removeEncoder`; non-empty or active removal
returns `object_in_use`. The protocol handle is invalidated only after the
libobs group has been destroyed.

Successful membership changes emit `encoderGroup.changed` and the affected
Encoder's `encoder.groupChanged` at the same revision. Group reconfiguration
continues to use libobs's synchronized frame-boundary behavior; the Encoder
tracked-update bridge does not treat an individual member's earlier callback
as a group-wide settlement.
