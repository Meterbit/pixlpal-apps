# Making Pixlpal apps

This guide takes you from nothing to an app running on your Pixlpal, then through everything an app can do. You need
to know a little C. You don't need to know WebAssembly or anything about Pixlpal's firmware.

1. [How an app works](#how-an-app-works)
2. [Setting up, once](#setting-up-once)
3. [Your first app](#your-first-app)
4. [Running it on your Pixlpal](#running-it-on-your-pixlpal)
5. [What Pixlpal calls in your app](#what-pixlpal-calls-in-your-app)
6. [What your app can call](#what-your-app-can-call)
7. [The phone page](#the-phone-page)
8. [Limits](#limits)
9. [Writing C without a C library](#writing-c-without-a-c-library)
10. [When something goes wrong](#when-something-goes-wrong)
11. [app.json](#appjson)

## How an app works

You write the app in C. A compiler, the **WASI SDK**, turns it into **WebAssembly**: a compact program format that
runs the same on any processor. `tools/build.py` packs it with its description (`app.json`) into a **package**, an
`.pxa` file. Pixlpal downloads that file, keeps it, and runs it in its WebAssembly runtime.

An app only ever talks to Pixlpal through the **Pixlpal App API**, the functions in [`sdk/pixlpal.h`](../sdk/pixlpal.h).
Pixlpal checks every call: drawing outside the screen is clipped, and an app that reaches outside its own memory is
stopped. So an app can't harm Pixlpal, and you can experiment freely.

There are two kinds of installed app:

- **Personal apps** are what you build. They aren't signed, so Pixlpal runs them only with **Developer Mode** on. You
  turn that on at the Pixlpal itself, by holding its knob.
- **Catalogue apps** were reviewed and signed by Meterbit ([CONTRIBUTING.md](../CONTRIBUTING.md)). Every Pixlpal
  installs them, from the phone app's App Store.

## Setting up, once

You need:

- **A Pixlpal** whose firmware has **App Manager**: it's under **Maker & Developer** in the phone app.
- **The Pixlpal phone app**, connected to your Pixlpal.
- **Python 3.** macOS and Linux have it. On Windows, install it from python.org.
- **The WASI SDK, version 34.** Download it from
  [github.com/WebAssembly/wasi-sdk/releases](https://github.com/WebAssembly/wasi-sdk/releases/tag/wasi-sdk-34): the
  file for your computer, such as `wasi-sdk-34.0-arm64-macos.tar.gz` for an Apple Silicon Mac,
  `wasi-sdk-34.0-x86_64-linux.tar.gz` for Linux or `wasi-sdk-34.0-x86_64-windows.tar.gz` for Windows. Unpack it
  and rename the folder so it's at `~/.pixlpal-tools/wasi-sdk` (on Windows, `C:\Users\<you>\.pixlpal-tools\wasi-sdk`).
  Or put it anywhere and set the environment variable `WASI_SDK` to that folder.

  On a Mac, the first time you build, macOS may refuse to run the downloaded compiler. Run
  `xattr -dr com.apple.quarantine ~/.pixlpal-tools/wasi-sdk` once to allow it.
- **This repository:** `git clone https://github.com/Meterbit/pixlpal-apps.git`, or download it as a ZIP from
  GitHub.

Check it works, from the repository's folder:

```
python3 tools/build.py examples/hello
```

It should print `examples/hello/hello.pxa: 1400 bytes ... not signed (a personal app: Developer Mode)`.

## Your first app

Copy `examples/hello` to a new folder, for example `my-apps/clock`. It holds two files.

**`app.json`** describes the app:

```json
{
  "id": "io.github.yourname.clock",
  "name": "My Clock",
  "version": "1.0.0",
  "author": "Your Name",
  "api": "0.2",
  "about": "A big clock."
}
```

`id` must be unique to your app. A good choice is a domain you control, reversed: your GitHub user name works as
`io.github.yourname`. Installing a package with the same `id` again updates the app and keeps its saved data.

**The C file** is the app. Here is a complete one:

```c
#include "pixlpal.h"

static char inbox[PXL_INBOX_SIZE];
PXL_EXPORT("pxl_inbox") char* pxl_inbox(void){ return inbox; }

static int size = 1;

PXL_EXPORT("pxl_input") void pxl_input(int kind, int a, int b){
    if (kind == PXL_IN_TURN){ size += a; if (size < 0) size = 0; if (size > 2) size = 2; }
}

PXL_EXPORT("pxl_frame") void pxl_frame(uint32_t ms, uint32_t dt){
    uint32_t s = ms / 1000;
    char t[9] = "00:00:00";
    t[0] += (s / 36000) % 10; t[1] += (s / 3600) % 10;
    t[3] += (s / 600) % 6;    t[4] += (s / 60) % 10;
    t[6] += (s / 10) % 6;     t[7] += s % 10;
    pxl_fill(PXL_BLACK);
    pxl_text(PXL_W / 2, 58, size, PXL_CENTRE, PXL_RGB(0, 200, 255), PXL_BLACK, t);
}
```

It shows the time since Pixlpal started, and the knob changes the size of the text. (Pixlpal's real clock time isn't
in the API yet.)

Build it:

```
python3 tools/build.py my-apps/clock
```

This writes `my-apps/clock/clock.pxa`. If something is wrong, it says what: a compile error, a missing
`pxl_frame`, or a C library function the app can't have (see [Writing C without a C library](#writing-c-without-a-c-library)).

## Running it on your Pixlpal

1. **Turn on Developer Mode, once.** In the phone app, open **Maker & Developer**, then **App Manager**. Open its
   **Developer** section and switch on **Developer Mode**. Pixlpal asks you to hold its knob; hold it within 20
   seconds. It stays on until you switch it off.
2. **Serve the app from your computer.** In the app's folder, run:
   ```
   python3 -m http.server
   ```
   Leave it running. It prints `Serving HTTP on :: port 8000 (http://[::]:8000/)`. That `[::]` is not an address
   you can use: it means "on every network connection".
3. **Find your computer's address** on the network: on a Mac, `ipconfig getifaddr en0`; on Linux, `hostname -I`; on
   Windows, `ipconfig` (the IPv4 address). It looks like `192.168.1.20`.
4. **Install it.** In App Manager, under **Install an App From a Link**, enter
   `http://192.168.1.20:8000/clock.pxa`, with your address and your file's name. Pixlpal and your computer must
   be on the same Wi-Fi network. The terminal shows a line ending in `"GET /clock.pxa HTTP/1.0" 200` when Pixlpal
   downloads it.
5. **Open it.** It appears under **Installed on Pixlpal** in the phone app, and in App Manager's list on Pixlpal
   (turn the knob, click). Pixlpal gives personal apps numbers from 200/0 upwards.

To try a change, build again and install from the same link. Pixlpal replaces the old copy.

To remove an app, enter its number (such as `200/0`) under **Remove an App** in App Manager's Developer section. Its
saved data is removed with it.

## What Pixlpal calls in your app

You write these functions and mark each with `PXL_EXPORT`. Only `pxl_frame` is required.

| Function | When Pixlpal calls it |
|---|---|
| `void pxl_start(void)` | Once, after loading. Read your saved data and set up your phone page here. Don't draw: Pixlpal sometimes starts an app only to fetch its phone page, then closes it. |
| `void pxl_frame(uint32_t ms, uint32_t dt)` | Every 20 ms. `ms` is the time since Pixlpal started, `dt` the time since the last frame. Draw the whole screen each time; Pixlpal shows it when `pxl_frame` returns. |
| `void pxl_input(int kind, int a, int b)` | When the knob, a USB keyboard or a USB mouse is used. See below. |
| `int pxl_command(int command, int length)` | When someone uses your phone page. See [The phone page](#the-phone-page). Return 0 if you used the command. |
| `char* pxl_inbox(void)` | Return a buffer of `PXL_INBOX_SIZE` (512) bytes. Pixlpal puts each phone command's JSON there before calling `pxl_command`. |
| `void pxl_stop(void)` | Before the app closes. Save here. |

Input kinds (`pxl_input`):

| `kind` | Meaning | `a` | `b` |
|---|---|---|---|
| `PXL_IN_TURN` | The knob turned | +1 clockwise, −1 back | |
| `PXL_IN_CLICK` | The knob's button, a short press | | |
| `PXL_IN_HOLD` | The knob's button, held | | |
| `PXL_IN_KEY_DOWN` | A key pressed on a USB keyboard | the HID key code | the character, or 0 |
| `PXL_IN_KEY_UP` | That key released | the HID key code | |
| `PXL_IN_MOUSE_MOVE` | A USB mouse moved | dx | dy |
| `PXL_IN_MOUSE_WHEEL` | Its wheel turned | + away from you | |
| `PXL_IN_MOUSE_BUTTONS` | Its buttons changed | bit 0 left, 1 right, 2 middle | |

## What your app can call

The screen is `PXL_W` × `PXL_H`, 256 × 128 pixels. (0, 0) is the top left. Colours are RGB565: `PXL_RGB(r, g, b)`
makes one from 0–255 values, and `PXL_BLACK` and `PXL_WHITE` are there. Anything drawn off the screen is clipped.

**Drawing**

| Call | What it does |
|---|---|
| `pxl_fill(color)` | Fills the whole screen |
| `pxl_rect(x, y, w, h, color)` | A filled rectangle |
| `pxl_frame_rect(x, y, w, h, color)` | A rectangle's outline |
| `pxl_pixel(x, y, color)` | One pixel |
| `pxl_circle(x, y, r, color)` | A filled circle |
| `pxl_triangle(x0, y0, x1, y1, x2, y2, color)` | A filled triangle |
| `pxl_text(x, y, font, align, color, background, text)` | Text, at most 64 characters, on a background colour. `font`: `PXL_FONT_SMALL` (4 × 6), `PXL_FONT_NORMAL` (6 × 8), `PXL_FONT_LARGE` (8 × 12). `align`: `PXL_LEFT` (x is the left edge), `PXL_CENTRE` or `PXL_RIGHT`. Returns the text's left edge. |
| `pxl_text_width(font, text)` | How wide that text would be, in pixels |

Each call costs time, so draw big areas with `pxl_rect` rather than many `pxl_pixel`s. Clearing the screen with
`pxl_fill` takes about 1.5 ms of the 20 ms a frame has.

`pxl_blend(a, b, t)` (mix two colours, t from 0 to 1) and `pxl_dim(color, level)` (darken, level 0–255) are helpers
in `pixlpal.h` and cost nothing.

**Time and chance**

| Call | What it does |
|---|---|
| `pxl_millis()` | Milliseconds since Pixlpal started |
| `pxl_random()` | A random 32-bit number |

**Sound**

| Call | What it does |
|---|---|
| `pxl_sound(notes, count)` | Plays notes, an array of `pxl_note_t {hz, ms}` (at most 40; `hz` 0 is a rest). It plays while your app carries on. Nothing plays if Pixlpal is in Silent Mode. |
| `pxl_sound_stop()` | Stops it |

```c
static const pxl_note_t win[] = {{523, 80}, {659, 80}, {784, 160}};
pxl_sound(win, 3);
```

**Saved data.** One block of up to `PXL_SAVE_MAX` (1024) bytes, kept across restarts and updates:

| Call | What it does |
|---|---|
| `pxl_save_write(data, bytes)` | Saves. Returns bytes, or 0 if it failed. |
| `pxl_save_read(data, bytes)` | Fills `data` if a block of exactly `bytes` was saved, and returns bytes. Returns 0 if nothing was saved yet, or it was saved with another size: keep your defaults then. |

Save a struct, and save when something changes or in `pxl_stop`, not every frame: saving writes flash. If you
change the struct in an update, old saves won't load (the size differs), so add a version field if you want to read
them.

**The phone page**: `pxl_phone_ui`, `pxl_phone_values`, `pxl_json_int`, `pxl_json_str`. See the next section.

**The log**: `pxl_log(text)` writes a line to Pixlpal's serial log, as `WASM_APP: text`.

## The phone page

Your app can have a page in the phone app with buttons, switches, sliders, text fields and status panels. It shows
when someone taps your app under **Installed on Pixlpal**. Without one, the page just shows your `about`.

Set the page in `pxl_start` with `pxl_phone_ui(manifest, widgets)`, two JSON texts:

```c
static const char UI_MANIFEST[] = "{\"name\": \"My Clock\", \"author\": \"Your Name\", \"version\": \"1.0.0\","
                                  " \"about\": \"A big clock.\"}";
static const char UI_WIDGETS[] = "{\"widgets\": ["
    "{\"type\": \"statusPanel\", \"title\": \"Now\", \"source\": \"now\"},"
    "{\"type\": \"slider\", \"title\": \"Text Size\", \"min\": 0, \"max\": 2, \"default\": 1,"
    " \"command\": {\"app_command\": 1, \"args\": [\"size\"]}},"
    "{\"type\": \"borderButton\", \"label\": \"Reset\", \"command\": {\"app_command\": 2}}"
    "]}";

PXL_EXPORT("pxl_start") void pxl_start(void){ pxl_phone_ui(UI_MANIFEST, UI_WIDGETS); }
```

**Commands.** A widget's `command` is `{"app_command": N, "args": ["name"]}`, with N from 0 to 30. When it's used,
Pixlpal calls `pxl_command(N, length)` with the JSON in your inbox, for the slider above
`{"app_command": 1, "size": 2}`. Read it with:

| Call | What it does |
|---|---|
| `pxl_json_int(json, key, fallback)` | The whole number under `key`, or `fallback` |
| `pxl_json_str(json, key, out, outSize)` | Copies the text under `key` into `out`, cut to fit and 0-terminated. Returns its length, or −1 if there is none. |

```c
PXL_EXPORT("pxl_command") int pxl_command(int command, int length){
    if (command == 1){ size = pxl_json_int(inbox, "size", size); send_values(); return 0; }
    if (command == 2){ size = 1; send_values(); return 0; }
    return -1;
}
```

**Values.** `pxl_phone_values(json)` sends what the page shows, as one JSON object. Send it in `pxl_start` and
whenever something changes. Two kinds of field:

- **A widget's own value**, under its argument's name: `{"size": 2}` sets the slider, so it shows the device's
  value rather than its default.
- **Rows for a `statusPanel`**, under the panel's `source`:
  `{"now": [{"label": "Size", "value": "Large", "state": "ok"}]}`. `state` colours the row's dot: `ok`, `warn`,
  `error`, `info` or `off`.

**Widgets** an installed app can use:

| `type` | Fields | Sends |
|---|---|---|
| `richTextBlock` | `title`, `blocks`: `{type: paragraph\|heading\|subheading\|bullet\|note, text}` | nothing |
| `writeup` | `title`, `body` | nothing |
| `borderButton` | `label`, `command`, `confirm` (asks first: `true` or a text) | `{app_command}` |
| `switch` | `title`, `default` | `{arg: 1\|0}` |
| `slider` | `title`, `min`, `max`, `default`, `step`, `suffix`, `decimals` | `{arg: number}` |
| `radioGroup` | `title`, `default`, `options`: `[{label, value}]` | `{arg: value}` |
| `textField` | `title`, `emptyHint`, `filledHint` | `{arg: text}` when sent |
| `textColorField` | `title`, `defaultText`, `defaultColor`; args `[text, color]` | `{text, color: "0xAARRGGBB"}` |
| `timeRange` | `title`, `mode` (`range` or `single`), `defaultFrom`, `defaultTo`; args `[from, to]` | `{from: "22:00", to: "07:00"}` |
| `statusPanel` | `title`, `source` (a value holding rows), `emptyText` | nothing |
| `section` | `title`, `subtitle`, `widgets`, `collapsed` | nothing (a group that folds) |

Any widget can have `visibleWhen`, such as `{"arg": "mode", "equals": 1}`, to show only while a value matches. A
page can also have `"toasts": [{"when": {"app_command": 2}, "text": "Reset"}]`, a short message when a command is
sent.

[apps/com.meterbit.pixel-arcade-wasm/arcade.c](../apps/com.meterbit.pixel-arcade-wasm/arcade.c) has a full page:
status panels, a game choice and a game pad.

Size limits: the manifest 2 KB, the widgets 12 KB, the values 4 KB.

## Limits

| What | Limit |
|---|---|
| The package | 512 KB |
| The app's memory | 128 KB: 16 KB of stack, the rest your globals and static arrays |
| One call into the app | About 20 million WebAssembly instructions, roughly a second. An app that runs longer, such as one stuck in a loop, is stopped. |
| Text | 64 characters per `pxl_text` |
| Saved data | 1 KB |
| Sound | 40 notes per `pxl_sound` |
| Phone command JSON | 512 bytes |
| Installed apps | 24 on one Pixlpal |

A frame has 20 ms. If yours take longer, the app still works, but runs slower.

## Writing C without a C library

Apps are built with `-nostdlib`, so there is no `printf`, `sprintf`, `malloc`, `strlen`, `strcpy`, `memset`,
`memcpy`, `rand` or `sin`. Calling one fails the build with `undefined symbol: memcpy` (or whichever it was).

What to do instead:

- **Memory:** use global and static arrays, sized for the most you need.
- **Text:** write the small helper you need, such as the number-to-text function in
  [examples/hello/hello.c](../examples/hello/hello.c).
- **Copying and clearing:** `__builtin_memcpy(d, s, n)` and `__builtin_memset(d, c, n)` work: they become
  WebAssembly's own copy and fill instructions. Copying a struct with `=` works too.
- **Random numbers:** `pxl_random()`.
- **Maths:** `float` and `double` arithmetic and `__builtin_sqrtf` work. For `sin` and `cos`, use a small table or a
  short approximation.

You can split an app into several `.c` files in its folder: `build.py` compiles them all.

## When something goes wrong

- **Pixlpal shows a problem screen** when an app stops. It says why: an out-of-bounds memory access, an endless
  loop, or Developer Mode being off.
- **App Manager's Status panel** in the phone app shows why an install failed, such as "could not download it"
  (check the address and that both are on the same network) or "the app does not load".
- **The serial log.** With Pixlpal connected to your computer by USB, a serial monitor shows your `pxl_log` lines. Every 30 seconds it also shows your app's frame time, split into your code and Pixlpal's
  drawing, with the drawing broken down by function.
- **Inspect a build:** `python3 tools/build.py my-apps/clock --keep-wasm` also keeps `clock.wasm`, and
  `python3 tools/wasm_info.py my-apps/clock/clock.wasm` lists what it imports and exports.

## app.json

| Field | Required | Meaning |
|---|---|---|
| `id` | yes | Unique to the app, lower case: `io.github.yourname.clock` (at most 47 bytes) |
| `name` | yes | What owners see (at most 39 bytes) |
| `version` | yes | Numbers and dots, like `1.2.0`. Raise it with every change you publish. |
| `author` | yes | You or your team (at most 31 bytes) |
| `api` | yes | The Pixlpal App API version the app was written for: `0.2` |
| `about` | catalogue | One or two sentences on what it does |
| `category` | catalogue | `games`, `kids`, `learning`, `clocks`, `art`, `productivity`, `tools`, `maker` or `other` |
| `appID` | no | Given by Meterbit to catalogue apps (100/0 to 199/255). Leave it out. |

Ready to share your app? See [CONTRIBUTING.md](../CONTRIBUTING.md).
