# Spinning Donut on M5StickC Plus2

A spinning 3D donut rendered on the M5StickC Plus2's TFT display using ASCII-style characters, written in C for ESP-IDF.

---
![am53h9](https://github.com/user-attachments/assets/b8db0f11-b065-4cb9-b6a5-72b69b6fbcea)

## What it does

- Renders a rotating 3D donut (torus) on the 240x135 ST7789 display
- Uses a tiny 3x5 pixel font to draw luminance characters that fake shading
- Press the main button (Button A) to change the donut's color to a random bright color
- Runs at a smooth rate using double buffering so there's no flicker

---

## How it works

The donut math is based on the classic ASCII donut algorithm by Andy Sloane. Here's the short version:

1. Two nested loops sweep angles around the torus shape
2. For each point, 3D coordinates are calculated and projected onto a 2D plane using a depth value (`D`)
3. A z-buffer (`z[]`) makes sure closer points overwrite farther ones
4. A luminance value (`N`) is computed based on the surface angle relative to a light source
5. Each luminance level (0–12) maps to a character from a custom tiny font
6. Those characters are drawn as 3x5 pixel glyphs into a framebuffer
7. The framebuffer is blasted to the display via SPI DMA

The two angles `A` and `B` increment every frame, which is what makes it spin.

---

## Hardware

| Part | Details |
|---|---|
| Board | M5StickC Plus2 |
| Display | ST7789 TFT, 240x135, SPI |
| Button | GPIO 37 (Button A) |

Pin definitions come from `pins.h` — make sure that file matches your board's actual wiring.

---

## Project Structure

```
project/
├── main/
│   ├── main.c        ← All the code (this file)
│   └── pins.h        ← GPIO pin definitions for your board
├── CMakeLists.txt
└── sdkconfig
```

---

## Terminal / Render Grid

The donut is calculated on a virtual 60x22 character grid, then each character is drawn as a 3x5 pixel glyph. With 4px horizontal and 6px vertical spacing, the final render fits inside the 240x135 screen with centering offsets applied.

---

## Luminance Characters

There are 13 levels (0 = background/dark, 12 = brightest). Each maps to a pixel pattern:

| Level | Character |
|---|---|
| 0 | ` ` (blank) |
| 1 | `.` |
| 2 | `,` |
| 3 | `-` |
| 4 | `~` |
| 5 | `:` |
| 6 | `;` |
| 7 | `=` |
| 8 | `!` |
| 9 | `*` |
| 10 | `#` |
| 11 | `$` |
| 12 | `@` |

---

## Double Buffering

Two framebuffers are allocated in DMA-capable memory. While one buffer is being sent to the display, the next frame is being drawn into the other. They swap each frame. This prevents screen tearing.

```
frame_buffer[0]  ←→  frame_buffer[1]
   (drawing)            (sending)
```

---

## Color Changing

Pressing Button A picks a new random color. The color generation:
- Picks random R, G, B values
- Forces the highest channel to 255 (max brightness)
- Forces the lowest channel to 0 (max saturation)
- Converts to RGB565 format with byte-swap for the SPI display

---

## Build & Flash

Make sure you have ESP-IDF v5.x set up.

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with your actual serial port.

---

## Task Layout

| Task | Core | Stack | Priority |
|---|---|---|---|
| `display_task` | Core 1 | 16384 bytes | 5 |
| `app_main` loop | Core 0 | default | — |

The display task handles everything: SPI init, display init, button polling, math, rendering, and DMA transfers.

`app_main` only holds power on and stays alive in a loop.

---

## Power Hold

The M5StickC Plus2 needs GPIO `M5_POWER_HOLD_PIN` set HIGH on startup, otherwise the board shuts itself off. This is the first thing `app_main` does.

---

## Dependencies

- ESP-IDF (v5.x recommended)
- `esp_lcd` component (included in ESP-IDF)
- `esp_random` for color generation
- Standard C `math.h` for `sin` / `cos`
