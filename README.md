# CrossPoint Apps


**CrossPoint Apps** is a community-driven fork of the original CrossPoint Reader project. While the upstream project focuses solely on e-reading, this fork expands the capabilities of the Xteink X4 device by supporting a robust ecosystem of **apps and utilities**. 

Our goal is to make the device more useful in your day-to-day life without compromising its battery life, stability, or its core mission as an distraction-free e-ink reader.

**Now running on:** ESP32C3-based Xteink [X4](https://www.xteink.com/products/xteink-x4) and [X3](https://www.xteink.com/products/xteink-x3).

![CrossPoint Reader running on Xteink device](./docs/images/apps/home-screen.jpg)

## Features

In addition to all the fantastic EPUB rendering, custom fonts, and library management features from the upstream CrossPoint project, **CrossPoint Apps** includes a growing suite of applications and technical capabilities:

- **Markdown & HTML Parser**: Features a custom parser and renderer that gracefully strips HTML tags and translates basic Markdown, allowing web content (like Wikipedia and Reddit) to be displayed elegantly in the native text reader engine.
- **Calculator**: A fully functional, e-ink optimized calculator for quick math.
- **Weather**: View local forecasts. Fetches data when connected to Wi-Fi and caches it locally so you can check the weather even when offline.
- **Sudoku**: Play randomly generated Sudoku puzzles. Fully playable offline.
- **Wikipedia**: Search for topics and download complete, text-only Wikipedia articles to your SD card for offline reference.
- **Chess**: A fully featured chess engine. Play against the computer directly on your e-reader.
- **Dice & 8-Ball**: A handy utility for tabletop gamers. Roll D6, D20, spin arrows, flip coins, or consult the Magic 8-Ball.
- **RSS Feed & Reddit**: Subscribe to your favorite blogs and news sites. Articles are downloaded and cached for distraction-free, offline reading using the native text reader engine.
- **DuckDuckGo**: Search the web using DuckDuckGo. Results are displayed in a text-heavy format optimized for e-ink displays.


## Gallery
<table>
  <tr>
    <td><img src="./docs/images/apps/weather.png" alt="Weather" width="60"></td>
    <td><img src="./docs/images/apps/rss.png" alt="RSS" width="60"></td>
    <td><img src="./docs/images/apps/reddit.png" alt="Reddit" width="60"></td>
    <td><img src="./docs/images/apps/duckduckgo.png" alt="DDG" width="60"></td>
    <td><img src="./docs/images/apps/markdown.png" alt="Markdown" width="60"></td>
  </tr>
  <tr>
    <td><img src="./docs/images/apps/html.png" alt="HTML" width="60"></td>
    <td><img src="./docs/images/apps/calculator.png" alt="Calculator" width="60"></td>
    <td><img src="./docs/images/apps/chess.png" alt="Chess" width="60"></td>
    <td><img src="./docs/images/apps/dice.png" alt="Dice" width="60"></td>
    <td><img src="./docs/images/apps/sudoku.png" alt="Sudoku" width="60"></td>
  </tr>
</table>


## Install Firmware

### Web Installer

1. Download the pre-compiled binary from this repository: [`bin/crosspoint-apps.bin`](./bin/crosspoint-apps.bin).
2. Connect your device to your computer via USB-C and wake/unlock the device.
3. Go to the [Web Flasher](https://crosspointreader.com/#flash-tools), select your device (X3 or X4).
4. Click "Custom .bin" and upload the `crosspoint-apps.bin` file you just downloaded.

### Development Quick Start / Command Line

1. Clone this repository:
```bash
git clone --recursive https://github.com/zakerytclarke/crosspoint-reader-apps.git
cd crosspoint-reader-apps
```

2. Install PlatformIO (if you haven't already).
3. Connect your device via USB-C.
4. Build and flash the firmware:
```bash
pio run --target upload
```

## Community Contributions

We enthusiastically welcome contributions! If you have an idea for an app that would be useful on an e-ink device, we want it. 

We strongly encourage apps that are **offline-first**—meaning they do not require a constant Wi-Fi connection to function. Use the SD card to cache data (like Reddit posts or RSS feeds) so users can refresh their feeds once and read them anywhere.

See [CLAUDE.md](./CLAUDE.md) for detailed developer guidelines on how to build apps that respect the constraints of the ESP32-C3 and the e-ink display.

## Documentation

- [User Guide](./USER_GUIDE.md) - Learn how to use the device and the new apps.
- [Project Scope](./SCOPE.md) - Understand our philosophy on what makes a good app.
- [Developer Guidelines](./CLAUDE.md) - Read this before contributing code or new apps!

---

CrossPoint Apps is a community fork and is **not affiliated with Xteink or any device manufacturer**.
