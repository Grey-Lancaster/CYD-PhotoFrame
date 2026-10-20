# CLAUDE.md

Firmware for a Wi-Fi photo frame on the ESP32 "Cheap Yellow Display" (CYD) boards, plus the Freenove FNK0104S 4.0" ESP32-S3. Photos come from an SD card; a built-in web page manages photos and settings (English/Spanish). Public project. `README.md` is the user guide (installer table, web interface, troubleshooting) and is kept current.

## Layout
- `platformio.ini` - one env per board: `cyd` (ILI9341), `cyd2usb` (ST7789), `cyd2b` (ILI9341 inverted), `cyd35` (3.5" ST7796 480x320), `freenove4` (FNK0104S). Pins and panel settings are build flags per env. Default env is `cyd`.
- `1-Slideshow/` - code for all CYD envs (`1-Slideshow.cpp` setup/loop/touch, `wifi_setup.cpp`, `storage.cpp`, `display.cpp`, `audio.cpp`, `web.cpp` + `web_assets.h`, `settings.cpp`, `motion.cpp`).
- `2-Freenove4/` - separate code for the Freenove (SDMMC SD card, capacitive touch, ES8311 audio codec, PSRAM photo scaling, `partitions.csv`). Its web server, web page and settings files are copies of the CYD ones, so a web/UI change usually has to be made in both folders. `build_src_filter` in each env picks the folder.
- `data/` - splash image and favicon, uploaded with `pio run -e cyd -t uploadfs`.
- `lib/FT6336U_CTP_Controller/` - local copy of the Freenove touch driver.
- `.github/workflows/build.yml` - CI. `google*.html` is a Search Console verification file; leave it.
- `parts/` and `dist/` are created by CI only (not in the repo).

## Build
- `pio run -e <env>` builds; `pio run -e <env> -t upload` flashes. Need PlatformIO.
- The platform is pinned to `espressif32@6.10.0` (Arduino-ESP32 2.x). Newer 3.x platforms break the internal-DAC audio and legacy APIs; do not bump it.
- All libraries are pinned in `platformio.ini`. TouchWifiProvisioner (separate repo, Grey-Lancaster/TouchWifiProvisioner) is pinned by commit hash; update the hash in both `lib_deps` blocks (CYD `[env]` and `freenove4`) together.
- I found no tests in the repo. Verification is building and flashing a board; do not claim hardware behaviour is verified unless it was seen on a board.

## Gotchas
- Wrong colours is usually the wrong env; the web page also has **Swap red and blue** and **Invert colours** toggles.
- The 3.5" CYD touch shares the display SPI pins and must go through TFT_eSPI, not a bit-banged driver (that freezes the screen).
- WiFi setup on every board is the on-screen TouchWifiProvisioner picker (LVGL 8.4); there is no `ESP32_AP` hotspot any more.
- Freenove flash mode must be DIO. QIO makes the ROM hang and the board reboot every few seconds. Merged images for it are built with `--flash_mode dio`.
- `freenove4` sets its own `lib_deps` and `build_flags` and does not inherit them from `[env]`.
- The web interface has no password by design (see README).

## Release / deploy
- CI builds every env on every push. Pushing a tag `v*` makes the `release` job publish a GitHub Release titled "CYD PhotoFrame <tag>" with `*-factory.bin` (full image, wipes WiFi) and `*-update.bin` (for the `/update` page, keeps WiFi) for each env. A tag containing `-` is marked pre-release. The release notes are the `body:` block in `build.yml`, so add a "What's new" section there before tagging.
- Ask before tagging or publishing a release.
- The web installer is the `gh-pages` branch (served at the project's GitHub Pages URL): `index.html`, per-board `.json` manifests and `.bin` files. I did not find how those bins are refreshed from a release; check before assuming.
- Several other branches exist (`release-4.6`, `touchwifi`, `rotation`, `motion-sleep`, ...); `main` is the default branch and carries the latest merged release line.
