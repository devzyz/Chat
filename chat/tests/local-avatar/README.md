# Local avatar

Business / Unit + Component. These tests exercise local image preparation/storage and the editor.
Production enables upload and publication through AvatarCache; network coverage lives in
`../resource-transfer`. The legacy `UserInfo::_icon` field is unchanged.

PNG and JPEG inputs must be at most 5 MiB and 4096 pixels on either axis.
The editor keeps the oriented source image in memory, starts with a centered
circular frame, and supports dragging, mouse-wheel zoom, +/- buttons and a
100–400% slider. The frame stays covered by the image. Confirmation saves the
frame's complete bounding square as a 256 x 256 PNG, including its four corners.
The original image is never copied to storage. Local avatar display uses a circular
mask without modifying that saved square. Storage uses
`<installation>/data/environments/<SHA256 normalized Gate URL>/users/<uid>/static/head/<uid>/current.png`
for legacy local images; published avatars use resource-ID filenames and an atomic index.
The persistence test also verifies read-only migration from the old AppLocalDataLocation layout,
preservation of the original file, and no overwrite of an existing destination.
`QSaveFile` replaces the image only after a successful write. Selection alone
never persists an image; confirmation saves it. The source file is no longer needed.
Qt Concurrent performs image/file work on one worker; session generations discard
old account callbacks, and preview revisions discard cancelled selections.
A confirmed save may finish for the old account after logout, but cannot update
the next account's UI. Resource upload and friend synchronization are covered by the resource suite.

| Test ID | CTest name | Level | Contract |
| --- | --- | --- | --- |
| Q04-AVATAR-01 | local_avatar.prepare | Unit | PNG/JPEG decoding preserves full image dimensions and content for editing |
| Q04-AVATAR-02 | local_avatar.validation | Unit | Missing, invalid, unsupported, truncated, oversized input; dimension boundary |
| Q04-AVATAR-03 | local_avatar.persistence | Unit | Restore without source; account and environment isolation |
| Q04-AVATAR-04 | local_avatar.save_failure | Unit | Invalid image, unusable directory, failed replacement preserve old data |
| Q04-AVATAR-05 | local_avatar.message_refresh | Unit | Only matching sender changes; notification and message identity preserved |
| Q04-AVATAR-06 | local_avatar.restore | Component | Preview/confirm, duplicate save, failed save/retry, account switch and new controller restore |
| Q04-AVATAR-07 | local_avatar.session_cancel | Component | Cancelled preview, empty selection, reset during save ignore stale results |
| Q04-AVATAR-08 | local_avatar.dialog | Component | Editor slider/drag, cancel without saving, confirmed crop matches selection; only one square PNG persists |
| Q04-AVATAR-09 | local_avatar.crop_geometry | Unit | Centered landscape/portrait crops, zoom/pan limits, opaque square corners and separate circular display |
| Q04-AVATAR-10 | local_avatar.crop_interaction | Component | Mouse drag, wheel zoom, masked frame rendering, new-image reset |

Tests use temporary directories and Qt's minimal platform, without network or
personal account data. Production and tests link the same `chat_local_avatar` library.
Unit tests report to `client_unit.xml`; Component tests report to `client_component.xml`.

Run the owning suite from the repository root:

```powershell
.\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
```

After configuration/build, focus the new tests with the Qt and MinGW `bin`
directories on PATH:

```powershell
ctest --test-dir build/windows-client/Release -R '^local_avatar\.' --output-on-failure
```

The native file picker and full logged-in window appearance still require manual
UI verification; the automated editor test drives the same selection controller.
