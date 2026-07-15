# DeskNest

> A small desktop display that rests when turned face down.

[中文说明](README.md) · Status: **V1.0 close-out / Release Candidate**

DeskNest is a 240×320 portrait-first desktop assistant for the DFRobot UNIHIKER K10. It combines AI usage, environment data, a browser-managed what2eat list, persistent device settings, and a face-down rest state.

## V1.0 features

- ChatGPT/Codex and MiniMax usage windows through the local TokenNest service;
- temperature, humidity, light, and comfort hints integrated into the overview;
- versioned what2eat draft, publish, board cache, ACK, and `B pick` workflow;
- persistent home-focus, AI-alert, and power settings;
- gesture/button navigation and face-down sleep with page restoration;
- one production LVGL renderer consuming the `UiModel` boundary.

Runtime portrait/landscape switching is intentionally outside V1.0.

## Requirements

- DFRobot UNIHIKER K10;
- Windows PowerShell and PlatformIO;
- Node.js `>=18.17.0` for TokenNest;
- a sibling CNFontNest checkout, or `CNFONTNEST_ROOT` pointing to it;
- `lv_font_conv 1.5.3`.

## Quick start

Start TokenNest:

```powershell
cd C:\path\to\DeskNest\TokenNest
npm install
Copy-Item config\minimax.example.json config\minimax.json
Copy-Item config\tokennest.example.yaml config\tokennest.yaml
Copy-Item .env.example .env
npm test
npm start
```

Create the ignored firmware configuration:

```powershell
cd C:\path\to\DeskNest
Copy-Item platformio.local.ini.example platformio.local.ini
```

Set `wifi_ssid`, `wifi_pass`, and `tokennest_url`. The board must use the PC's LAN IP, not `127.0.0.1`.

Build, upload, and monitor:

```powershell
C:\Users\DF\.platformio\penv\Scripts\pio.exe run -e DeskNest
C:\Users\DF\.platformio\penv\Scripts\pio.exe run -e DeskNest -t upload
C:\Users\DF\.platformio\penv\Scripts\pio.exe device monitor -b 115200
```

The supported V1.0 release path is PlatformIO. Arduino IDE, Mind+, and the current PC simulator are not release acceptance targets.

## Default controls

| Input | Action |
| --- | --- |
| Hold A and shake left/right | Previous/next page |
| Short A | Toggle the hold-A gesture confirmation gate |
| Hold B for about 1 second | Return to overview |
| Short B on what2eat | Pick another item |
| A/B on Settings | Select row / change and persist value |
| Turn face down / face up | Rest / wake and restore the previous page |

## Documentation

- [Chinese user guide](docs/USER_GUIDE.md)
- [Current architecture](docs/ARCHITECTURE.md)
- [V1.0 release checklist](docs/V1_RELEASE_CHECKLIST.md)
- [TokenNest](TokenNest/README.md)
- [Image-generation prompts](docs/IMAGE_PROMPTS.md)

## Boundaries

- TokenNest is designed for a trusted local network and must not be exposed directly to the public internet.
- Dynamic what2eat text supports a bounded character set, not arbitrary Unicode or emoji.
- Build and host-test success do not prove K10 visual or physical-input behavior.
- The repository does not yet contain a root open-source license file; decide and add one before a public release.
