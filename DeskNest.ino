// DeskNest.ino - Arduino IDE entry point
// 栖于桌面，息于常亮之间
//
// Thin wrapper: includes src/app.h and forwards to dn_app_setup/dn_app_loop.
// All real code lives under src/.
//
// Compatible with:
//   - Arduino IDE (open this file directly)
//   - PlatformIO (src_dir = . in platformio.ini)
//   - Mind+ via mindplus_assets/UhlCore/ wrapper

#include "src/app.h"

void setup() {
    dn_app_setup();
}

void loop() {
    dn_app_loop();
}
