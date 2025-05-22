/*
  PhotoFrame 2.2 - Slideshow with Web Interface, WAV Playback, and WebSocket Synchronization

  This project creates an advanced ESP32-powered photo frame that displays a slideshow of images,
  hosts a web interface for controlling slideshow speed, uploading/deleting images, and plays
  audio using the built-in DAC. It also provides a synchronized slideshow webpage using WebSockets.

  Created by: ChatGPT (OpenAI), Claude AI,  Grey Lancaster, and the Open-Source Community

  Special Thanks to the following libraries and developers:
  -------------------------------------------------------------------------------
  - WiFiManager by tzapu (tzapu/WiFiManager): Helps manage Wi-Fi connections easily.
  - ESPAsyncWebServer by me-no-dev: Provides asynchronous web server functionalities.
  - TFT_eSPI by Bodmer (Bodmer/TFT_eSPI): For controlling the TFT display.
  - XPT2046_Bitbang by nitek: For interfacing with the touchscreen.
  - SdFat by Greiman (greiman/SdFat): Advanced SD card handling.
  - JPEGDEC by BitBank (bitbank2/JPEGDEC): JPG decoding for image display.
  - QRCode by ricmoo: To display QR codes on the TFT screen.
  - ESP32-audioI2S by schreibfaul1: For WAV and MP3 audio playback via I2S.
  - AudioFileSourceSD by Phil Schatzmann: Provides audio file reading from SD.
  - mDNS (ESP32 Core): Enables access to the device using photoframe.local.
  - FS (ESP32 Core): File system handling.
  - ElegantOTA by Ayush Sharma

  Project Features:
  -------------------------------------------------------------------------------
  - Displays a slideshow of images stored on the SD card.
  - Web-based interface for controlling slideshow speed, uploading and deleting images.
  - Plays a WAV file when a file is uploaded or on request via the web interface.
  - Displays the IP address and mDNS hostname (photoframe.local) on the TFT screen.
  - QR code generation to allow easy access to the web interface.
  - Provides a /slideshow webpage to display the current image, synchronized via WebSocket.

  Acknowledgements:
  -------------------------------------------------------------------------------
  This project would not be possible without the open-source community and the
  many talented developers who have contributed to the libraries we utilized.

  Created with the assistance of ChatGPT from OpenAI, guided by Grey Lancaster.
  
  Date: September 2024
*/

#include <WiFi.h>
#include <WiFiManager.h>          // WiFiManager to manage Wi-Fi connections
#include <AsyncTCP.h>             // Async TCP library for WebSocket

#include <AsyncEventSource.h>
#include <TFT_eSPI.h>             // TFT display library
#include <XPT2046_Bitbang.h>      // Touch screen library
#include <SPI.h>
#include <SdFat.h>                // SD card library (SdFat)
#include <SD.h>                   // Standard SD library
#include <JPEGDEC.h>              // JPG decoder library
#include "SPIFFS.h"               // Include SPIFFS to satisfy TFT_eSPI dependency
#include "qrcode.h"               // QR code library
#include "AudioFileSourceSD.h"    // Audio file source from SD card
#include "AudioGeneratorWAV.h"    // WAV audio generator
#include "AudioOutputI2S.h"       // I2S audio output
#include <FS.h>                   // Include FS.h
#include "esp_task_wdt.h"         // ESP32 Task Watchdog Timer
#include "esp_heap_caps.h"        // Heap memory debugging
#include <ESPmDNS.h>
#include <ElegantOTA.h>  // Using the official ElegantOTA with async mode enabled

// Touch Screen pins
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// SD card pins
#define SD_CS 5 // Chip Select for SD Card (IO5)

TFT_eSPI tft = TFT_eSPI();
JPEGDEC jpeg;
XPT2046_Bitbang ts(XPT2046_MOSI, XPT2046_MISO, XPT2046_CLK, XPT2046_CS);

// Audio objects
AudioGeneratorWAV *wav;
AudioFileSourceSD *file;
AudioOutputI2S *out = nullptr;  // Initialize to nullptr

SPIClass sdSpi(VSPI);
SdSpiConfig sdSpiConfig(SD_CS, SHARED_SPI, SD_SCK_MHZ(10), &sdSpi);  // Use SHARED_SPI mode
SdFat sd;
SdBaseFile sdRoot;
SdBaseFile jpgFile;
int16_t currentIndex = 0;
uint16_t fileCount = 0;

uint32_t timer;
SemaphoreHandle_t xSpiMutex;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");  // Create a WebSocket object
WiFiManager wm;
volatile bool buttonPressed = false;

bool sdMounted = false;
bool slideshowActive = true;

// Function declarations
void setupWebServer();
void loadImage(uint16_t targetIndex);
void decodeJpeg(const char *name);
void displayMessageAndQRCode(String ip);
void displayQRCode(String ip);
void playWAV();
void stopSlideshow();
void restartSlideshow();
void error(const char* msg);
bool checkAndMountSDCard();
void playWAVTask(void * parameter);
int X = 10;  // Default time in seconds (X * 1000 milliseconds)
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                      void *arg, uint8_t *data, size_t len);

// Global variable to store the current image name
String currentImageName = "";



// Button interrupt for slideshow control
void IRAM_ATTR buttonInt() {
  buttonPressed = true;
}

// JPG decoding functions
int JPEGDraw(JPEGDRAW *pDraw) {
  tft.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
  return 1;
}

void *myOpen(const char *filename, int32_t *size) {
  jpgFile = sd.open(filename);
  *size = jpgFile.fileSize();
  return &jpgFile;
}

void myClose(void *handle) {
  if (jpgFile) jpgFile.close();
}

int32_t myRead(JPEGFILE *handle, uint8_t *buffer, int32_t length) {
  return jpgFile.read(buffer, length);
}

int32_t mySeek(JPEGFILE *handle, int32_t position) {
  return jpgFile.seekSet(position);
}

// Special function for JPEGDEC file operations with SPIFFS
void *spiffsOpen(const char *filename, int32_t *size) {
  File f = SPIFFS.open(filename);
  if (!f) {
    Serial.printf("Failed to open %s from SPIFFS\n", filename);
    return NULL;
  }
  *size = f.size();
  return (void *)new File(f);
}

void spiffsClose(void *handle) {
  if (handle) {
    File *f = (File *)handle;
    f->close();
    delete f;
  }
}

int32_t spiffsRead(JPEGFILE *handle, uint8_t *buffer, int32_t length) {
  File *f = (File *)handle->fHandle;
  return f->read(buffer, length);
}

int32_t spiffsSeek(JPEGFILE *handle, int32_t position) {
  File *f = (File *)handle->fHandle;
  return f->seek(position);
}

// Stop the slideshow by releasing the resources and stopping the decoder
void stopSlideshow() {
  slideshowActive = false;
  xSemaphoreTake(xSpiMutex, portMAX_DELAY);  // Lock SPI access for SD card
  jpeg.close();  // Ensure that the JPEG decoder is closed
  xSemaphoreGive(xSpiMutex);  // Unlock SPI access
  delay(500);  // Add delay to ensure proper transition
  Serial.println("Slideshow stopped.");
}

// Restart the slideshow after WAV playback
void restartSlideshow() {
  slideshowActive = true;
  loadImage(currentIndex);
  Serial.println("Slideshow restarted.");
}

// Function to load and display an image
void loadImage(uint16_t targetIndex) {
  if (!slideshowActive) return;

  if (targetIndex >= fileCount) targetIndex = 0;
  sdRoot.rewind();
  uint16_t index = 0;
  SdBaseFile entry;
  char name[100];
  while (entry.openNext(&sdRoot)) {
    if (!entry.isSubDir()) {
      entry.getName(name, sizeof(name));
      if (strcasecmp(name + strlen(name) - 3, "JPG") == 0) {
        if (index == targetIndex) {
          currentImageName = String(name);  // Set current image name
          decodeJpeg(name);
          entry.close();

          // Send WebSocket message to notify clients
          ws.textAll("update");

          return;
        }
        index++;
      }
    }
    entry.close();
  }
}

void decodeJpeg(const char *name) {
  if (!slideshowActive) return;

  xSemaphoreTake(xSpiMutex, portMAX_DELAY);  // Lock SPI access for SD card
  if (!jpeg.open(name, myOpen, myClose, myRead, mySeek, JPEGDraw)) {
    xSemaphoreGive(xSpiMutex);  // Unlock SPI access
    return;
  }
  tft.fillScreen(TFT_BLACK);  // Clear screen if the image doesn't fill it
  jpeg.decode((tft.width() - jpeg.getWidth()) / 2, (tft.height() - jpeg.getHeight()) / 2, 0);
  jpeg.close();
  xSemaphoreGive(xSpiMutex);  // Unlock SPI access
}

// Error handling function
void error(const char* msg) {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.println(msg);
  Serial.println(msg);  // Also print to Serial for debugging
  
}

// Function to check and mount SD card if not already mounted
bool checkAndMountSDCard() {
  if (!sdMounted) {  // Only try to mount if it's not already mounted
    // Try to remount the SD card
    if (!sd.begin(sdSpiConfig)) {
      Serial.println("SD Card Mount Failed");
      sdMounted = false;
      return false;
    } else {
      sdMounted = true;  // Successfully mounted SD card
      Serial.println("SD Card Mounted Successfully");
    }
  }
  return true;
}

// Play the WAV file (always "music.wav") with SD card reinitialization
void playWAV() {
  Serial.printf("Free heap before playback: %d bytes\n", esp_get_free_heap_size());

  xSemaphoreTake(xSpiMutex, portMAX_DELAY);  // Lock SPI access

  // Unmount SdFat
  sd.end();
  sdMounted = false;
  delay(500);  // Allow time to unmount

  // Initialize standard SD library
  if (!SD.begin(SD_CS, sdSpi)) {  // Use the same SPI configuration
    Serial.println("Failed to initialize standard SD library");
    xSemaphoreGive(xSpiMutex);     // Unlock SPI access
    return;
  }

  xSemaphoreGive(xSpiMutex);       // Unlock SPI before starting playback

  // Open WAV file from SD card
  file = new AudioFileSourceSD("/music.wav");
  wav = new AudioGeneratorWAV();

  if (wav->begin(file, out)) {
    Serial.println("Started playing WAV file");
  } else {
    Serial.println("Failed to start WAV playback");
    // Clean up
    wav->stop();
    delete wav;
    delete file;
    return;
  }

  // Play the WAV file to completion
  while (wav->isRunning()) {
    if (!wav->loop()) {
      wav->stop();
      Serial.println("WAV playback finished");
    }
    delay(1);  // Yield to other tasks
  }

  // Clean up
  wav->stop();
  delete wav;
  delete file;

  xSemaphoreTake(xSpiMutex, portMAX_DELAY);  // Lock SPI access again

  // Unmount SD and remount SdFat
  SD.end();
  delay(500);  // Allow time to unmount SD

  // Remount SdFat
  if (!checkAndMountSDCard()) {
    Serial.println("Failed to remount SdFat after WAV playback.");
    xSemaphoreGive(xSpiMutex);
    return;
  }

  xSemaphoreGive(xSpiMutex);  // Unlock SPI access

  Serial.printf("Free heap after playback: %d bytes\n", esp_get_free_heap_size());
}

// FreeRTOS task for WAV playback
void playWAVTask(void * parameter) {
  playWAV();
  restartSlideshow();  // Restart the slideshow after playback
  vTaskDelete(NULL);   // Delete this task when done
}

// Web server handler to play WAV file and show a "Go Back" button
void handlePlayMusicRequest(AsyncWebServerRequest *request) {
  // Stop the slideshow before playing music
  stopSlideshow();

  // Send the response with "Play Music" confirmation and a "Go Back" button
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
  <link rel="icon" type="image/x-icon" href="/favicon.ico">
    <title>Play Music</title>
    <style>
      body {
        font-family: Arial, sans-serif;
        background-color: #f0f0f0;
        text-align: center;
        margin: 0;
        padding: 0;
      }
      h1 {
        background-color: #333;
        color: white;
        padding: 20px;
        margin: 0;
      }
      .container {
        padding: 20px;
      }
      .button {
        display: inline-block;
        padding: 15px 25px;
        font-size: 16px;
        margin: 10px;
        cursor: pointer;
        text-align: center;
        text-decoration: none;
        outline: none;
        color: #fff;
        background-color: #4CAF50;
        border: none;
        border-radius: 15px;
        box-shadow: 0 5px #999;
      }
      .button:hover {background-color: #3e8e41}
      .button:active {
        background-color: #3e8e41;
        box-shadow: 0 3px #666;
        transform: translateY(2px);
      }
    </style>
  </head>
  <body>
    <h1>Playing Music</h1>
    <div class="container">
      <p>Now playing <strong>music.wav</strong></p>
      <a href="/" class="button">Go Back to Main Page</a>
    </div>
  </body>
  </html>
  )rawliteral";

  request->send(200, "text/html", html);

  // Play "music.wav" in a separate task
  xTaskCreatePinnedToCore(
    playWAVTask,      // Function to implement the task
    "playWAVTask",    // Name of the task
    8192,             // Stack size in words
    NULL,             // Task input parameter
    1,                // Priority of the task
    NULL,             // Task handle
    1                 // Core where the task should run
  );

  // No need to call restartSlideshow() here; it will be called after playback
}

// Function to handle file upload and play music.wav after any file upload
void handleFileUpload(AsyncWebServerRequest *request) {
  // Stop the slideshow before handling the upload
  stopSlideshow();

  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <title>Upload Successful</title>
    <style>
      body {
        font-family: Arial, sans-serif;
        background-color: #f0f0f0;
        text-align: center;
        margin: 0;
        padding: 0;
      }
      h1 {
        background-color: #333;
        color: white;
        padding: 20px;
        margin: 0;
      }
      .container {
        padding: 20px;
      }
      .button {
        display: inline-block;
        padding: 15px 25px;
        font-size: 16px;
        margin: 10px;
        cursor: pointer;
        text-align: center;
        text-decoration: none;
        outline: none;
        color: #fff;
        background-color: #4CAF50;
        border: none;
        border-radius: 15px;
        box-shadow: 0 5px #999;
      }
      .button:hover {background-color: #3e8e41}
      .button:active {
        background-color: #3e8e41;
        box-shadow: 0 3px #666;
        transform: translateY(2px);
      }
    </style>
  </head>
  <body>
    <h1>Upload Successful</h1>
    <div class="container">
      <p>File uploaded successfully!</p>
      <a href="/upload_file" class="button">Upload More Files</a>
      <a href="/" class="button">Go Back to Main Page</a>
    </div>
  </body>
  </html>
  )rawliteral";

  request->send(200, "text/html", html);

  // Rebuild the file list after upload
  sdRoot.rewind();
  fileCount = 0;
  SdBaseFile fileEntry;
  char name[100];
  while (fileEntry.openNext(&sdRoot)) {
    fileEntry.getName(name, sizeof(name));
    if (strcasecmp(name + strlen(name) - 3, "JPG") == 0) {
      fileCount++;
    }
    fileEntry.close();
  }
  currentIndex = 0;

  // Play "music.wav" after any file is uploaded
  if (sd.exists("/music.wav")) {  // Use sd.exists() instead of SD.exists()
    Serial.println("Playing music.wav after file upload.");
    xTaskCreatePinnedToCore(
      playWAVTask,      // Function to implement the task
      "playWAVTask",    // Name of the task
      8192,             // Stack size in words
      NULL,             // Task input parameter
      1,                // Priority of the task
      NULL,             // Task handle
      1                 // Core where the task should run
    );
  } else {
    Serial.println("music.wav not found on the SD card.");
    // Restart the slideshow immediately if WAV file is not found
    restartSlideshow();
  }
}

// WebSocket event handler
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    // Let the client know the current image immediately when they connect
    client->text("update");
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  }
}
// Setup the web server, including routes for speed, upload, delete, and slideshow
// Setup the web server, including routes for speed, upload, delete, and slideshow
void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>PhotoFrame 2.2</title>
      <style>
        body {
          font-family: Arial, sans-serif;
          background-color: #f0f0f0;
          text-align: center;
          margin: 0;
          padding: 0;
        }
        h1 {
          background-color: #333;
          color: white;
          padding: 20px;
          margin: 0;
        }
        .container {
          padding: 20px;
        }
        .button {
          display: inline-block;
          padding: 15px 25px;
          font-size: 16px;
          margin: 10px;
          cursor: pointer;
          text-align: center;
          text-decoration: none;
          outline: none;
          color: #fff;
          background-color: #4CAF50;
          border: none;
          border-radius: 15px;
          box-shadow: 0 5px #999;
        }
        .button:hover {background-color: #3e8e41}
        .button:active {
          background-color: #3e8e41;
          box-shadow: 0 3px #666;
          transform: translateY(2px);
        }
      </style>
    </head>
    <body>
      <h1>PhotoFrame 2.2</h1>
      <div class="container">
        <p>Use the following options:</p>
        <a href="/upload_file" class="button">Upload a New Image</a>
        <a href="/delete" class="button">Delete Images</a>
        <a href="/play-music" class="button">Play Music</a>
        <a href="/speed" class="button">Set Slideshow Speed</a>
        <a href="/slideshow" class="button">View Slideshow</a>
        <a href="/about" class="button">About</a>
      </div>
    </body>
    </html>
    )rawliteral";
    request->send(200, "text/html", html);
  });

  // Define the /speed page
  server.on("/speed", HTTP_GET, [&](AsyncWebServerRequest *request) {
      String html = R"rawliteral(
      <!DOCTYPE html>
      <html>
      <head>
        <title>Set Slideshow Speed</title>
        <style>
          body {
            font-family: Arial, sans-serif;
            background-color: #f0f0f0;
            text-align: center;
            margin: 0;
            padding: 0;
          }
          h1 {
            background-color: #333;
            color: white;
            padding: 20px;
            margin: 0;
          }
          .container {
            padding: 20px;
          }
          .input-field {
            padding: 10px;
            font-size: 16px;
            width: 200px;
            margin-bottom: 20px;
          }
          .button {
            display: inline-block;
            padding: 15px 25px;
            font-size: 16px;
            margin: 10px;
            cursor: pointer;
            text-align: center;
            text-decoration: none;
            outline: none;
            color: #fff;
            background-color: #4CAF50;
            border: none;
            border-radius: 15px;
            box-shadow: 0 5px #999;
          }
          .button:hover {background-color: #3e8e41}
          .button:active {
            background-color: #3e8e41;
            box-shadow: 0 3px #666;
            transform: translateY(2px);
          }
        </style>
      </head>
      <body>
        <h1>Set Slideshow Speed</h1>
        <div class="container">
          <form action="/set-speed" method="POST">
            <label for="speed">Enter slideshow speed (in seconds):</label><br>
            <input type="number" id="speed" name="speed" min="1" value=")rawliteral" + String(X) + R"rawliteral(" class="input-field"><br>
            <input type="submit" value="Set Speed" class="button">
          </form>
          <a href="/" class="button">Go Back to Main Page</a>
        </div>
      </body>
      </html>
      )rawliteral";
      request->send(200, "text/html", html);
  });

  // Define the /set-speed page
  server.on("/set-speed", HTTP_POST, [&](AsyncWebServerRequest *request) {
    if (request->hasParam("speed", true)) {
        String speedValue = request->getParam("speed", true)->value();
        X = speedValue.toInt();
        Serial.printf("Slideshow speed updated to: %d seconds\n", X);
    }
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>Speed Updated</title>
      <style>
        body {
          font-family: Arial, sans-serif;
          background-color: #f0f0f0;
          text-align: center;
          margin: 0;
          padding: 0;
        }
        h1 {
          background-color: #333;
          color: white;
          padding: 20px;
          margin: 0;
        }
        .container {
          padding: 20px;
        }
        .button {
          display: inline-block;
          padding: 15px 25px;
          font-size: 16px;
          margin: 10px;
          cursor: pointer;
          text-align: center;
          text-decoration: none;
          outline: none;
          color: #fff;
          background-color: #4CAF50;
          border: none;
          border-radius: 15px;
          box-shadow: 0 5px #999;
        }
        .button:hover {background-color: #3e8e41}
        .button:active {
          background-color: #3e8e41;
          box-shadow: 0 3px #666;
          transform: translateY(2px);
        }
      </style>
    </head>
    <body>
      <h1>Slideshow Speed Updated</h1>
      <div class="container">
        <p>Slideshow speed updated successfully!</p>
        <a href="/speed" class="button">Go Back</a>
        <a href="/" class="button">Go Back to Main Page</a>
      </div>
    </body>
    </html>
    )rawliteral";
    request->send(200, "text/html", html);
  });

  // Route to handle play music button
  server.on("/play-music", HTTP_GET, handlePlayMusicRequest);

  // File upload form handler
  server.on("/upload_file", HTTP_GET, [](AsyncWebServerRequest *request) {
      String html = R"rawliteral(
      <!DOCTYPE html>
      <html>
      <head>
        <title>Upload Image</title>
        <style>
          body {
            font-family: Arial, sans-serif;
            background-color: #f0f0f0;
            text-align: center;
            margin: 0;
            padding: 0;
          }
          h1 {
            background-color: #333;
            color: white;
            padding: 20px;
            margin: 0;
          }
          .container {
            padding: 20px;
          }
          .button, .submit-button {
            display: inline-block;
            padding: 15px 25px;
            font-size: 16px;
            margin: 10px;
            cursor: pointer;
            text-align: center;
            text-decoration: none;
            outline: none;
            color: #fff;
            background-color: #4CAF50;
            border: none;
            border-radius: 15px;
            box-shadow: 0 5px #999;
          }
          .button:hover, .submit-button:hover {background-color: #3e8e41}
          .button:active, .submit-button:active {
            background-color: #3e8e41;
            box-shadow: 0 3px #666;
            transform: translateY(2px);
          }
          .input-file {
            font-size: 16px;
            margin-bottom: 20px;
          }
        </style>
      </head>
      <body>
        <h1>Upload a New Image</h1>
        <div class="container">
          <form method="POST" action="/upload_file" enctype="multipart/form-data">
            <input type="file" name="file" id="file" class="input-file"><br>
            <input type="submit" value="Upload" class="submit-button">
          </form>
          <a href="/" class="button">Go Back to Main Page</a>
        </div>
      </body>
      </html>
      )rawliteral";
      request->send(200, "text/html", html);
  });

  // File upload processing handler
  server.on("/upload_file", HTTP_POST, handleFileUpload,
      [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
          static SdBaseFile file;
          if (index == 0) {
              if (!file.open(("/" + filename).c_str(), O_WRITE | O_CREAT | O_TRUNC)) return;
          }
          if (file.write(data, len) != len) return;
          if (final) file.close();
      }
  );

  // File delete page handler
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request) {
      String html = R"rawliteral(
      <!DOCTYPE html>
      <html>
      <head>
        <title>Delete Images</title>
        <style>
          body {
            font-family: Arial, sans-serif;
            background-color: #f0f0f0;
            text-align: center;
            margin: 0;
            padding: 0;
          }
          h1 {
            background-color: #333;
            color: white;
            padding: 20px;
            margin: 0;
          }
          .container {
            padding: 20px;
            text-align: left;
            display: inline-block;
          }
          .button, .submit-button {
            display: inline-block;
            padding: 15px 25px;
            font-size: 16px;
            margin: 10px;
            cursor: pointer;
            text-align: center;
            text-decoration: none;
            outline: none;
            color: #fff;
            background-color: #4CAF50;
            border: none;
            border-radius: 15px;
            box-shadow: 0 5px #999;
          }
          .button:hover, .submit-button:hover {background-color: #3e8e41}
          .button:active, .submit-button:active {
            background-color: #3e8e41;
            box-shadow: 0 3px #666;
            transform: translateY(2px);
          }
          input[type=checkbox] {
            margin: 5px;
            transform: scale(1.5);
          }
        </style>
      </head>
      <body>
        <h1>Delete Images</h1>
        <div class="container">
          <form method="POST" action="/delete_files">
      )rawliteral";

      // Make sure SD card is initialized
      if (!checkAndMountSDCard()) {
          request->send(500, "text/html", "<h3>SD Card Mount Failed!</h3>");
          return;
      }

      sdRoot.rewind();
      SdBaseFile entry;
      char name[100];
      bool fileFound = false;

      // Iterate through all files
      while (entry.openNext(&sdRoot)) {
          entry.getName(name, sizeof(name));
          Serial.printf("Found file: %s\n", name);

          // Skip unwanted files or directories
          if (strcasecmp(name, "System Volume Information") == 0) {
              entry.close();
              continue;  // Skip this file or directory
          }

          // List all other files
          html += "<input type='checkbox' name='file' value='" + String(name) + "'>" + String(name) + "<br>";
          fileFound = true;
          entry.close();
      }

      if (!fileFound) {
          html += "<p>No files found.</p>";
          Serial.println("No files found for deletion.");
      }

      html += R"rawliteral(
            <input type="submit" value="Delete Selected Files" class="submit-button">
          </form>
          <a href="/" class="button">Go Back to Main Page</a>
        </div>
      </body>
      </html>
      )rawliteral";

      request->send(200, "text/html", html);
  });

  // File deletion handler
  server.on("/delete_files", HTTP_POST, [](AsyncWebServerRequest *request) {
      int params = request->params();
      bool deletionSuccess = true;
      for (int i = 0; i < params; i++) {
          const AsyncWebParameter* p = request->getParam(i);
          if (p->isPost()) {
              String fileToDelete = "/" + p->value();
              if (sd.exists(fileToDelete.c_str())) {
                  if (sd.remove(fileToDelete.c_str())) {
                      Serial.printf("File deleted: %s\n", fileToDelete.c_str());
                  } else {
                      Serial.printf("Failed to delete file: %s\n", fileToDelete.c_str());
                      deletionSuccess = false;
                  }
              } else {
                  Serial.printf("File not found: %s\n", fileToDelete.c_str());
                  deletionSuccess = false;
              }
          }
      }
      String html = R"rawliteral(
      <!DOCTYPE html>
      <html>
      <head>
        <title>Delete Images</title>
        <style>
          body {
            font-family: Arial, sans-serif;
            background-color: #f0f0f0;
            text-align: center;
            margin: 0;
            padding: 0;
          }
          h1 {
            background-color: #333;
            color: white;
            padding: 20px;
            margin: 0;
          }
          .container {
            padding: 20px;
          }
          .button {
            display: inline-block;
            padding: 15px 25px;
            font-size: 16px;
            margin: 10px;
            cursor: pointer;
            text-align: center;
            text-decoration: none;
            outline: none;
            color: #fff;
            background-color: #4CAF50;
            border: none;
            border-radius: 15px;
            box-shadow: 0 5px #999;
          }
          .button:hover {background-color: #3e8e41}
          .button:active {
            background-color: #3e8e41;
            box-shadow: 0 3px #666;
            transform: translateY(2px);
          }
        </style>
      </head>
      <body>
        <h1>Delete Images</h1>
        <div class="container">
      )rawliteral";

      if (deletionSuccess) {
          html += "<p>Selected images deleted successfully!</p>";
      } else {
          html += "<p>Failed to delete some images!</p>";
      }

      html += R"rawliteral(
          <a href="/delete" class="button">Go Back</a>
          <a href="/" class="button">Go Back to Main Page</a>
        </div>
      </body>
      </html>
      )rawliteral";

      request->send(200, "text/html", html);
  });

  // About page route
  server.on("/about", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>About PhotoFrame 2.2</title>
      <style>
        body {
          font-family: Arial, sans-serif;
          background-color: #f0f0f0;
          text-align: center;
          margin: 0;
          padding: 0;
        }
        h1 {
          background-color: #333;
          color: white;
          padding: 20px;
          margin: 0;
        }
        .container {
          padding: 20px;
        }
        .button {
          display: inline-block;
          padding: 15px 25px;
          font-size: 16px;
          margin: 10px;
          cursor: pointer;
          text-align: center;
          text-decoration: none;
          outline: none;
          color: #fff;
          background-color: #4CAF50;
          border: none;
          border-radius: 15px;
          box-shadow: 0 5px #999;
        }
        .button:hover {background-color: #3e8e41}
        .button:active {
          background-color: #3e8e41;
          box-shadow: 0 3px #666;
          transform: translateY(2px);
        }
        ul {
          text-align: left;
          display: inline-block;
          margin: 0;
          padding: 0;
        }
      </style>
    </head>
    <body>
      <h1>About PhotoFrame 2.2</h1>
      <div class="container">
        <p>This project creates an advanced ESP32-powered photo frame that displays a slideshow of images, hosts a web interface for controlling slideshow speed, uploading/deleting images, and plays audio using the built-in DAC.</p>
        <h3>Created by:</h3>
        <p>ChatGPT (OpenAI), Claude AI, Grey Lancaster, and the Open-Source Community</p>
        <h3>Special Thanks to the following libraries and developers:</h3>
        <ul>
          <li>WiFiManager by tzapu</li>
          <li>ESPAsyncWebServer (AsyncWebServer)</li>
          <li>TFT_eSPI by Bodmer</li>
          <li>XPT2046_Bitbang by nitek</li>
          <li>SdFat by Greiman</li>
          <li>JPEGDEC by BitBank</li>
          <li>QRCode by ricmoo</li>
          <li>ESP32-audioI2S by schreibfaul1</li>
          <li>AudioFileSourceSD by Phil Schatzmann</li>
          <li>mDNS (ESP32 Core)</li>
          <li>FS (ESP32 Core)</li>
          <li>ElegantOTA by Ayush Sharma</li>
        </ul>
        <p>This project would not be possible without the open-source community and the many talented developers who have contributed to the libraries we utilized.</p>
        <a href="/" class="button">Go Back to Main Page</a>
   
      
      <!-- OTA Update Link -->
      <div class="row mb-3">
        <div class="col text-center">
          <a href="/update" class="button" title="Click here to upload a new firmware .bin file">OTA Update</a>
        </div>
      </div>
    </div>
    </body>
    </html>
    )rawliteral";

    request->send(200, "text/html", html);
  });

  // Route to serve the slideshow page
  server.on("/slideshow", HTTP_GET, [](AsyncWebServerRequest *request) {
    String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <title>Slideshow</title>
      <style>
        body {
          margin: 0;
          padding: 0;
          font-family: Arial, sans-serif;
          background-color: #f0f0f0;
        }
        #sidebar {
          position: fixed;
          left: 0;
          top: 0;
          width: 200px;
          height: 100%;
          background-color: #333;
          color: white;
          padding-top: 20px;
          box-sizing: border-box;
        }
        #sidebar button {
          display: block;
          width: 160px;
          margin: 20px auto;
          padding: 15px;
          font-size: 16px;
          cursor: pointer;
          text-align: center;
          text-decoration: none;
          outline: none;
          color: #fff;
          background-color: #4CAF50;
          border: none;
          border-radius: 15px;
        }
        #sidebar button:hover {background-color: #3e8e41}
        #sidebar button:active {
          background-color: #3e8e41;
          box-shadow: 0 3px #666;
          transform: translateY(2px);
        }
        #main-content {
          margin-left: 200px;
          padding: 0;
          text-align: center;
        }
        #main-content img {
          max-width: 100%;
          height: auto;
        }
      </style>
    </head>
    <body>
      <div id="sidebar">
        <button onclick="location.href='/'">Go Back to Main Page</button>
      </div>
      <div id="main-content">
        <img id="slideshow" src="/current_image">
      </div>


<script>
  var gateway = `ws://${window.location.hostname}/ws`;
  var websocket;
  
  window.addEventListener('load', onLoad);
  window.addEventListener('beforeunload', function() {
    if (websocket) {
      websocket.close();
    }
  });
  
  function onLoad(event) {
    initWebSocket();
    // Load initial image
    updateImage();
  }
  
  function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen = function(event) {
      console.log('Connection opened');
    };
    websocket.onclose = function(event) {
      console.log('Connection closed');
      // Try to reconnect after a delay
      setTimeout(initWebSocket, 2000);
    };
    websocket.onmessage = function(event) {
      console.log('WebSocket message received:', event.data);
      if (event.data === 'update') {
        updateImage();
      }
    };
  }
  
  function updateImage() {
    console.log('Updating image...');
    var img = document.getElementById('slideshow');
    // Add a cache-busting parameter to force a fresh load
    img.src = '/current_image?nocache=' + new Date().getTime();
  }
</script>
    </body>
    </html>
    )rawliteral";
    request->send(200, "text/html", html);
  });

  // Route to serve the current image - FIXED VERSION
server.on("/current_image", HTTP_GET, [](AsyncWebServerRequest *request) {
  if (currentImageName == "") {
    request->send(404, "text/plain", "No image");
    return;
  }

  // Store the current image name at request time
  String requestImageName = currentImageName;
  
  // Create a new scope for the SPI mutex to check file exists
  {
    // Take the mutex just to check if the file exists and get its size
    xSemaphoreTake(xSpiMutex, portMAX_DELAY);
    
    SdBaseFile *file = new SdBaseFile();
    if (!file->open(requestImageName.c_str(), O_RDONLY)) {
      Serial.println("Failed to open image file for initial check");
      xSemaphoreGive(xSpiMutex);  // Release the mutex on error
      delete file;
      request->send(404, "text/plain", "Image not found");
      return;
    }
    
    // Get the file size before setting up the chunked response
    uint32_t fileSize = file->fileSize();
    file->close();
    delete file;
    
    xSemaphoreGive(xSpiMutex);  // Release the mutex after checking file exists
    Serial.println("Image file exists, setting up chunked response");
  }

  // Create a self-contained response handler that manages its own mutex
  // Pass the current image name by value to the lambda
  AsyncWebServerResponse *response = request->beginChunkedResponse("image/jpeg",
    [requestImageName](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      // Each chunk operation takes and releases the mutex
      xSemaphoreTake(xSpiMutex, portMAX_DELAY);
      
      // Open the file for each chunk to avoid keeping file handles between chunks
      SdBaseFile file;
      if (!file.open(requestImageName.c_str(), O_RDONLY)) {
        Serial.println("Failed to open image file during chunked read");
        xSemaphoreGive(xSpiMutex);
        return 0;  // End of file or error
      }
      
      // Seek to the current position
      if (index > 0) {
        file.seekSet(index);
      }
      
      // Read the data
      size_t bytesRead = file.read(buffer, maxLen);
      
      // Close file and release mutex
      file.close();
      xSemaphoreGive(xSpiMutex);
      
      return bytesRead;
    });

  response->addHeader("Content-Disposition", "inline; filename=" + requestImageName);
  response->addHeader("Access-Control-Allow-Origin", "*");

  request->send(response);
});


  // Initialize WebSocket
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);

  // Catch-all route for undefined URLs (404 error page)
  server.onNotFound([](AsyncWebServerRequest *request){
      request->send(404, "text/html", "<h3>404 - Page Not Found</h3>");
  });
  
server.serveStatic("/favicon.ico", SPIFFS, "/favicon.ico").setCacheControl("max-age=86400");
  // Start the web server
  Serial.println("Starting web server...");
  server.begin();
  Serial.println("Web server started.");
}

// QR Code display function
void displayQRCode(String ip) {
  // Remove port number - use default port 80
  String url = "http://" + ip;

  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(4)];
  qrcode_initText(&qrcode, qrcodeData, 4, ECC_MEDIUM, url.c_str());

  int blockSize = 6;  // Adjust the size of the blocks
  int qrSize = qrcode.size;
  int startX = (tft.width() - qrSize * blockSize) / 2;
  int startY = (tft.height() - qrSize * blockSize) / 2;

  tft.fillScreen(TFT_BLACK);
  for (int y = 0; y < qrSize; y++) {
    for (int x = 0; x < qrSize; x++) {
      tft.fillRect(startX + x * blockSize, startY + y * blockSize, blockSize, blockSize,
                   qrcode_getModule(&qrcode, x, y) ? TFT_BLACK : TFT_WHITE);
    }
  }
}

// Function to show Wi-Fi information, then QR code
void displayMessageAndQRCode(String ip) {
  // Append :8080 to the IP address for the web server
 // String fullIP = ip + ":8080";

  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);

  // Reformat the layout to better fit a 320x240 display
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(20, 20);  // Adjust the starting position for centering
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);

  // Display instructions in a clear, centered format
  tft.println("Use your browser");
  tft.setCursor(20, 40);  // Move down for the next instruction
  tft.println("to connect to:");

  tft.setTextColor(TFT_CYAN);  // Highlight the IP address in a different color
  tft.setCursor(20, 60);  
  tft.printf("%s\n", ip.c_str());  // Display IP without port

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(20, 90);  // Leave space before mDNS info
  tft.println("Or use");

  tft.setTextColor(TFT_CYAN);  // Highlight the mDNS address
  tft.setCursor(20, 120);
  tft.printf("%s\n", "photoframe.local");  // mDNS Hostname without Port

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(20, 160);  // Space before final instruction
  tft.println("Or scan the QR code");

  tft.setCursor(20, 180);
  tft.println("to open the website");

  tft.setCursor(20, 200);  // Add more detail
  tft.println("to manage your frame.");

  // Reserve space for the countdown timer
  tft.setTextColor(TFT_WHITE);
  tft.fillRect(0, 220, tft.width(), 40, TFT_BLACK);  // Clear space for countdown area
  tft.setCursor(20, 220);

  // Countdown timer for QR code display
  for (int countdown = 10; countdown > 0; countdown--) {
      tft.fillRect(0, 220, tft.width(), 40, TFT_BLACK);  // Clear countdown area
      tft.setCursor(20, 220);
      tft.printf("QR Code in: %d seconds", countdown);
      delay(1000);
  }

    displayQRCode(ip);  // Show QR code with IP 
    delay(5000);  // Show QR code for 5 seconds
}

void setup() {
  Serial.begin(115200);
 

  pinMode(0, INPUT);
  attachInterrupt(0, buttonInt, FALLING);
  pinMode(4, OUTPUT); digitalWrite(4, HIGH);
  pinMode(16, OUTPUT); digitalWrite(16, HIGH);
  pinMode(17, OUTPUT); digitalWrite(17, HIGH);

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED);

     // Define the ILI9341_GAMMASET if it is not included in your library
#define ILI9341_GAMMASET 0x26  // Gamma curve command for ILI9341

  // Example usage in code
#ifdef ENV_CYD2B
  // Code specific to cyd2b
    // Insert the gamma curve setup here
  tft.writecommand(ILI9341_GAMMASET); // Gamma curve selected
  tft.writedata(2);
  delay(120);
  tft.writecommand(ILI9341_GAMMASET); // Gamma curve selected
  tft.writedata(1);  
#endif

tft.setTextSize(3);
tft.setSwapBytes(true);
// Set the viewport to constrain the display within 320x240 resolution
tft.setViewport(0, 0, 320, 240);

// Initialize SPIFFS (removed duplicate initialization)
if (!SPIFFS.begin(true)) {  // 'true' will format the partition if SPIFFS is not initialized
  Serial.println("SPIFFS Mount Failed");
  return;  // Stop further execution if SPIFFS mount fails
}

// List contents
Serial.println("Listing files in SPIFFS:");
File spiffsRoot = SPIFFS.open("/");
if (!spiffsRoot || !spiffsRoot.isDirectory()) {
  Serial.println("Failed to open SPIFFS root directory.");
} else {
  File file = spiffsRoot.openNextFile();
  while (file) {
    Serial.printf("  %s (%d bytes)\n", file.name(), file.size());
    file = spiffsRoot.openNextFile();
  }
}

// Replace the existing vanity.jpg loading code with:
// Try to open and display vanity.jpg using the proper File-specific JPEGDEC methods
if (SPIFFS.exists("/vanity.jpg")) {
  tft.fillScreen(TFT_BLACK);
  
  if (jpeg.open("/vanity.jpg", spiffsOpen, spiffsClose, spiffsRead, spiffsSeek, JPEGDraw)) {
    Serial.println("Decoding vanity.jpg from SPIFFS...");
    jpeg.decode(0, 0, 0);
    jpeg.close();
    Serial.println("Successfully displayed vanity.jpg from SPIFFS.");
  } else {
    Serial.println("Failed to decode vanity.jpg with JPEGDEC.");
    // Display error message on screen
    tft.setCursor(20, 100);
    tft.setTextColor(TFT_RED);
    tft.println("Failed to load");
    tft.setCursor(20, 130);
    tft.println("splash image");
  }
} else {
  Serial.println("vanity.jpg not found in SPIFFS. Continuing without it.");
}




    delay(10000);  // Wait for 10 seconds

    tft.fillScreen(TFT_BLACK);  // Clear the screen after 10 seconds

  // Title starts higher up
  tft.setCursor(20, 0); 
  tft.println("CYD PhotoFrame ");

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);

  // Adjusted vertical positions to fit properly
  tft.setCursor(20, 40);
  tft.println("If this stays on your");
  tft.setCursor(20, 60);
  tft.println("screen for more than 15 ");
  tft.setCursor(20, 80);
  tft.println("seconds, follow the");
  tft.setCursor(20, 100);
  tft.println("instructions below");
  tft.println("");  

  // Highlight WiFi and IP instructions
  tft.setTextColor(TFT_GREEN);
  tft.setCursor(20, 120);
  tft.println("Connect your wifi to:");
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(20, 140);
  tft.println("ESP32_AP");
  tft.setTextColor(TFT_GREEN);
  tft.setCursor(20, 160);
  tft.println("And use browser to open");
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(20, 180);
  tft.println("192.168.4.1");
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(20, 200);
  tft.println("to configure WiFi");
  tft.setCursor(20, 220);
  tft.println("Then enjoy PhotoFrame");

    delay(1000);
    ts.begin();
    xSpiMutex = xSemaphoreCreateMutex();

    // Increase the Task Watchdog Timer to prevent resets
    esp_task_wdt_init(30, true);  // Set watchdog timeout to 30 seconds

    // Initialize I2S output using internal DAC
    out = new AudioOutputI2S(0, AudioOutputI2S::INTERNAL_DAC);  // Use internal DAC on GPIO26
    out->SetOutputModeMono(true);    // Mono output
    out->SetGain(0.5);               // Set volume (0.0 to 1.0)
    out->SetRate(44100);             // Set sample rate to match your WAV file

    // Connect to WiFi using WiFiManager
    if (!wm.autoConnect("ESP32_AP")) {
      Serial.println("Failed to connect to WiFi. Starting AP mode...");
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(0, 0);
      tft.setTextColor(TFT_WHITE);
      tft.setTextSize(3);
      tft.println("WiFi AP Mode");
      while (true) delay(1000);
    }

    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.println("Waiting for WiFi...");
    }
    if (!MDNS.begin("photoframe")) {
    Serial.println("Error starting mDNS");
  } else {
    Serial.println("mDNS started: photoframe.local");
  }
    // Set up OTA update page using ElegantOTA
  ElegantOTA.begin(&server);
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);

  ElegantOTA.onStart([]() {
    Serial.println("OTA update started");
  });
  ElegantOTA.onEnd([](bool success) {
    Serial.println("OTA update finished. Rebooting...");
    if (success) {
      Serial.println("Update successful");
    } else {
      Serial.println("Update failed");
    }
    delay(1000);
  });

    // Now that Wi-Fi is connected, set up the web server
    setupWebServer();

    // Display IP address
    String ip = WiFi.localIP().toString();
    if (ip == "0.0.0.0") {
      Serial.println("IP address not assigned, retrying...");
      // Wait for IP to be assigned
      int retries = 0;
      while (ip == "0.0.0.0" && retries < 10) {
        delay(1000);
        ip = WiFi.localIP().toString();
        Serial.println("Waiting for IP address...");
        retries++;
      }
      if (ip == "0.0.0.0") {
        Serial.println("Failed to obtain IP address.");
        // Handle failure (e.g., restart or enter AP mode)
        return;
      }
    }

    Serial.printf("Assigned IP: %s\n", ip.c_str());
    displayMessageAndQRCode(ip);

    // Initialize SD card
if (!checkAndMountSDCard()) {
  error("SD Card Mount Failed");
} else {
  sdRoot = sd.open("/");
  if (!sdRoot) {
    error("Failed to open SD root directory.");
  } else {
    fileCount = 0;
    SdBaseFile fileEntry;
    char name[100];
    while (fileEntry.openNext(&sdRoot, O_RDONLY)) {
      fileEntry.getName(name, sizeof(name));
      if (strcasecmp(name + strlen(name) - 3, "JPG") == 0) {
        fileCount++;
      }
      fileEntry.close();
    }
    Serial.printf("Found %d images.\n", fileCount);
  }
}

if (fileCount == 0) error("No .JPG images found");
currentIndex = 0;
loadImage(currentIndex);
}


void loop() {
  if (fileCount > 0) {
    if ((millis() - timer > X * 1000) || buttonPressed) {
      currentIndex = (currentIndex + 1) % fileCount;
      loadImage(currentIndex);
      timer = millis();
      buttonPressed = false;
    }
  }
 ElegantOTA.loop();
  ws.cleanupClients();  // Clean up WebSocket clients


  delay(10); // Yield to other tasks
}
