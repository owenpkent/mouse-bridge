// mouse_bridge.ino
//
// Bluetooth Classic mouse -> wired USB mouse bridge for the Raspberry Pi Pico 2 W
// (or Pico W). The Pico pairs with the mouse and keeps the pairing, so the bridge
// can move between computers and each one just sees an ordinary USB mouse.
//
// Core:  arduino-pico (Earle Philhower), https://github.com/earlephilhower/arduino-pico
// Tools: Board              = Raspberry Pi Pico 2 W (or Pico W)
//        IP/Bluetooth Stack = IPv4 + Bluetooth
//        USB Stack          = Pico SDK
//
// LED:   fast blink = pairing, slow blink = reconnecting, solid = connected.
// Hold BOOTSEL for 3 s to forget the mouse and reboot into pairing mode.

#include <BluetoothHIDMaster.h>
#include <EEPROM.h>
#include <Mouse.h>

// ---------------------------------------------------------------- config ----

// Set to 1 if left-click registers as right-click (library numbers buttons from 1).
#define BUTTON_INDEX_BASE 0
// Set to 1 if the scroll wheel runs backwards.
#define WHEEL_INVERT 0
// Set to 1 for serial logging at 115200 baud.
#define DEBUG 0

static const int SCAN_SECONDS = 5;
static const uint32_t PAIR_CONNECT_TIMEOUT_MS = 30000;  // includes SSP pairing
static const uint32_t RECONNECT_TIMEOUT_MS = 6000;      // per attempt
static const uint32_t RECONNECT_PAUSE_MS = 1000;        // between attempts
static const uint32_t FORGET_HOLD_MS = 3000;
static const uint32_t FAST_BLINK_MS = 100;
static const uint32_t SLOW_BLINK_MS = 600;

// ----------------------------------------------------------------------------

#if DEBUG
#define LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LOG(...) \
  do {           \
  } while (0)
#endif

BluetoothHIDMaster hid;

enum class State { Pairing, Reconnecting, Connected };
static State state = State::Pairing;

static uint8_t savedAddr[6];

// Written by the BT callbacks (async context), read by loop() under noInterrupts().
static volatile int32_t pendX = 0, pendY = 0, pendW = 0;
static volatile uint8_t btButtons = 0;  // bit0 = left, bit1 = right, bit2 = middle, ...
static uint8_t usbButtons = 0;          // what the USB host currently sees

// ------------------------------------------------------ flash storage --------

static const uint32_t STORE_MAGIC = 0x4D423031;  // "MB01"

static bool loadAddr(uint8_t out[6]) {
  uint32_t magic = 0;
  EEPROM.get(0, magic);
  if (magic != STORE_MAGIC) return false;
  for (int i = 0; i < 6; i++) out[i] = EEPROM.read(4 + i);
  return true;
}

static void saveAddr(const uint8_t addr[6]) {
  EEPROM.put(0, STORE_MAGIC);
  for (int i = 0; i < 6; i++) EEPROM.write(4 + i, addr[i]);
  EEPROM.commit();
}

static void eraseAddr() {
  EEPROM.put(0, (uint32_t)0xFFFFFFFF);
  EEPROM.commit();
}

// ------------------------------------------------------------ BT callbacks --

static void onMouseMove(void *, int dx, int dy, int dw) {
  pendX += dx;
  pendY += dy;
  pendW += dw;
}

static void onMouseButton(void *, int butNo, bool down) {
  int idx = butNo - BUTTON_INDEX_BASE;
  if (idx < 0 || idx > 4) return;  // USB boot mouse has 5 buttons
  uint8_t mask = 1u << idx;
  if (down) {
    btButtons |= mask;
  } else {
    btButtons &= ~mask;
  }
}

// --------------------------------------------------------- LED and button --

static void ledUpdate() {
  static bool on = false;
  bool want;
  uint32_t now = millis();
  switch (state) {
    case State::Connected:
      want = true;
      break;
    case State::Reconnecting:
      want = (now / SLOW_BLINK_MS) & 1;
      break;
    default:
      want = (now / FAST_BLINK_MS) & 1;
      break;
  }
  if (want != on) {  // the Pico W LED sits on the radio chip; avoid needless writes
    on = want;
    digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
  }
}

// Hold BOOTSEL for FORGET_HOLD_MS: drop the mouse and reboot into pairing mode.
static void checkForget() {
  static uint32_t lastPoll = 0;
  static uint32_t pressedSince = 0;
  uint32_t now = millis();
  if (now - lastPoll < 50) return;  // reading BOOTSEL briefly stalls flash; poll gently
  lastPoll = now;
  if (!BOOTSEL) {
    pressedSince = 0;
    return;
  }
  if (pressedSince == 0) pressedSince = now ? now : 1;
  if (now - pressedSince < FORGET_HOLD_MS) return;

  LOG("BOOTSEL held: forgetting mouse\n");
  for (int i = 0; i < 6; i++) {  // acknowledge with a quick flutter
    digitalWrite(LED_BUILTIN, i & 1);
    delay(80);
  }
  Mouse.release(0x1F);  // all five buttons
  hid.disconnect();
  hid.clearPairing();
  eraseAddr();
  rp2040.reboot();
}

// Waits for an issued connect() to finish while keeping the LED and BOOTSEL alive.
static bool waitConnected(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (hid.connected()) return true;
    ledUpdate();
    checkForget();
    delay(10);
  }
  return hid.connected();
}

// ------------------------------------------------------------- pairing ------

// Class of Device: major class Peripheral (0x05), minor bit 0x80 = pointing device.
// Combo keyboard+pointer devices match too.
static bool isPointingDevice(uint32_t cod) {
  return (((cod >> 8) & 0x1F) == 0x05) && (cod & 0x80);
}

static void doPairing() {
  LOG("Pairing: scanning %d s (put the mouse in pairing mode)\n", SCAN_SECONDS);

  // Async scan so the LED keeps blinking while the radio listens.
  hid.scan(BluetoothHIDMaster::any_cod, SCAN_SECONDS, true);
  while (!hid.scanAsyncDone()) {
    ledUpdate();
    checkForget();
    delay(10);
  }
  auto found = hid.scanAsyncResult();

  int best = -1;
  for (size_t i = 0; i < found.size(); i++) {
    auto &d = found[i];
    bool pointer = isPointingDevice(d.deviceClass());
    LOG("  %s  cod=0x%06lx  rssi=%d  %s%s\n", d.addressString(), (unsigned long)d.deviceClass(),
        d.rssi(), d.name(), pointer ? "  <- pointing device" : "");
    if (pointer && (best < 0 || d.rssi() > found[best].rssi())) best = (int)i;
  }
  if (best < 0) {
    LOG("No pointing device found, scanning again\n");
    return;
  }

  uint8_t addr[6];
  memcpy(addr, found[best].address(), 6);
  LOG("Connecting to %s\n", found[best].addressString());
  if (!hid.connect(addr) || !waitConnected(PAIR_CONNECT_TIMEOUT_MS)) {
    LOG("Connect failed, scanning again\n");
    hid.disconnect();
    return;
  }

  memcpy(savedAddr, addr, 6);
  saveAddr(savedAddr);
  LOG("Paired and saved\n");
  state = State::Connected;
}

// ------------------------------------------------------------ reconnect -----

static void doReconnect() {
  static uint32_t lastAttempt = 0;
  uint32_t now = millis();
  if (lastAttempt != 0 && now - lastAttempt < RECONNECT_PAUSE_MS) return;

  LOG("Reconnecting to saved mouse\n");
  if (hid.connect(savedAddr) && waitConnected(RECONNECT_TIMEOUT_MS)) {
    LOG("Reconnected\n");
    state = State::Connected;
  } else {
    hid.disconnect();
  }
  lastAttempt = millis();
}

// ------------------------------------------------------------ forwarding ----

static int8_t clamp8(int32_t v) {
  return (int8_t)(v > 127 ? 127 : (v < -127 ? -127 : v));
}

static void forwardToUsb() {
  noInterrupts();
  int32_t x = pendX, y = pendY, w = pendW;
  pendX = pendY = pendW = 0;
  uint8_t b = btButtons;
  interrupts();

#if WHEEL_INVERT
  w = -w;
#endif

  // Buttons first, so a click lands where the cursor was when it happened.
  if (b != usbButtons) {
    for (int i = 0; i < 5; i++) {
      uint8_t m = 1u << i;
      if ((b & m) && !(usbButtons & m)) Mouse.press(m);
      if (!(b & m) && (usbButtons & m)) Mouse.release(m);
    }
    usbButtons = b;
  }

  // USB boot mouse reports carry 8-bit deltas; split large moves.
  while (x || y || w) {
    int8_t cx = clamp8(x), cy = clamp8(y), cw = clamp8(w);
    Mouse.move(cx, cy, cw);
    x -= cx;
    y -= cy;
    w -= cw;
  }
}

static void releaseEverything() {
  noInterrupts();
  pendX = pendY = pendW = 0;
  btButtons = 0;
  interrupts();
  if (usbButtons) Mouse.release(0x1F);  // all five buttons
  usbButtons = 0;
}

// ------------------------------------------------------------- arduino ------

void setup() {
#if DEBUG
  Serial.begin(115200);
  uint32_t t = millis();
  while (!Serial && millis() - t < 3000) delay(10);
#endif
  pinMode(LED_BUILTIN, OUTPUT);
  Mouse.begin();
  EEPROM.begin(256);

  hid.onMouseMove(onMouseMove);
  hid.onMouseButton(onMouseButton);
  hid.begin();  // Bluetooth Classic HID host

  if (loadAddr(savedAddr)) {
    LOG("Saved mouse found, reconnecting\n");
    state = State::Reconnecting;
  } else {
    LOG("No saved mouse, pairing\n");
    state = State::Pairing;
  }
}

void loop() {
  ledUpdate();
  checkForget();

  switch (state) {
    case State::Pairing:
      doPairing();
      break;
    case State::Reconnecting:
      doReconnect();
      break;
    case State::Connected:
      if (!hid.connected()) {
        LOG("Mouse disconnected\n");
        releaseEverything();
        state = State::Reconnecting;
        break;
      }
      forwardToUsb();
      break;
  }
  delay(1);
}
