# Engine Protocol v2 — Preview v1

Preview is a logical scene selection independent from Program. It does not
change Main Canvas channel `0`, does not expose raw frame bytes, and does not
advertise shared GPU handles. The later `PreviewOutput` namespace owns render
resources and synchronization.

## Methods

```text
preview.getScene
preview.setScene
preview.getInfo
```

`preview.setScene` requires `params.scene` as a canonical positive decimal
scene handle string or JSON null. Null clears Preview. A missing or malformed
handle returns `bad_request`; a valid-looking handle that is not live returns
`not_found`.

The state document is:

```json
{
  "scene":"2",
  "canvas":"1",
  "hasScene":true,
  "renderWidth":1920,
  "renderHeight":1080
}
```

When Preview is clear, `scene` and `canvas` are null, `hasScene` is false, and
the render dimensions describe the Main Canvas video output when available.
Dimensions for a selected Scene come from its owning Canvas output video mix.

## Events and revisions

Each real selection change consumes one global revision and emits one
`preview.sceneChanged` event after its response. The event carries `scene` as a
handle or null and `previousScene` when known. Equal selections are successful
no-ops and consume no revision. Removing the selected Scene clears Preview and
emits that event before the Scene handle is invalidated.

Preview has no `preview.invalidated` event in this logical-only version;
resource invalidation belongs to the later PreviewOutput implementation.
