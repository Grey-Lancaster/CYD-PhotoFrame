# PhotoFrame

A feature-rich photo frame project powered by the ESP32 microcontroller with a built-in 320x240 TFT display. Ideal for showcasing slideshows, playing audio, and more.

---

## Features

- **TFT Display**: Displays photos and other instructions on a 320x240 resolution.
- **Audio Playback**: Supports `.wav` and `.mp3` files, played from an SD card.
- **Slideshow**: Automatically cycles through images stored on the SD card.
- **WiFi Configuration**: Easily connect to WiFi using a built-in WiFiManager interface.
- ~~**OTA Updates**: Update the firmware wirelessly using ElegantOTA.~~ (not yet implemented)
- **Web Interface**: Upload and manage photos directly through the web.
- **Customizable Interface**: Use QR codes and vanity screens to guide and inform users.
- **Compact Design**: Built for the "Cheap Yellow Display" (CYD) board, a cost-effective solution under $20.

---

## Board Description

This project is designed for the **Cheap Yellow Display (CYD)** ESP32 board, featuring:
- 320x240 TFT display
- SD card support
- Speaker output
- GPIO pins

You can purchase this board on:
- [Amazon](https://amzn.to/3UVQwrV) (under $20) *(right-click to open in a new tab)*
- [AliExpress](#) (under $13, 2-week shipping)

---

## Setup Instructions

1. **Prepare the Filesystem**:
   - Create a `data` folder in your project directory.
   - Add a 320x240 JPEG file named `vanity.jpg` to this folder.
   - Use PlatformIO's "Build Filesystem Image" and "Upload Filesystem Image" to upload the file to SPIFFS.

2. **Upload the Code**:
   - Use PlatformIO to upload the firmware to your CYD board.
   - Alternatively, download the precompiled `.bin` file from the [Releases](#) section and flash it using the ESP32 Flash Download Tool.

3. **WiFi Configuration**:
   - If the board is not connected to WiFi, follow these steps:
     - Connect to the ESP32 AP (`ESP32_AP`).
     - Open your browser and go to `192.168.4.1` to configure WiFi.

4. **Start the Frame**:
   - Once connected, the frame will:
     - Display your `vanity.jpg`.
     - Begin the slideshow from the SD card.
     - Play audio files as uploaded.

---

## Web Interface

Access the web interface at `<device-IP>` (e.g., `192.168.1.x`) to:
- Upload images and audio files
- Adjust slideshow speed
- Trigger audio playback
- Check device status

---

## Preparing Images and Audio

1. **Images**:
   - Resize all images to `320x240` pixels.
   - Save them as `.jpg` files for better performance.

2. **Audio**:
   - Use [Audacity](https://www.audacityteam.org/) to convert audio to `.wav` format for playback.

---

## Advanced Features

- **mDNS**: Access your device via `photoframe.local` without needing the IP address.
- **OTA Updates~~**: Update firmware using ElegantOTA by navigating to `/update` on the web interface.
- - **Customization**: Modify the vanity screen (`vanity.jpg`) for personalized branding.

---

## Project Files and Notes

- **README**: Basic instructions.
- **Wiki**: Detailed guides, including:
  - Using the `.bin` file
  - Uploading files to SPIFFS
  - Preparing audio and images
  - Creating a 3D printed case
- **.bin File**: Precompiled firmware for easy flashing.

---

## Contributions

Feel free to contribute! Fork the repo, make changes, and submit a pull request. Ideas and bug reports are also welcome.

---

## License

This project is licensed under the [MIT License](LICENSE).

---

## About

Developed by **Grey Lancaster**. Thanks to the open-source community for inspiration and support.

