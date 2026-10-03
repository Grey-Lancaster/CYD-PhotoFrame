# CYD PhotoFrame

> **About this branch:** `gh-pages` hosts the [web installer](https://grey-lancaster.github.io/CYD-PhotoFrame/) (`index.html`, the three manifest `.json` files and the factory images they flash). The source code is on the [`main` branch](https://github.com/Grey-Lancaster/CYD-PhotoFrame), and every build is on the [Releases page](https://github.com/Grey-Lancaster/CYD-PhotoFrame/releases). The images here are the **v4.0** release:
>
> | File | Build |
> | --- | --- |
> | `firmware.bin` | `cyd` (ILI9341, normal colours) |
> | `firmware2.bin` | `cyd2usb` (ST7789) |
> | `CYD2bPhotoFrame.bin` | `cyd2b` (ILI9341, inverted colours) |

A photo frame for the ESP32 **"Cheap Yellow Display" (CYD)** — a ~$15 board with a 320×240 touch screen, SD card slot and speaker. Photos live on the SD card, and you manage everything from your phone or PC through a built-in web page.

## Install

**Easiest:** use the browser-based installer — no tools needed:
[**➡ CYD-PhotoFrame Web Installer**](https://grey-lancaster.github.io/CYD-PhotoFrame/)

**From source:** install [PlatformIO](https://platformio.org/) and run one of

```
pio run -e cyd -t upload          # standard CYD (ILI9341)
pio run -e cyd2usb -t upload      # CYD with 2 USB ports (ST7789)
pio run -e cyd2b -t upload        # CYD "2B" variant (inverted colours)
pio run -e cyd -t uploadfs        # splash image + favicon (data/ folder), once
```

## First start

1. Put a FAT32 SD card with a few `.jpg` photos in the slot (or add them later from the web page).
2. Power the frame. It shows the splash screen, then tries your saved WiFi.
3. First time only: connect your phone/PC to the WiFi network **`ESP32_AP`**, open **192.168.4.1** and choose your network.
4. The screen shows the frame's address and a QR code. Open it in a browser (or use **http://photoframe.local**).

The frame keeps working without WiFi (it just shows the slideshow) and reconnects by itself when the network is back.
To pick a different WiFi network later, **hold the BOOT button while powering on**.

## Touch and buttons

| Action | Result |
| --- | --- |
| Touch left third | Previous photo |
| Touch right third (or BOOT button) | Next photo |
| Touch middle | Show IP address + QR code |

If left and right feel reversed on your board, uncomment `TOUCH_FLIP_X` in `1-Slideshow/app.h`.

## Web interface

- **Live preview** of what is on the frame, with prev / next / pause.
- **Upload** photos by drag-and-drop (several at once). Photos are **resized in your browser** to fit the screen before they are sent, so you don't need to prepare them. Crop-to-fill or letterbox, your choice.
- **Delete** photos, with thumbnails.
- **Settings** (saved on the frame): seconds per photo, shuffle, screen brightness, sound volume, and *night mode* (turn the screen off between two hours; needs WiFi once for the clock).
- **Sound**: upload `music.wav` or `music.mp3` and it plays (through the speaker) whenever you upload a photo, or on demand.
- **Firmware update** at `/update` (ElegantOTA). Upload the `firmware.bin` from a release or from a CI build.

There is no password on the web interface — keep the frame on a network you trust.

## Photos

- Any size is fine; photos larger than the screen are scaled down (1/2, 1/4 or 1/8) as they are drawn. Smaller ones are centered.
- **Baseline JPEGs only.** Progressive JPEGs can't be decoded and are skipped. The browser upload always produces baseline JPEGs.
- Only files in the SD card's top-level folder are used (`.jpg` / `.jpeg`, any case).

## Hardware

Designed for the CYD ESP32-2432S028 family: ILI9341 (or ST7789) 320×240 display, XPT2046 touch, SD slot, speaker on GPIO26, backlight on GPIO21.

You can buy the board on [Amazon](https://amzn.to/3UVQwrV) (under $20) — or search for "ESP32-2432S028" / "Cheap Yellow Display" on AliExpress (about $13, 2-week shipping).

## Troubleshooting

- **White screen, or colours look like a negative** — "2 USB" boards come with either an ST7789 or an ILI9341 panel. Open the serial monitor and look for the `Display ID:` line: `ID4=0x009341` means ILI9341 (use `cyd`, or `cyd2b` if colours are inverted); `0x008552` means ST7789 (use `cyd2usb`).
- **"No SD card"** — re-seat the card; the frame retries every few seconds. FAT32 only.
- **"Cannot show photos"** — all photos failed to decode (progressive/corrupt JPEG). Re-upload them through the web page.
- **Build fails with a framework error** — the project is pinned to `espressif32@6.10.0` (Arduino-ESP32 2.x). Newer platform releases use Arduino-ESP32 3.x, which this code doesn't support.

## Development

`platformio.ini` pins the platform and all libraries. The code lives in `1-Slideshow/`:

| File | Purpose |
| --- | --- |
| `1-Slideshow.cpp` | setup / loop, touch, night mode, WiFi |
| `storage.cpp` | SD card, photo list & playlist, uploads, deletes (one mutex guards all SD access) |
| `display.cpp` | backlight, status screens, JPEG decode |
| `audio.cpp` | WAV / MP3 playback |
| `web.cpp`, `web_assets.h` | web server, JSON API, the single-page UI |
| `settings.cpp` | settings saved in flash |

GitHub Actions builds all three boards on every push and keeps the `.bin` files as artifacts.

## Credits

Created by **Grey Lancaster** with help from ChatGPT, Claude and the open-source community. Built on WiFiManager, ESPAsyncWebServer, TFT_eSPI, XPT2046_Bitbang, SdFat, JPEGDEC, QRCode, ESP8266Audio and ElegantOTA — thank you to their authors.

## License

[MIT](LICENSE)
