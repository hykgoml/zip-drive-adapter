# Ordering the board from PCBWay (v0.6)

These steps follow PCBWay's **PCB Instant Quote → Standard PCB** form as it looked on 2026-11-22. Field names may change a little over time.

## 1. Get the files
- **Gerbers:** `zipbridge_v0.6_gerbers.zip`, from the repo root or the [v0.6 release](../../releases/tag/v0.6). Upload the zip as-is; don't unzip it.
- Before you order: the DB25 is the 0.318" setback type (see [BOM.md](BOM.md)), and the board (v0.4 and later) is sized to fit it.

## 2. Open the quote form
Go to <https://www.pcbway.com/orderonline.aspx> (**PCB Instant Quote**) and select the **Standard PCB** tab. Sign in or create an account; you'll need one to check out.

## 3. Fill in the specification

| Field | Choose | Why |
|---|---|---|
| Board type | **Single pieces** | One design, not a panel |
| Different design in panel | **1** | |
| Size (single) | **66 × 79.8 mm** | Board outline (v0.6) |
| Quantity (single) | **5**, or more | PCBWay's minimum for prototypes |
| Layers | **2 Layers** | |
| Material | **FR-4** | |
| FR4-TG | **TG 150-160** (the default) | Fine for this board |
| Thickness | **1.6 mm** | The DB25 board locks and through-hole parts expect 1.6mm |
| Min track/spacing | **6/6mil** (default) | The board uses 0.25mm (≈10mil) tracks and 0.2mm (≈8mil) spacing |
| Min hole size | **0.3mm** (default) | Smallest drill is the 0.3mm vias |
| Solder mask | **Black** | Standard option |
| Silkscreen | **Yellow** | The logo and QR code are drawn for light ink on a dark board |
| UV printing (multi-color) | **None** | |
| Edge connector | **No** | |
| Surface finish | **HASL lead free** (default) or HASL with lead | The board is all through-hole and hand-soldered; either is fine |
| Via process | **Tenting vias** (default) | |
| Finished copper | **1 oz Cu** (default) | |
| Remove product No. | **Specify a location** (free) or **Yes** (+$1.5) | Otherwise PCBWay may print its order number anywhere, possibly over the logo or the QR code. If you specify a location, write in the notes: *"Place product number on the top side, bottom-left corner, near H3."* |
| SMD-Stencil | **leave unticked** | No SMD parts |

### Colours
Both colours are standard PCBWay options, so no special request is needed:
- **Solder mask: Black**
- **Silkscreen: Yellow**

The bottom-side logo and QR code are drawn for light ink on a dark board. Don't switch to a light mask with dark silkscreen; the artwork would come out inverted. If you want a different look, white silkscreen on black also works with this artwork. PCBWay's form has no green silkscreen.

## 4. Calculate and add to cart
1. Click **Calculate** to see the price and build time.
2. Pick a build time and shipping method, then **Add to cart** (or **Save to cart**).

## 5. Upload the Gerbers
1. In the cart, upload `zipbridge_v0.6_gerbers.zip` for this item.
2. Check PCBWay's Gerber preview: 2 copper layers, a 66 × 79.8mm outline, the Ick logo and QR code on the **bottom** silkscreen, and the LED labels on the top.
3. Submit the order for review.

## 6. Engineering review, then pay
PCBWay's engineers review the files before you pay. They usually reply in the order page or by email. They may ask about:
- **Silkscreen clipped where it overlaps pads.** That's normal and fine.
- **The single combined drill file**, which holds both plated holes and the non-plated mounting holes. Confirm H1–H3 (3.2mm) are mounting holes.

Once the order is approved, pay and the board goes to fabrication. PCBWay then shows progress through Fabrication, Shipment, and Confirm and Review.

## After the boards arrive
Assemble from [BOM.md](BOM.md). Fit the DIP sockets and the LVC245s, and **check the Zip is the parallel model, not SCSI**, before you connect anything. The README has the warning section.
