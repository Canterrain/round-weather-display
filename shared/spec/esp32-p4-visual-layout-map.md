# ESP32-P4 Visual Layout Map

Updated on Friday, August 7, 2026.

This note captures the layout comparison that should drive the next ESP32-P4 parity pass.
The key rule is that the Pi `digital`, `forecast`, and `message` compositions live inside a
`760x760` circular face centered on the `800x800` stage, so their face-local origin is:

- face origin on stage: `(20, 20)`
- face center on stage: `(400, 400)`

## Digital Home

### Pi reference layout

Source:

- `targets/pi/public/index.html`
- `targets/pi/public/style.css`

| Element | Pi local face coordinates | Pi stage coordinates (`800x800`) | Notes |
| --- | --- | --- | --- |
| Digital face background | `(0, 0, 760, 760)` | `(20, 20, 760, 760)` | `digital-face` container centered in the stage |
| Message edge indicator | `(0, 0, 760, 760)` | `(20, 20, 760, 760)` | Same circular edge treatment as analog |
| Day | `top=80`, centered at `x=380` | `top=100`, centered at `x=400` | `32px`, uppercase |
| Date | `top=115`, centered at `x=380` | `top=135`, centered at `x=400` | `25px`, uppercase |
| Time block | `(x=70, y=150, w=620)` | `(x=90, y=170, w=620)` | Flex row with centered time + AM/PM |
| AM/PM | inside time block | inside time block | Not separately anchored on the stage |
| Divider top | `(x=130, y=300, w=500)` | `(x=150, y=320, w=500)` | 1px gradient rule |
| Current panel | `(x=80, y=320, w=600, h=150)` | `(x=100, y=340, w=600, h=150)` | Single balanced row |
| Current temp | `left=25`, vertical center of panel | `x=125`, `centerY=415` | Large left temp block |
| Current icon | center of panel | `center=(400, 415)`, about `200x200` | Must visually occupy the center |
| Current summary/high-low block | `left=400`, vertical center of panel, `w=250` | `x=500`, `centerY=415`, `w=250` | Right-aligned copy group |
| Divider bottom | `(x=130, y=486, w=500)` | `(x=150, y=506, w=500)` | Second rule before forecast strip |
| Forecast strip | `(x=141, y=500, w=478)` | `(x=161, y=520, w=478)` | Five equal forecast cards |
| Status stack | centered, `bottom=56`, `w=360` | `x=220`, bottom inset `56` | Only status text when needed |
| Settings affordance | none | none | Pi product UI does not show a persistent home-screen settings button |

### Current ESP32-P4 digital layout before fix

Source:

- `targets/esp32-p4/main/app_ui.c`
- physical photo: `IMG_9563.jpg`

| Element | Current ESP stage coordinates | Visible mismatch |
| --- | --- | --- |
| Day | `top=80`, centered at `x=400` | 20px too high vs Pi; collides with the time block |
| Date | `top=115`, centered at `x=400` | 20px too high vs Pi; partially hidden behind time |
| Time | `(x=90, y=150, w=620)` with stand-alone scaled label | Not grouped with AM/PM; scale/anchor make it visually overpower the header |
| PM | separate label at `x offset=248`, `top=268` | Detached from time instead of living inside the shared time block |
| Divider top | `(x=150, y=300, w=500)` | 20px too high vs Pi, so it slices the screen awkwardly |
| Current panel | `(x=100, y=320, w=600, h=150)` | 20px too high vs Pi |
| Current temp | parent-local `(18, 32)` | Reads as isolated on the far left in the physical photo |
| Current summary/high-low | parent-local `(400, 36)` | Reads as isolated on the far right in the physical photo |
| Center weather art | panel-center icon | The row does not read as a single centered composition on hardware |
| Divider bottom | `(x=150, y=486, w=500)` | 20px too high vs Pi |
| Forecast strip | `(x=161, y=500, w=478)` | 20px too high vs Pi |
| Settings button | shown in bottom status stack when setup is complete | Looks like a development control, not a Pi product affordance |

### Measured diagnosis

- The Pi digital layout numbers in CSS are face-local, not stage-global.
- The current ESP32-P4 layout reused those same numbers on the full `800x800` root.
- That missing `(+20, +20)` face offset explains the photo:
  - header stack too high
  - time crowding day/date
  - dividers too high
  - current conditions row visually unbalanced
  - forecast strip too high
- The Pi groups `time` and `meridiem` into one centered flex row; the ESP current layout still anchors `AM/PM` separately.
- The Pi product UI does not show a persistent ready-state settings button on the home screens.

### Planned correction

- Build a transparent `760x760` digital content container centered on the `800x800` stage.
- Place all digital live objects using Pi face-local coordinates inside that container.
- Group the time and meridiem into a single centered row instead of anchoring `AM/PM` separately.
- Keep status text in the bottom status area, but remove the persistent ready-state settings button from the normal home UI.
- Preserve hidden setup access via a larger invisible hotspot / long-press region instead of a visible product control.

## Forecast Home

### Pi reference

- Tomorrow hero frame local top: `68` -> stage top `88`
- Tomorrow label local top: `146` -> stage top `166`
- Tomorrow icon local top: `180` -> stage top `200`
- Tomorrow temps local top: `464` -> stage top `484`
- Forecast rows are face-local:
  - row 1 center `(205, 600)` -> stage center `(225, 620)`
  - row 2 center `(315, 640)` -> stage center `(335, 660)`
  - row 3 center `(435, 640)` -> stage center `(455, 660)`
  - row 4 center `(545, 600)` -> stage center `(565, 620)`

### Current ESP32-P4 forecast diagnosis

- The current ESP forecast view also uses Pi face-local numbers directly on the full stage.
- That means the hero and the secondary rows are all effectively shifted `20px` up and `20px` left relative to the Pi composition.
- Forecast should be rebuilt around the same centered `760x760` face-local container as digital.

## Message View

### Pi reference

- Message face is a centered `760x760` circular composition.
- Title local top `138` -> stage top `158`
- Card local top `232` -> stage top `252`
- Empty label local top `318` -> stage top `338`

### Current ESP32-P4 diagnosis

- The ESP message view already uses a centered `760x760` `message_face` container.
- Its structure is much closer to the Pi than the digital screen and does not have the same root-origin error.
- Message still needs visual review on the physical panel after the next flash, but structurally it is not the main parity failure.

## Settings / Setup

### Pi product behavior

- The Pi runtime clock surfaces do not show a persistent `Settings` button during normal ready-state use.
- Setup/configuration is not part of the visible digital home composition.

### Current ESP32-P4 diagnosis

- The visible ready-state `Settings` button in the shared bottom status stack is a parity break.
- It should not be part of the normal analog/digital/forecast home presentation.
- Setup access should stay available, but through a hidden interaction that does not alter the product layout.
