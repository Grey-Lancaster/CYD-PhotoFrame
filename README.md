# CYD PhotoFrame

> **About this branch:** `gh-pages` hosts the [web installer](https://grey-lancaster.github.io/CYD-PhotoFrame/) (`index.html`, the manifest `.json` files and the factory images they flash). The source code is on the [`main` branch](https://github.com/Grey-Lancaster/CYD-PhotoFrame), and every build is on the [Releases page](https://github.com/Grey-Lancaster/CYD-PhotoFrame/releases). The images here are the **v4.5.4** release:
>
> | File | Build |
> | --- | --- |
> | `firmware.bin` | `cyd` (ILI9341, normal colours) |
> | `firmware2.bin` | `cyd2usb` (ST7789) |
> | `CYD2bPhotoFrame.bin` | `cyd2b` (ILI9341, inverted colours) |
> | `cyd35.bin` | `cyd35` (3.5" CYD, ST7796 480×320) |
> | `freenove4.bin` | `freenove4` (Freenove 4.0" ESP32-S3, ST7796 480×320) |

A photo frame for the ESP32 **"Cheap Yellow Display" (CYD)** — a ~$15 board with a 320×240 touch screen, SD card slot and speaker. Photos live on the SD card, and you manage everything from your phone or PC through a built-in web page. It also runs on the **Freenove 4.0" ESP32-S3 display** (see [below](#freenove-40-esp32-s3)).

## ⚡ Easiest way to install

### [➡ Open the Web Installer](https://grey-lancaster.github.io/CYD-PhotoFrame/)

Plug the frame into your computer with a USB cable, click a button, done — no software to install.

- Use **Chrome or Edge on a computer**. Firefox, Safari and phones can't flash over USB.
- **Which button?** It depends on the display panel in your board, not the number of USB ports. Start with button **1**. If the screen stays white or the picture looks like a negative, nothing is damaged, just try another:

  | Installer button | Build | Display |
  | --- | --- | --- |
  | **1. CYD** | `cyd` | ILI9341, normal colours: the standard CYD and many "2 USB" boards |
  | **2. CYD2** | `cyd2usb` | ST7789 ("2 USB" boards with an ST7789 display) |
  | **3. CYD2 alternate** | `cyd2b` | ILI9341, inverted colours |
  | **4. 3.5" CYD** | `cyd35` | ST7796 480×320: the 3.5-inch CYD (see [below](#35-cyd)) |
  | **5. Freenove 4" S3** | `freenove4` | Freenove FNK0104S 4.0-inch ESP32-S3, ST7796 480×320, capacitive touch (see [below](#freenove-40-esp32-s3)) |

  Only the colours off? You don't need another file: in the frame's web page, *Settings*, tick **Swap red and blue** or **Invert colours**.
- Installing from the page replaces everything on the board, so you'll set up WiFi again. Photos on the SD card are not touched.
- If your computer doesn't see the board, install the CH340 USB driver (links on the installer page).

## Install from source

Prefer to build it yourself? Install [PlatformIO](https://platformio.org/) and run one of

```
pio run -e cyd -t upload          # standard CYD (ILI9341)
pio run -e cyd2usb -t upload      # CYD with 2 USB ports (ST7789)
pio run -e cyd2b -t upload        # CYD "2B" variant (inverted colours)
pio run -e cyd35 -t upload        # 3.5" CYD, ST7796 480x320 (see below)
pio run -e freenove4 -t upload      # Freenove 4.0" ESP32-S3, ST7796 480x320 (see below)
pio run -e cyd -t uploadfs        # splash image + favicon (data/ folder), once
```

## 3.5" CYD

The `cyd35` build is for the 3.5-inch CYD (ESP32-3248S035, ST7796 480×320, resistive touch). It differs from the 2.8" boards in two ways: the backlight is on GPIO27, and the touch controller shares the display's SPI pins, so touch is handled by TFT_eSPI (a bit-banged driver would take over the display's pins and freeze the screen).

- **Touch calibration:** the first time it starts, the frame asks you to touch four corners (the 2.8" boards ask for five crosshairs). It is saved; the web page's *System* panel has a **Recalibrate touch** button.
- **Colours look wrong?** In *Settings* try **Swap red and blue** and/or **Invert colours**; they apply immediately.
- **Tested** on the ESP32-32E 3.5" board (ST7796 + XPT2046) by a user, see [issue #3](https://github.com/Grey-Lancaster/CYD-PhotoFrame/issues/3): slideshow, touch, colours, QR code and the web page all work. Other 3.5" boards with different pins would need their own settings in `platformio.ini`; reports are welcome.
- Sound has not been tested on this board yet.

## Freenove 4.0" ESP32-S3

The `freenove4` build is for the **Freenove FNK0104S** 4.0-inch display board: ESP32-S3 with 16 MB flash and 8 MB PSRAM, ST7796 480×320 screen, **capacitive** touch, an SD card slot (4-bit SDMMC) and a speaker driven by an ES8311 audio codec. It has the same web page and photo handling as the CYD builds, with these differences:

- **WiFi is set up on the frame's own screen.** There is no `ESP32_AP` hotspot. On first start (or after holding BOOT while powering on) the frame lists the networks it can see: tap yours and type the password on the on-screen keyboard. It remembers several networks. This uses [TouchWifiProvisioner](https://github.com/Grey-Lancaster/TouchWifiProvisioner).
- **Touch is capacitive.** Use a finger; a plastic (resistive-style) stylus will not register. No calibration is needed.
- **Photos fill the 480×320 screen.** Small photos are enlarged as well as large ones shrunk (the CYD builds never enlarge). The web page's upload resize targets 480×320; tick *Crop to fill* to fill the whole screen, otherwise black bars keep the photo's shape.
- **Sound** plays through the on-board codec: upload `music.wav` or `music.mp3` as on the CYD. Uploads are checked by their real format, so an M4A renamed to `.mp3` is refused with an explanation instead of playing silence.
- **Installing:** use installer button **5**. Use the board's USB port; if the computer or installer can't find it, hold the BOOT button while plugging it in. Installing replaces the saved WiFi, as on the CYD.
- **Address:** the 4" frame answers at **http://photoframe4.local** (the CYDs use `photoframe.local`), so both can run on the same network.
- **Web updates:** `/update` works as on the CYD; upload `CYD-PhotoFrame-freenove4-update.bin` from a release.
- Built and tested on the FNK0104S 4.0-inch board. Freenove's other sizes (2.8", 3.5") use different displays and pins and would need their own build.


## First start

1. Put a FAT32 SD card with a few `.jpg` photos in the slot (or add them later from the web page).
2. Power the frame. It shows the splash screen, then tries your saved WiFi.
3. First time only: the 2.8" and 3.5" CYDs ask you to calibrate the touch screen (touch the crosshairs as they appear; it is saved). Then the frame asks for a **language** (English or Spanish) and lists the WiFi networks it can see on its own screen: tap yours (or choose *Hidden network...* for one that is not listed) and type the password on the on-screen keyboard. There is no `ESP32_AP` hotspot any more.
4. The screen shows the frame's address and a QR code. Open it in a browser (or use **http://photoframe.local**).

The frame keeps working without WiFi (it just shows the slideshow) and reconnects by itself when the network is back.
To pick a different WiFi network later, **hold the BOOT button while powering on**. The frame remembers several networks and joins the strongest one it knows. A frame updated from 4.3 or older keeps its saved WiFi.
If nobody touches a frame that has no WiFi saved for 3 minutes, it starts the slideshow without WiFi.

## Touch and buttons

| Action | Result |
| --- | --- |
| Touch left third | Previous photo |
| Touch right third (or BOOT button) | Next photo |
| Touch middle | Show IP address + QR code |

If left and right feel reversed on your board, uncomment `TOUCH_FLIP_X` in `1-Slideshow/app.h`.

On the Freenove board touch is capacitive: use a finger, not a plastic stylus.

## Web interface

- **Live preview** of what is on the frame, with prev / next / pause.
- **Upload** photos by drag-and-drop (several at once). Photos are **resized in your browser** to fit the screen before they are sent, so you don't need to prepare them. Crop-to-fill or letterbox, your choice.
- **Delete** photos, with thumbnails.
- **Settings** (saved on the frame): seconds per photo, shuffle, screen brightness, sound volume, and *night mode* (turn the screen off between two hours; needs WiFi once for the clock).
- **Motion sleep (experimental, all boards):** the screen turns off after a set number of minutes without anyone around and wakes when someone moves or touches it. It needs no extra hardware: the frame pings the router and watches how people moving change the WiFi signal (CSI). It is experimental: how well it works depends on the room and on where the frame and router sit, and the sensitivity slider (with a live score) is there to tune it. Off by default.
- **Sound**: upload `music.wav` or `music.mp3` and it plays (through the speaker) whenever you upload a photo, or on demand.
- **Firmware update** at `/update` (ElegantOTA). Upload the `firmware.bin` from a release or from a CI build.

There is no password on the web interface — keep the frame on a network you trust.

**Screen rotation** (a setting on the web page; the frame restarts when you change it): every board offers 0, 90, 180 and 270 degrees, so you can stand the frame up for portrait photos or put the USB port on any side. The CYDs ask you to touch the calibration targets the first time you use each orientation (every orientation keeps its own calibration); the Freenove needs no extra calibration. In the web page, the *Resize* option for uploads follows the frame's real size in the current rotation, so portrait photos are resized to fill a portrait screen.

## Photos

- Any size is fine; photos larger than the screen are scaled down (1/2, 1/4 or 1/8) as they are drawn. Smaller ones are centered.
- **Baseline JPEGs only.** Progressive JPEGs can't be decoded and are skipped. The browser upload always produces baseline JPEGs.
- Only files in the SD card's top-level folder are used (`.jpg` / `.jpeg`, any case).

## Hardware

Designed for the CYD ESP32-2432S028 family: ILI9341 (or ST7789) 320×240 display, XPT2046 touch, SD slot, speaker on GPIO26, backlight on GPIO21. The `freenove4` build is for the Freenove FNK0104S 4.0-inch ESP32-S3 display board (see above).

You can buy the board on [Amazon](https://amzn.to/3UVQwrV) (under $20) — or search for "ESP32-2432S028" / "Cheap Yellow Display" on AliExpress (about $13, 2-week shipping).

## Troubleshooting

- **White screen, or colours look like a negative** — "2 USB" boards come with either an ST7789 or an ILI9341 panel. Open the serial monitor and look for the `Display ID:` line: `ID4=0x009341` means ILI9341 (use `cyd`, or `cyd2b` if colours are inverted); `0x008552` means ST7789 (use `cyd2usb`).
- **Is it crashing or restarting?** Open the web page: the *System* panel shows **Last restart** (power on, crash, watchdog, brownout). "Brownout" or random "Power on" usually means a weak USB cable or port. A built-in watchdog restarts the frame if it ever hangs.
- **Photos look like a negative** — in the web page's *Settings* tick **Invert colours**.
- **Red and blue swapped (reds look blue)** — panels of the same type are wired differently. Open the web page, go to *Settings* and tick **Swap red and blue**, then *Save settings*. It applies immediately and is remembered.
- **Freenove: blank screen, and the computer keeps detecting/losing the USB port every few seconds** - the board was flashed in QIO flash mode. Reflash with the installer (or a `freenove4` build, which is set to DIO).
- **"No SD card"** — re-seat the card; the frame retries every few seconds. FAT32 only.
- **"Cannot show photos"** — all photos failed to decode (progressive/corrupt JPEG). Re-upload them through the web page.
- **Build fails with a framework error** — the project is pinned to `espressif32@6.10.0` (Arduino-ESP32 2.x). Newer platform releases use Arduino-ESP32 3.x, which this code doesn't support.

## Development

`platformio.ini` pins the platform and all libraries. The code lives in `1-Slideshow/`:

| File | Purpose |
| --- | --- |
| `1-Slideshow.cpp` | setup / loop, touch (and its calibration), night mode |
| `wifi_setup.cpp` | on-screen WiFi picker (TouchWifiProvisioner on LVGL) |
| `storage.cpp` | SD card, photo list & playlist, uploads, deletes (one mutex guards all SD access) |
| `display.cpp` | backlight, status screens, JPEG decode |
| `audio.cpp` | WAV / MP3 playback |
| `web.cpp`, `web_assets.h` | web server, JSON API, the single-page UI |
| `settings.cpp` | settings saved in flash |

The Freenove build has its own folder, `2-Freenove4/`, because its hardware differs from the CYDs: SD card over SDMMC instead of SPI, capacitive touch (`touch.cpp`), an I2S audio codec (`audio.cpp`, `es8311.*`), PSRAM-backed photo scaling (`display.cpp`) and an on-device WiFi picker (`wifi_setup.cpp`). The web server, web page and settings files are copies of the CYD ones. Its partition table is `2-Freenove4/partitions.csv` (two OTA app slots and a small SPIFFS), and flash mode must stay **DIO**: with QIO the ROM bootloader hangs and the board reboots every few seconds.

GitHub Actions builds every board on every push and keeps the `.bin` files as artifacts.

## Credits

Created by **Grey Lancaster** with help from ChatGPT, Claude and the open-source community. Built on TouchWifiProvisioner, LVGL, ESPAsyncWebServer, TFT_eSPI, SdFat, JPEGDEC, QRCode, ESP8266Audio and ElegantOTA — thank you to their authors.

The Freenove build also uses the FT6336U library and Espressif's ES8311 driver.

## License

[MIT](LICENSE)

---

# CYD PhotoFrame (Español)

Un marco de fotos para el ESP32 **"Cheap Yellow Display" (CYD)**, una placa de unos 15 $ con pantalla táctil de 320×240, ranura para tarjeta SD y altavoz. Las fotos están en la tarjeta SD y todo se gestiona desde el móvil o el PC con una página web integrada, que también está en español. Funciona además con la pantalla **Freenove de 4,0" con ESP32-S3**.

## ⚡ La forma más fácil de instalar

### [➡ Abrir el instalador web](https://grey-lancaster.github.io/CYD-PhotoFrame/)

Conecta el marco al ordenador con un cable USB, pulsa un botón y listo: no hay que instalar nada.

- Usa **Chrome o Edge en un ordenador**. Firefox, Safari y los móviles no pueden grabar por USB.
- **¿Qué botón?** Depende del panel de la pantalla de tu placa, no del número de puertos USB. Empieza por el botón **1**. Si la pantalla se queda en blanco o la imagen parece un negativo, no se ha estropeado nada: prueba otro.

  | Botón del instalador | Versión | Pantalla |
  | --- | --- | --- |
  | **1. CYD** | `cyd` | ILI9341, colores normales: el CYD estándar y muchas placas "2 USB" |
  | **2. CYD2** | `cyd2usb` | ST7789 (placas "2 USB" con pantalla ST7789) |
  | **3. CYD2 alternativo** | `cyd2b` | ILI9341, colores invertidos |
  | **4. CYD de 3,5"** | `cyd35` | ST7796 480×320: el CYD de 3,5 pulgadas |
  | **5. Freenove 4" S3** | `freenove4` | Freenove FNK0104S de 4,0 pulgadas con ESP32-S3, ST7796 480×320, táctil capacitivo |

  ¿Solo fallan los colores? No hace falta otro archivo: en la página web del marco, en *Ajustes*, marca **Intercambiar rojo y azul** o **Invertir colores**.
- Instalar desde la página reemplaza todo lo de la placa, así que tendrás que configurar de nuevo el WiFi. Las fotos de la tarjeta SD no se tocan.
- Si el ordenador no reconoce la placa, instala el controlador USB CH340 (hay enlaces en la página del instalador).

## Primer arranque

1. Pon en la ranura una tarjeta SD FAT32 con algunas fotos `.jpg` (o añádelas después desde la página web).
2. Enciende el marco. Muestra la pantalla de inicio y luego intenta conectarse al WiFi guardado.
3. Solo la primera vez: los CYD de 2,8" y 3,5" piden calibrar la pantalla táctil (toca las cruces a medida que aparecen; se guarda). Después el marco pregunta el **idioma** (inglés o español) y muestra en su pantalla las redes WiFi que ve: toca la tuya (o elige *Red oculta...* si no aparece) y escribe la contraseña con el teclado en pantalla. Ya no existe el punto de acceso `ESP32_AP`.
4. La pantalla muestra la dirección del marco y un código QR. Ábrela en un navegador (o usa **http://photoframe.local**).

El marco sigue funcionando sin WiFi (solo muestra las fotos) y se reconecta solo cuando vuelve la red.
Para elegir otra red WiFi más adelante, **mantén pulsado el botón BOOT al encenderlo**. El marco recuerda varias redes y se une a la más fuerte que conoce. Un marco actualizado desde la 4.3 o anterior conserva su WiFi.
Si nadie toca un marco sin WiFi guardado durante 3 minutos, empieza la presentación sin WiFi.

## Pantalla táctil y botones

| Acción | Resultado |
| --- | --- |
| Tocar el tercio izquierdo | Foto anterior |
| Tocar el tercio derecho (o botón BOOT) | Foto siguiente |
| Tocar el centro | Muestra la dirección IP y el código QR |

## Página web

- **Vista previa en vivo** de lo que muestra el marco, con anterior / siguiente / pausa.
- **Subir** fotos arrastrándolas (varias a la vez). Se **redimensionan en tu navegador** para ajustarse a la pantalla antes de enviarlas, así que no hace falta prepararlas. Puedes recortar para llenar la pantalla o dejar bandas negras.
- **Eliminar** fotos, con miniaturas.
- **Ajustes** (se guardan en el marco): segundos por foto, orden aleatorio, brillo, volumen y *modo nocturno* (apaga la pantalla entre dos horas; necesita WiFi una vez para el reloj).
- **Apagado por movimiento (experimental, todas las placas):** la pantalla se apaga tras unos minutos sin nadie cerca y se enciende cuando alguien se mueve o la toca. No necesita hardware adicional: el marco hace ping al router y observa cómo el movimiento de las personas cambia la señal WiFi (CSI). Es experimental: el resultado depende de la habitación y de dónde estén el marco y el router, y el control de sensibilidad (con un nivel en vivo) sirve para ajustarlo. Desactivado por defecto.
- **Sonido**: sube `music.wav` o `music.mp3` y suena (por el altavoz) al subir una foto o cuando quieras.
- **Actualización del firmware** en `/update`. Sube el `firmware.bin` de una versión publicada.
- **Idioma**: la página aparece en el idioma del marco; el interruptor **EN / ES** de la esquina cambia también el idioma del marco.
- **Rotación de la pantalla**: 0, 90, 180 y 270 grados en todas las placas, para poner el marco en vertical o el USB en cualquier lado. Los CYD piden calibrar la pantalla táctil la primera vez que usas cada orientación.

No hay contraseña en la página web: conecta el marco solo a una red de confianza.

## Problemas frecuentes

- **Pantalla en blanco o colores como un negativo**: las placas "2 USB" traen un panel ST7789 o ILI9341. Prueba otro botón del instalador (ver arriba).
- **Fotos como un negativo**: en *Ajustes* marca **Invertir colores**.
- **Rojo y azul intercambiados**: en *Ajustes* marca **Intercambiar rojo y azul** y pulsa *Guardar ajustes*.
- **"No SD card"**: vuelve a insertar la tarjeta; el marco reintenta cada pocos segundos. Solo FAT32.
- **Las fotos no se ven**: solo se admiten JPEG de tipo *baseline*. Súbelas desde la página web, que las convierte.

Más detalles técnicos (compilar desde el código, el desarrollo) en la parte inglesa de arriba.

## Créditos y licencia

Creado por **Grey Lancaster** con ayuda de ChatGPT, Claude y la comunidad de código abierto. Gracias a los autores de TouchWifiProvisioner, LVGL, ESPAsyncWebServer, TFT_eSPI, SdFat, JPEGDEC, QRCode, ESP8266Audio y ElegantOTA. Licencia [MIT](LICENSE).

*La traducción al español es nueva; se agradecen las correcciones.*
