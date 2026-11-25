# Bill of Materials: board v0.6

The references, values and footprints in the first table come from `zipbridge_v0.6.kicad_pcb` (same parts as v0.5). Every part is through-hole. A spreadsheet version is in [`BOM.csv`](BOM.csv).

## Parts on the board

| Qty | Refs | Part | Value / spec | Footprint | Notes |
|---:|---|---|---|---|---|
| 1 | U4 | Raspberry Pi Pico 2 (RP2350) | — | 2×20, 2.54mm pitch, rows 17.78mm apart | Mounted component side up, with the USB end at the left board edge |
| 3 | U1, U2, U3 | SN74LVC245AN | Octal bus transceiver, DIP-20 | DIP-20, 7.62mm (socketed) | U1 data, U2 control out, U3 status in. Runs on 3.3V; inputs are 5V-tolerant. Must be **LVC**: LS/HC/HCT parts are unsafe here |
| 1 | J1 | DB25 female, right angle, PCB mount | 0.318" (8.08mm) setback, with board locks | DSUB-25 socket, horizontal, 7.70mm edge offset | **Bought:** [Connectors Pro 10-pack, Amazon B00C58IPSO](https://www.amazon.com/dp/B00C58IPSO). Alternative: [McMaster-Carr 5063N19](https://www.mcmaster.com/5063N19) |
| 8 | R1–R8 | Resistor | 100Ω, ¼W | Axial DIN0207, 7.62mm, horizontal | Series resistors on the data lines (cable side) |
| 5 | R9–R13 | Resistor | 10kΩ, ¼W | Axial DIN0207, 7.62mm, horizontal | Pull-ups to 3.3V on the status inputs |
| 2 | R14, R15 | Resistor | 10kΩ, ¼W | Axial DIN0207, 7.62mm, horizontal | OE# pull-ups (buffers off at boot) |
| 4 | R16–R19 | Resistor | 470Ω, ¼W | Axial DIN0207, **2.54mm, standing (vertical)** | LED series resistors, about 2–3mA each |
| 3 | C1, C2, C3 | Ceramic capacitor | 100nF, 50V | Disc, 2.50mm lead spacing | Decoupling, one per LVC245 |
| 1 | D1 | LED, 3mm | Green | LED 3mm THT | **PWR**: on when 3.3V is up |
| 1 | D2 | LED, 3mm | Yellow | LED 3mm THT | **READY**: GP22, firmware |
| 1 | D3 | LED, 3mm | Green | LED 3mm THT | **READ** (Zip→PC): GP20, firmware |
| 1 | D4 | LED, 3mm | Red | LED 3mm THT | **WRITE** (PC→Zip): GP21, firmware |

For the LEDs, use red, green or yellow (forward voltage about 2.2V or less). Blue and white LEDs are too dim from 3.3V through 470Ω. On the board, the square pad is the cathode (K).

## Hardware and accessories (not on the board file)

| Qty | Part | Notes |
|---:|---|---|
| 3 | DIP-20 IC socket, 7.62mm (0.3") | For U1–U3, so the buffers can be swapped |
| 2 | 1×20 female header, 2.54mm | Optional: makes the Pico removable. Otherwise solder the Pico directly with its male headers |
| 2 | 1×20 male header, 2.54mm | For the Pico, if it didn't come with headers fitted |
| 3 | M3 standoff + screw | Optional: for mounting holes H1–H3 (3.2mm) |
| 1 | DB25 male–male cable | The Zip drive's original parallel cable plugs into J1 |
| 1 | Micro-USB cable | The Pico 2 has a micro-USB port; it powers the board and carries data |

The parallel Zip drive needs its own power supply; this board does not power it.

## Where to buy

Amazon picks found on 2026-11-22 (prices change). Most are kits, so one order covers several boards with spares.

| For | Buy | Price then | Notes |
|---|---|---|---|
| J1 | [Connectors Pro DB25 female right-angle, 10-pack](https://www.amazon.com/dp/B00C58IPSO) | — | Bought |
| D1–D4 | [DiCUNO 3mm LED kit, 200pcs, 10 colours](https://www.amazon.com/dp/B07CB44M2Z) | $7.99 | Has red, green and yellow. Alt: [C.J. SHOP 3mm diffused, 250pcs](https://www.amazon.com/dp/B00XT0P1VQ) ($5.79) |
| R1–R19 | [BOJACK ¼W 1% metal film kit, 50 values, 1350pcs](https://www.amazon.com/dp/B07P3MFG5D) | $12.99 | Includes 100Ω, 470Ω and 10kΩ |
| C1–C3 | [100nF (104) ceramic capacitors, 100pcs](https://www.amazon.com/dp/B0DFQ93CPT) | $6.99 | Holes are 2.5mm apart; bend wider leads to fit |
| U1–U3 sockets | [uxcell DIP-20 IC sockets, 30pcs](https://www.amazon.com/dp/B07H3RPFH8) | $7.29 | Must be the narrow 0.3" (7.62mm) type |
| U4 | [Raspberry Pi Pico 2, headers fitted](https://www.amazon.com/dp/B0DG3QPQCT) | $13.44 | Make sure it's a Pico 2 (RP2350) |
| U4 (optional) | [1×20 female headers for Pico, 20pcs](https://www.amazon.com/dp/B0CFDV41T9) | $8.99 | Two per board |
| H1–H3 (optional) | [M3 nylon standoff kit, 580pcs](https://www.amazon.com/dp/B0F297R23T) | $8.99 | |
| U1–U3 | **SN74LVC245AN from [DigiKey](https://www.digikey.com/en/products/result?keywords=SN74LVC245AN) or [Mouser](https://www.mouser.com/c/?q=SN74LVC245AN)** | about $1 each | Not Amazon: listings there are overpriced or unreviewed, and fakes are common. A relabelled LS/HC part would damage the Pico |

Total from Amazon is about $58 with the optional parts, or about $40 without, plus the LVC245s.

## PCB

The fab files are `zipbridge_v0.6_gerbers.zip` (also on the [v0.6 release](../../releases/tag/v0.6)). Order options: 2 layers, 66 × 79.8mm, 1.6mm thick, HASL finish, **black solder mask, yellow silkscreen** (both standard). Full steps: [ORDERING.md](ORDERING.md).
