# Fourth board: Freenove FNK0115 (800 x 480 IPS, RGB panel + GT911) - port notes, 2026-10-14

The IPS variants of Freenove's ESP32-S3 display kit: **FNK0115L** (4.3") and
**FNK0115Q** (5.0"), both 800 x 480 with a GT911 capacitive touch. The TN
variants (FNK0115B 480 x 272, FNK0115N) read touch from an XPT2046 over SPI
and are **not supported**. Sources: `Agent_FNK0115_new_ESP-IDF.md` (Freenove's
pins and panel timings, a pattern verified on the board under ESP-IDF) and
`Agent_GT911.md` (the GT911 bring-up fix).

**Status: built and in the release workflows; first on the glass 2026-10-14.**
Everything below compiles (IDF 5.4.1, the release toolchain) and the world
passes every selftest in the simulator. On the first board the panel comes up
and the picture is steady (check 4); the rest of "First flash" is still open.

## The FNK0115 build (`TANK_WIDE`): an 800 x 480 tank

The panel is far bigger than the 448 x 368 frame, and an RGB panel has no
controller to scale or turn a picture. So, like the bowl and the watch, the
board gets its own world (common/tank.h), the whole glass:

- **A plain rectangle**, 800 x 480: no corners and no curve. `TANK_FX0` /
  `TANK_FX1` are the walls and `TANK_BOT` is the bottom, as on the 1.8. Floor
  things are placed by `TANK_FLOOR_X` (the 1.8's floor scaled 1.79x), so the
  beds, the decor's first spots and the plant's are spread across 800 px.
- **Taller fronds**: a segment is 4.15 px (`VEG_SEG_PX`; 3.2 on the 1.8), so
  growth 1 still reaches the surface 112 px higher up. What a trim pays stays
  at the 1.8's pitch (`VEG_PAY_PX`).
- **Film grid**: 32 x 20 cells of 25 px = 640, inside the 644 the save keeps
  (the last row hangs 20 px under the glass). The save is the same on every
  board.
- **Pages** sit centred on the glass (`PAGE_X` 176, `PAGE_Y` 56), at the 1.8's
  size, with the live tank around them. Nothing on them is cut, so no page
  needed a layout block of its own.

`make WIDE=1` builds `sim/fishsim-wide`. `make check-all` runs every selftest
in all four worlds, and `tools/board_sheet.py` puts the FNK0115's pictures
beside the other three. Three selftests had used one board's distances and
were made board-neutral: the urchin's walk to its frond, the castle's depth
probe (where the beds stand) and the fish's swim to its naming stage.

## The hardware, against the 1.8

| | the 1.8 | the FNK0115 |
|---|---|---|
| panel | SH8601 / CO5300, QSPI, 368 x 448 portrait (turned in software) | 800 x 480 RGB565 parallel, streamed from PSRAM by the LCD peripheral |
| touch | FT3168 / CST816 | GT911 (I2C 0x5D, SDA 19, SCL 20, INT 18, RST 38) |
| backlight | the panel's DCS 0x51 | GPIO 2, LEDC PWM at 20 kHz |
| PMIC, PWR key | AXP2101 | none: on USB power. BOOT (GPIO 0) is the sleep key, as on any board without a PMIC |
| RTC | PCF85063 | none: the night is a deep sleep and the clock comes from the network, as on the bowl |
| IMU | QMI8658 (auto-flip) | none: settings' ROTATION turns the picture over by hand, UPRIGHT / TURNED, saved (`orient_manual`: any board whose IMU does not answer at boot) |
| audio | ES8311 + amp | none in this build (the 1.8's I2S pins are RGB data lines here): silent |
| console | the S3's USB Serial/JTAG | **UART0** through the board's USB-UART bridge. GPIO 19 / 20, the S3's own USB pins, carry the touch's I2C |

The image tells the board apart at build time, not at boot: `CONFIG_POCKET_TANK_FNK0115`
(sdkconfig.fnk0115) selects `display_port_rgb.c`, the GT911 path in
`touch_port_ft3168.c` and the UART console in `director.c`, and switches off
the SH8601 and ES8311 ports so they cannot be built in. The absent chips are
probed on the touch's bus at boot and step aside, as they do on the other
boards.

### The display port (`firmware/main/display_port_rgb.c`)

The driver owns three frame buffers in PSRAM (3 x 750 KB) that the tank
renders into directly (check 6), and streams them through two bounce
buffers in internal RAM (check 4). A flush copies the
tank's frame into the one **not** on the glass, turned 180 degrees if the
picture is flipped, and hands it to `draw_bitmap`. The stream switches to it at
the end of the frame being sent. Before the next flush writes the other buffer,
two VSYNCs must have passed since the switch, because the DMA prefetches. The
driver's own path for a foreign buffer copies into the buffer being shown,
which would tear. Pixel clock 13 MHz (~32 Hz), porches and the data-line order are Freenove's
verified values. Not lower: at 11 MHz the panel took no picture and cycled
its own test colours (white, blue, black, red), the firmware running fine.

PSRAM: the code runs from it (`SPIRAM_XIP_FROM_PSRAM`, ~1.3 MB of the 8),
then the panel's three buffers (the tank's render frames too) and the scene
cache, ~3 MB at 800 x 480, and the advisor's ~2.5 MB. Measured running
(2026-10-14): ~1.1 MB free.

### The touch (`touch_port_ft3168.c`, `gt911_init`)

Through `esp_lcd_touch_gt911`, with `driver_data` naming the address. Without
it the driver skips the GT911's address latch (INT driven during the reset),
and on the bench the guide comes from that failed every read on every boot.
The init retries 5 times and then boots without touch rather than not at all.
A `gpio: conflict found for GPIO[18]` warning at boot is expected and benign.
The panel's px are the tank's: no turn, no mirror and (yet) no calibration.
The finger-landing bias (`touch bias`, 10 px) applies as on every board.

## Building and flashing

```sh
tools/flash_fnk0115.sh --build-only
FNK_PORT=/dev/ttyUSB0 tools/flash_fnk0115.sh --full   # a blank board: nvs cleared, the model, bootloader + table + app
FNK_PORT=/dev/ttyUSB0 tools/flash_fnk0115.sh          # later: the app (and otadata)
```

or by hand: `idf.py -B <dir> -DSDKCONFIG=<dir>/sdkconfig
"-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.fnk0115" build`. Like
every board, this needs `firmware/keys/ota_signing_key.pem` (tools/ota_key.sh)
and uses IDF 5.4.1. The project does not configure on IDF 6.x yet: the
`json` component moved to the registry there, which affects every board.

## First flash: what to check

1. `esptool.py flash_id`: 16 MB flash and octal PSRAM, which is what
   sdkconfig.defaults assumes. Another chip needs its own flash and PSRAM
   lines in sdkconfig.fnk0115.
2. The boot log on the USB-UART: `rgb: FNK0115 RGB panel 800x480 up`, `touch:
   GT911 ready`, `PSRAM plan OK`, and the console `ready on UART0`.
3. **Colors**: the sand is brown-green and the water teal. Red and blue
   swapped means the `data_gpio_nums` order in display_port_rgb.c is wrong.
4. **A steady picture**: no rolling or shifting while the fish swim or the
   advisor thinks. On the first flash (2026-10-14) the picture drifted
   sideways: the DMA streamed straight from PSRAM. Fixed, and steady on the
   glass, with bounce buffers (`BOUNCE_PX`, now 24 x 800, 2 x 38 KB of internal
   RAM) and, in sdkconfig.fnk0115, `SPIRAM_XIP_FROM_PSRAM` (Freenove's
   FETCH_INSTRUCTIONS + RODATA). Tried and dropped: `LCD_RGB_ISR_IRAM_SAFE`
   (it breaks the scene prefetch's async memcpy) and
   `LCD_RGB_RESTART_IN_VSYNC` (it restarts the stream every frame; with it
   the picture flickered now and then). A flicker that stays is the driver
   restarting after a late refill, both CPUs on PSRAM (Espressif's RGB LCD
   notes). A lower pixel clock is NOT the way: at 11 MHz the panel showed
   only its own test colours (2026-10-14). Measured (the `rgb: refill late
   in N of M frames` line, every 10 s; a VSYNC with no frame completion since
   the last): 0 of 322 while the advisor is idle, 20-56 of ~320 (6-17%)
   while it computes. Its weights stream from the mmap'd flash through the
   same cache and MSPI bus as the PSRAM the refill reads. Tried and dropped
   (2026-10-14): the advisor resting between matmul rows, spinning in ROM.
   100 us in every 400 made it WORSE (13-28% late, 5.2 s a decision); 400 in
   400 only cut it to 3-6% at 7.3 s a decision (4.4 flat out). So the bus is
   not all of it. Not the refill ISR's code either: IDF 5.4.1 keeps the whole
   refill path in IRAM whatever the Kconfig (lcd_rgb_panel_eof_handler,
   lcd_rgb_panel_fill_bounce_buffer, the GDMA ISR). What is left is the
   refill's own copy out of PSRAM, slowed by the advisor's load: larger
   bounce buffers give it more slack. 10 -> 16 lines (0.6 -> 1.0 ms a
   refill): late in 2.5-8% of frames while the advisor computes (from
   6-17%), 0 idle; internal heap ~26 KB free running (from ~44). Then the
   render's dirty mask (48 KB here, 20 on the 1.8) moved to PSRAM on this
   board (main.c; ~2 ms more render) and the bounce buffers grew to 24 lines
   (1.5 ms a refill): late in 0-2.5% of frames while the advisor computes
   (mostly ~1%), 0 idle; internal heap ~48 KB free running. Not checked yet:
   Wi-Fi (the clock, the update check), which the tested board never used.
5. **Touch, at four corners and the centre** (director `touch log on`). A
   mirrored axis is `mirror_x` / `mirror_y` in `gt911_init`. An offset is a
   calibration for `cal_point`, measured the way the other boards' were
   (`ota calib`, tools/touch_calib.py).
6. Frame rate (the display log's fps): 2.3x the 1.8's pixels to render. The
   flush is one 750 KB PSRAM copy (`flush_prof`'s send time). Measured
   2026-10-14 with the advisor running: ~12.5 fps, render ~13 ms, the copy
   ~60 ms (PSRAM is shared with the bounce refill, ~24 MB/s at 32 Hz, and
   the model). The wait before the copy is one `on_frame_buf_complete` (~3
   ms; it was two VSYNCs, ~35 ms). Then the tank rendered straight into the
   panel's buffers (`display_port_direct_fb`), the copy gone: ~15-16 fps,
   1.5 MB more PSRAM free (1882 KB running). The frame now locks to two
   panel frames (~62 ms): render ~13 + the scene prefetch ~21 (after the
   flush, nothing hides it) + the wait for the switch. So the panel got a
   third buffer (NUM_FBS 3, PLAN_FB_COUNT 3 on this board): the tank renders
   one while one is on the glass and one waits, and waits only before it
   writes the next (`display_port_direct_wait`). 25.5 fps (the loop's cap)
   while the advisor is idle, ~17-20 while it thinks (its math and the render
   share the CPUs and PSRAM; render 12 -> ~20 ms). ~1.1 MB PSRAM free.
7. Sleep: BOOT puts it to sleep (the backlight off) and wakes it (the stream
   restarted).

## Open

- In the release since it was added (2026-10-14): `release.yml` and
  `installer.yml` build `build-fnk0115` and ship `latest-fnk0115.json` /
  `manifest-fnk0115.json`, so the first tagged release after this offers an
  image only partly checked on the glass ("First flash"). Flash one by cable first (the checks
  above) or tag a pre-release (`v0.3.4-rc1`) to keep it off "latest".
- Audio: if the kit's speaker is on I2S pins this board leaves free, an
  `audio_port` for it.
- The TN variants (XPT2046 touch, and the 480 x 272 FNK0115B's own world).
