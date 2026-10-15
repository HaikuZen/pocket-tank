# Fifth board: Elecrow CrowPanel Advanced 7" / 10.1" (ESP32-P4, 1024 x 600 MIPI-DSI + GT911) - port notes, 2026-10-15

The first board on a chip other than the ESP32-S3. Elecrow's **CrowPanel
Advanced** 7" and 10.1" (hardware **V1.2**; the unit on the bench is a 10.1",
SKU DHE04310D) share one image: the same 1024 x 600 IPS panel behind an
EK79007 controller on 2-lane MIPI-DSI, a GT911 touch, a PWM backlight and an
ESP32-C6 for Wi-Fi, on the same pins - and, in practice, the same audio
(below).
Sources: Elecrow's repos for both sizes (`Eagle_SCH&PCB/1.2/readme.md`, the
V1.2 schematic, `example/V1.2/idf-code` Lessons 05 / 07 / 11 / 17) and the
factory firmware's boot log. Elecrow's V1.0 / V1.1 / V1.2 code differs only
in the plug-in radio modules' pins (LoRa / nRF24), which the tank does not use.

**Status: built, flashed and running on the first board (2026-10-15).** The
panel comes up, the GT911 answers, the model loads and the advisor decides;
the tank renders at the frame cap (25.6 fps, ~12 ms a frame to draw); colors
and touch are right on the glass, and the sound plays clean. Every selftest
passes in the new world in the simulator. Still open: the rest of the checks
below, Wi-Fi, the advisor's speed, CI and the installer.

## The CrowPanel P4 build (`TANK_WIDE_1024`): a 1024 x 600 tank

The FNK0115's wide tank a size up: `TANK_WIDE` plus `TANK_WIDE_1024`
(common/tank.h), the whole glass a plain rectangle.

- **Film grid**: 32 x 19 cells of 32 px = 608, inside the 644 the save keeps
  (the last row hangs 8 px under the glass).
- **Fronds**: a segment is 5.25 px, so a full frond's tip still stops just
  under the surface (y 22).
- **Bed 1** stands against the right glass (`TANK_FX1 - 128`), its wall frond
  20 px in, as on every other board; at the old 0.84 of the width it stopped
  56 px short of the glass and the wall-frond rule never applied.
- **Pages** sit centred at the 1.8's size (`PAGE_X` 288, `PAGE_Y` 116).

`make WIDE1024=1` builds `sim/fishsim-wide1024`; `make check-all` runs every
selftest in all five worlds. Three selftests had one board's distances and are
now board-neutral: the keeper's feeding spots in the arrival test
(`TANK_FLOOR_X`), the "rest above the canopy" height (the bed's own top) and a
frame of float rounding in the spawning dance's length.

## The hardware, against the FNK0115

| | the FNK0115 | the CrowPanel P4 |
|---|---|---|
| chip | ESP32-S3, 8 MB octal PSRAM | ESP32-P4 rev 1.3 (360 MHz), 32 MB hex PSRAM at 200 MHz, 16 MB flash |
| panel | 800 x 480 RGB parallel, bounce buffers | 1024 x 600 MIPI-DSI (EK79007), 2 lanes x 900 Mbps, 51 MHz DPI (~56 Hz), streamed straight from PSRAM |
| touch | GT911, SDA 19 / SCL 20, INT 18, RST 38 | GT911, SDA 45 / SCL 46, INT 42, RST 40 |
| backlight | GPIO 2, LEDC 20 kHz | GPIO 31 into an MT9201, LEDC 30 kHz 11-bit, Elecrow's floor of 200 / 2047 |
| Wi-Fi | the S3's own | an ESP32-C6 over 4-bit SDIO (esp_hosted 2.12, esp_wifi_remote 1.6.3): CLK 18, CMD 19, D0..D3 17 / 16 / 15 / 14, the C6's enable on GPIO 32 |
| audio | none | plain 16-bit I2S at 16 kHz into the speaker amps, no codec: BCLK 22, LRCK 21, DATA 23, no MCLK, the mono mix in both slots; the amps' enable GPIO 30, **active low** (10k pull-up). Through `audio_port_es8311.c`'s player with the codec steps left out |
| sleep key | BOOT, GPIO 0 | BOOT is **GPIO 35** (GPIO 0 / 1 are the P4's 32 kHz crystal). It is no LP pad, so it cannot wake a deep sleep: **RESET wakes the board** (a deep-sleep wake is a boot anyway) |
| console | UART0 via the USB-UART bridge | UART0 (GPIO 37 / 38) via the board's CH340 - the "UART" USB socket. The P4's own USB socket enumerates as Elecrow's HID demo under the factory firmware and is not used |
| PMIC, IMU, RTC | none | none: the clock from the network, ROTATION turned by hand |

`CONFIG_POCKET_TANK_CROWP4` (sdkconfig.crowp4) selects `display_port_dsi.c`,
the GT911 path in `touch_port_ft3168.c` (board_pins.h `GT_*`) and the UART0
console. Two hidden options now carry what the FNK0115 had alone:
`POCKET_TANK_PANEL_FBS` (the tank renders into the panel driver's own three
frame buffers) and `POCKET_TANK_CONSOLE_UART0`.

### The display port (`firmware/main/display_port_dsi.c`)

The same shape as the FNK0115's: three frame buffers owned by the DPI driver,
rendered into directly; a flush of one is a switch at the end of the frame
being sent; a buffer left by a switch is free after one `on_refresh_done`.
No bounce buffers - the P4's DMA reads PSRAM fast enough. Before the bus the
D-PHY's supply, the P4's internal LDO3 at 2.5 V (and LDO4 at 3.3 V, as every
Elecrow example takes it). The panel's reset (GPIO 41) is left alone, as in
Elecrow's driver.

### Audio: the schematic vs the board

The 10.1" V1.2 schematic draws its NS4168s as NC and an ES8311 (I2C 0x18,
behind the BSS138 level shifter on GPIO 45 / 46) feeding an NS4263B analog
amp. The bench 10.1" has **no ES8311**: the boot's I2C scan finds only 0x5d
(the GT911) and 0x2f (the STC8 battery helper, behind the same level shifter),
with or without a master clock on GPIO 24. Elecrow's factory firmware (its
`esp32_p4_function_ev_board` BSP and a codec-less "my_codec") and its 10.1"
sound lessons send plain I2S on 21 / 22 / 23 with the amp enable low, as on
the 7" - and that is what plays, clean, on the bench (2026-10-15). The STC8
at 0x2f (charging state, the battery's ADC) is not read yet.

### What the P4 needed (2026-10-15)

- **esp_hosted starts in a C constructor**, before esp_timer, when only
  ~130 KB of internal RAM is in the heap. Its buffers and task stacks in
  internal RAM left 3.5 KB and the boot aborted ("Not enough memory to create
  timer task"). sdkconfig.crowp4 puts both in PSRAM
  (`ESP_HOSTED_MEMPOOL_PREFER_SPIRAM`, `ESP_HOSTED_DFLT_TASK_FROM_SPIRAM`).
- **The scene prefetch's async memcpy** is installed on the AXI GDMA: the
  default (AHB) one cannot reach PSRAM on the P4 and refused every copy.
- **PSRAM at 200 MHz** is behind `IDF_EXPERIMENTAL_FEATURES` on IDF 5.4
  (20 MHz otherwise). The factory firmware runs this chip at 200 MHz.
- **esp_hosted's SDIO pins** are set through its `PRIV_..._SLOT_1` symbols;
  its P4 default has the data lines the other way round.
- **Deep sleep**: no ext0 on the P4 and no all-pads deep-sleep hold
  (`gpio_deep_sleep_hold_*`); the pins that matter are held one by one.
- **The model's kernels on the P4's own PIE** (common/llm/q4_model.c, the
  S3's four kernels step for step: `esp.vmulas.s8.xacc` for `ee.vmulas.s8.accx`).
  A PIE instruction names only x8..x15, so the operands are register variables
  pinned to a0-a5 (this gcc has no constraint for the class) and the 4- / 8-token
  kernels store each sum as they go. A decision went from 17.5 s (scalar,
  2.4 tok/s, matmul 15.2 s) to **2.6 s** (16 tok/s, matmul 1.4 s); the boot
  bench's 100k dots from 165 ms to 7.4 ms. The boot self-check holds them to
  plain C.

## Building and flashing

```sh
tools/flash_crowp4.sh --build-only
CROWP4_PORT=/dev/ttyUSB3 tools/flash_crowp4.sh --full   # a blank board: nvs cleared, the model, bootloader + table + app
CROWP4_PORT=/dev/ttyUSB3 tools/flash_crowp4.sh          # later: the app (and otadata)
```

or `idf.py -B <dir> -DIDF_TARGET=esp32p4 -DSDKCONFIG=<dir>/sdkconfig
"-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.crowp4" build`, or the
`crowp4` preset in firmware/CMakePresets.json. IDF 5.4.1, the signing key as
for every board. `--full` replaces Elecrow's factory firmware: the first
board's whole flash was read back first (16 MB at 460800 baud; at 921600 the
CH340 dropped bytes).

## First flash: what to check

1. ~~`esptool.py flash_id`: 16 MB flash, 32 MB PSRAM.~~ Done (2026-10-15).
2. ~~The boot log: `dsi: CrowPanel P4 DSI panel 1024x600 up`, `touch: GT911
   ready`, `PSRAM plan OK`, the console on UART0.~~ Done.
3. ~~**Colors**: the sand brown-green, the water teal.~~ Right (2026-10-15).
4. **A steady picture** while the fish swim and the advisor thinks.
5. ~~**Touch**: a tap lands under the finger, the right way round.~~ Works
   (2026-10-15).
5b. ~~**Sound**: `snd milestone` on the console.~~ Clean (2026-10-15).
6. **The backlight**: settings' brightness steps it, the lowest still lit.
7. **Sleep**: BOOT sleeps it; RESET wakes it.
8. **Wi-Fi**: the C6 answers over SDIO (its firmware version in the log) and
   the clock and an update check come through. Untested: no saved network.

## Still to do

- The STC8 battery helper at I2C 0x2f: the battery page has nothing to show.
- CI, the release workflow, the installer and the OTA manifest do not build
  this board yet (`crowp4`).
