# Pixlpal Apps

The catalogue of apps for [Pixlpal](https://github.com/Meterbit), the 256 × 128 pixel desk display, and everything
you need to make one.

A Pixlpal app is a small program written in C. Pixlpal downloads it and runs it in a sealed space: it can draw on
the screen, read the knob, a USB keyboard and mouse, play sounds, keep 1 KB of saved data, and show a page in the
Pixlpal phone app. It can't reach anything else on the device, such as its Wi-Fi password, files or other apps.

## For Pixlpal owners

Open the Pixlpal phone app, go to **Maker & Developer**, then **App Store**. Pick an app and tap **Install**.
Pixlpal downloads it over its own Wi-Fi and checks it. It then appears under **Installed on Pixlpal**.

Every app here was reviewed and signed by Meterbit. Pixlpal installs only signed apps, and checks the signature
again every time an app opens. If an app turns out to be harmful, Meterbit withdraws it, and Pixlpals stop opening
it.

To report an app that misbehaves, [open an issue](../../issues/new?template=report-an-app.md).

## For developers

- **[The developer guide](docs/GUIDE.md):** setting up, your first app, the full API, the phone page, testing on
  your own Pixlpal.
- **[Contributing](CONTRIBUTING.md):** sharing your app with every Pixlpal owner through this catalogue.
- **[examples/hello](examples/hello):** the smallest app, about 50 lines.
- **[apps/](apps):** the catalogue's apps, all with their source code.

You can make apps for your own Pixlpal without ever submitting them: Developer Mode lets your Pixlpal run them.

## What's in this repository

| Folder | What it holds |
|---|---|
| `sdk/pixlpal.h` | The Pixlpal App API: the only header an app includes |
| `tools/` | `build.py` (build and pack an app), `pxa.py` (packages), `wasm_info.py` (inspect a module), `publish.py` (the catalogue) |
| `examples/` | Example apps, not in the catalogue |
| `apps/<id>/` | Each catalogue app: `app.json`, its `.c` files, `icon.png` |
| `catalogue/` | What Pixlpal and the phone app read, made by `tools/publish.py`: the signed index, the packages, icons, the withdrawn list, and Meterbit's public key |

## Licence

Everything in this repository is under the [MIT licence](LICENSE), including every app submitted to it. Contributors
keep the copyright to their apps.
