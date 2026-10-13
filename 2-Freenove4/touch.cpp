// FT6336U capacitive touch (I2C). The controller reports portrait coordinates
// (x 0..319, y 0..479); the screen position depends on the rotation, see touchReadLocked().
//
// The FT6336U library is only used for its reset/init sequence. Its scan() puts a
// 10 ms delay inside every register read (~100 ms per touch read), which makes quick
// taps invisible to a poller, so touches are read here with a single 5-byte burst.

#include <Wire.h>
#include "FT6336U.h"
#include "app.h"

#define FT_ADDR 0x38
#define FT_REG_TD_STATUS 0x02  // then P1_XH, P1_XL, P1_YH, P1_YL

static FT6336U ctp(I2C_SDA, I2C_SCL, TOUCH_RST, TOUCH_INT);

static bool touchReadLocked(int &x, int &y);

// The touch chip and the audio codec share this I2C bus; touch polling runs in the
// main loop and codec set-up in the audio task, so transactions are serialised.
static SemaphoreHandle_t i2cMutex = nullptr;

bool i2cLock(uint32_t timeoutMs) {
  if (!i2cMutex) return false;
  return xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void i2cUnlock() {
  xSemaphoreGive(i2cMutex);
}

void touchBegin() {
  if (!i2cMutex) i2cMutex = xSemaphoreCreateMutex();
  ctp.begin();
  Wire.setClock(400000);
}

bool touchRead(int &x, int &y) {
  if (!i2cLock(5)) return false;
  bool ok = touchReadLocked(x, y);
  i2cUnlock();
  return ok;
}

static bool touchReadLocked(int &x, int &y) {
  Wire.beginTransmission(FT_ADDR);
  Wire.write(FT_REG_TD_STATUS);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)FT_ADDR, (uint8_t)5) != 5) return false;
  uint8_t status = Wire.read() & 0x0F;
  uint8_t xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  if (status == 0 || status > 2) return false;
  int rawX = ((xh & 0x0F) << 8) | xl;
  int rawY = ((yh & 0x0F) << 8) | yl;
  // raw is the panel's portrait frame (x 0..319, y 0..479). Verified by eye in the two landscape
  // rotations; the portrait ones follow from the same MADCTL flags (to be confirmed on the board).
  switch (tft.getRotation() & 3) {
    case 3: x = 479 - rawY; y = rawX; break;         // USB on the left (setting 0)
    case 0: x = rawX; y = rawY; break;               // portrait (setting 1)
    case 1: x = rawY; y = 319 - rawX; break;         // USB on the right (setting 2)
    default: x = 319 - rawX; y = 479 - rawY; break;  // portrait, turned over (setting 3)
  }
  x = constrain(x, 0, tft.width() - 1);
  y = constrain(y, 0, tft.height() - 1);
  return true;
}
