## HTTP API Endpoints

This device exposes a simple HTTP API served from the built-in web server. All endpoints use HTTP GET and return JSON responses (or HTML for the root). Use your browser or curl to interact with them.

Base URL: http://<device-hostname-or-ip>/

List of endpoints:

- `GET /` — Returns the web UI (HTML page) with controls for the strips and animations.

- Regular (non-addressable) dim strip (PWM, MOSFET on GPIO 4):
  - `GET /api/dim/on` — Turn PWM strip on (uses stored brightness).
    - Response: {"ok":true}
  - `GET /api/dim/off` — Turn PWM strip off (PWM duty = 0).
    - Response: {"ok":true}
  - `GET /api/dim/brightness?b=<0-255>` — Set stored brightness and apply when strip is on.
    - Query: `b` (0..255)
    - Response: {"ok":true,"brightness":<value>}

- Addressable strip #1 (WS2812 on GPIO 17):
  - `GET /api/ws1/on` — Turn strip on (uses stored color & brightness).
  - `GET /api/ws1/off` — Turn strip off (clears pixels).
  - `GET /api/ws1/set?b=<0-255>&r=<0-255>&g=<0-255>&b2=<0-255>` — Set brightness and RGB color.
    - Note: query parameter for blue is named `b2` to avoid collision with brightness `b`.
    - Response: {"ok":true}

- Addressable strip #2 (WS2812 on GPIO 18):
  - `GET /api/ws2/on` — Turn strip on.
  - `GET /api/ws2/off` — Turn strip off.
  - `GET /api/ws2/set?b=<0-255>&r=<0-255>&g=<0-255>&b2=<0-255>` — Set brightness and RGB color for strip #2.
    - Response: {"ok":true}

- Global convenience:
  - `GET /api/onall` — Turn PWM strip and both addressable strips on (apply stored values).
  - `GET /api/offall` — Turn everything off.

- Animations:
  - `GET /api/anim/start?name=<sunrise|sunset|waves>&dur=<ms>` — Start an animation.
    - `name` (required): one of `sunrise`, `sunset`, or `waves`.
    - `dur` (optional): duration in milliseconds. If omitted for `sunrise`/`sunset`, the server defaults to 20 minutes (1,200,000 ms). If omitted for `waves`, a short default (30s) is used.
    - Response: {"ok":true}
    - Examples:
      - `GET /api/anim/start?name=sunrise` — Start sunrise for default 20 minutes.
      - `GET /api/anim/start?name=sunset&dur=600000` — Start 10-minute sunset.
  - `GET /api/anim/stop` — Stop any running animation and return to manual controls.

Notes and tips:
- Use the root web UI for quick interactive control from a browser.
- Blue channel query parameter is named `b2` to avoid conflict with brightness `b` in the same query string.
- Animations are non-blocking and progress is handled in the main loop; calling `anim/stop` will immediately stop them.

```mermaid
graph LR
  %% Layout
  %% Use subgraphs to mimic modules/parts
  subgraph PSU["5V PSU"]
    P5V["+5V"]
    PGND["GND"]
  end

  subgraph ESP["ESP32 Dev Board"]
    VIN["VIN (5V in)"]
    GND_E["GND"]
    V3V3["3V3"]
    GPIO4["GPIO4"]
    D16["GPIO16 (D16)"]
    D18["GPIO18 (D18)"]
  end

  subgraph MOSFET["N-MOSFET (low-side switch)"]
    Gate["Gate"]
    Drain["Drain"]
    Source["Source"]
  end

  R1["R1 220 Ω (gate series)"]
  R2["R2 10 kΩ (gate pull-down)"]

  subgraph LS["Level Shifter (bidirectional)"]
    HV["HV (5V)"]
    LV["LV (3.3V)"]
    GND_LS["GND"]
    H1["H1 → to DIN #1"]
    H2["H2 → to DIN #2"]
    L1["L1 ← from ESP D16"]
    L2["L2 ← from ESP D18"]
  end

  %% Non-addressable 5V LED strip driven by MOSFET
  subgraph DUMB["Non-addressable LED Strip (5V, dim via MOSFET)"]
    DUMB_PLUS["+5V"]
    DUMB_MINUS["GND (−)"]
  end

  %% Addressable strips (e.g., WS2812/NEOPIXEL)
  subgraph S1["Addressable LED Strip #1 (WS2812)"]
    S1_5V["+5V"]
    S1_GND["GND"]
    S1_DIN["DIN"]
  end

  subgraph S2["Addressable LED Strip #2 (WS2812)"]
    S2_5V["+5V"]
    S2_GND["GND"]
    S2_DIN["DIN"]
  end

  %% Power connections
  P5V --> VIN
  P5V --> DUMB_PLUS
  P5V --> S1_5V
  P5V --> S2_5V

  %% Common grounds
  PGND --> Source
  PGND --> GND_E
  PGND --> GND_LS
  PGND --> S1_GND
  PGND --> S2_GND

  %% MOSFET wiring for non-addressable strip
  DUMB_MINUS --> Drain
  GPIO4 --> R1 --> Gate
  Gate --- R2 --- Source

  %% Level shifter power & reference
  P5V --> HV
  V3V3 --> LV
  PGND --> GND_LS

  %% Data paths (3.3V -> 5V)
  D16 --> L1
  D18 --> L2
  H1 --> S1_DIN
  H2 --> S2_DIN
```