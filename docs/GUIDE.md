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
8. [The internet](#the-internet)
9. [Limits](#limits)
10. [Writing C without a C library](#writing-c-without-a-c-library), [or with one, and C++](#c-the-c-library-and-loop-style-apps)
11. [When something goes wrong](#when-something-goes-wrong)
12. [app.json](#appjson)

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
  "api": "0.3",
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

It shows the time since Pixlpal started, and the knob changes the size of the text. For the real time of day, use
`pxl_time` ([below](#what-your-app-can-call)).

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
| `void pxl_frame(uint32_t ms, uint32_t dt)` | On every second frame of the panel's: about every 21 ms, evenly. `ms` is the time since Pixlpal started, `dt` the time since the last frame. Draw the whole screen each time; Pixlpal shows it all at once at the next frame, so it never shows half drawn. |
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
| `pxl_text(x, y, font, align, color, background, text)` | Text, at most 64 characters. `font`: `PXL_FONT_SMALL` (4 × 6), `PXL_FONT_NORMAL` (6 × 8), `PXL_FONT_LARGE` (8 × 12). `align`: `PXL_LEFT` (x is the left edge), `PXL_CENTRE` or `PXL_RIGHT`. `background`: a colour for the box behind the text, or `PXL_CLEAR` to draw on what is there. Returns the text's left edge. |
| `pxl_text_width(font, text)` | How wide that text would be, in pixels |
| `pxl_line(x0, y0, x1, y1, color)` | A line (0.3) |
| `pxl_round_rect(x, y, w, h, radius, color)` | A filled rectangle with rounded corners (0.3) |
| `pxl_bitmap(x, y, w, h, pixels)` | A picture: `w × h` RGB565 colours from a `uint16_t` array, row by row (0.3) |
| `pxl_bitmap_mono(x, y, w, h, bits, color, background)` | A one-colour picture: rows of `(w + 7) / 8` bytes, bit 0 of each byte the leftmost pixel. `background` a colour, or `PXL_CLEAR` to leave the screen as it is (0.3) |

Pictures are how an app brings icons, sprites and fonts of its own: put them in `static const` arrays. A
one-colour picture takes an eighth of the space of a colour one, and can be drawn in any colour.

Each call costs time, so draw big areas with `pxl_rect` rather than many `pxl_pixel`s. Clearing the screen with
`pxl_fill` takes about 1.5 ms of the 21 ms a frame has.

`pxl_blend(a, b, t)` (mix two colours, t from 0 to 1) and `pxl_dim(color, level)` (darken, level 0–255) are helpers
in `pixlpal.h` and cost nothing.

**Time and chance**

| Call | What it does |
|---|---|
| `pxl_millis()` | Milliseconds since Pixlpal started |
| `pxl_random()` | A random 32-bit number |
| `pxl_time(&t)` | The time of day in the owner's time zone (0.3): `t.year`, `month` (1–12), `day`, `hour` (0–23), `minute`, `second`, `weekday` (0 is Sunday), `yearday`, `utc_offset_minutes` and `unix` (seconds since 1970, UTC). Returns `t.valid`: 0 until Pixlpal has had the time from the internet, so show something like `--:--` until then. |
| `pxl_clock_24h()` | 1 if the owner chose a 24-hour clock, 0 for 12-hour (0.3) |

```c
pxl_time_t t;
if (pxl_time(&t)){
    char hm[6] = "00:00";
    int h = pxl_clock_24h() ? t.hour : (t.hour + 11) % 12 + 1;
    hm[0] += h / 10; hm[1] += h % 10; hm[3] += t.minute / 10; hm[4] += t.minute % 10;
    pxl_text(PXL_W / 2, 50, PXL_FONT_LARGE, PXL_CENTRE, PXL_WHITE, PXL_CLEAR, hm);
}
```

**Sound**

| Call | What it does |
|---|---|
| `pxl_sound(notes, count)` | Plays notes, an array of `pxl_note_t {hz, ms}` (at most 40; `hz` 0 is a rest). It plays while your app carries on. Nothing plays if Pixlpal is in Silent Mode. |
| `pxl_sound_stop()` | Stops it |
| `pxl_say(text, language)` | Speaks up to 200 characters in a language (`"en"`, `"fr"`, `"es"`...), with Google's text to speech (0.3). It needs the internet, the text goes to Google, and it plays at most once every 2 seconds. Returns 1 if it started. |
| `pxl_silent()` | 1 while Pixlpal is in Silent Mode, when sounds and speech don't play (0.3) |

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
| `pxl_json_float(json, key, fallback)` | A number with decimals (0.3) |
| `pxl_json_count(json, key)` | How many entries the array or object under `key` has, or −1 (0.3) |

`key` can also be a path, such as `"settings.size"` (0.3): see [The internet](#the-internet).

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
| `textField` | `title`, `emptyHint`, `filledHint`, `secret` (hides what is typed, for keys and passwords) | `{arg: text}` when sent |
| `textColorField` | `title`, `defaultText`, `defaultColor`; args `[text, color]` | `{text, color: "0xAARRGGBB"}` |
| `timeRange` | `title`, `mode` (`range` or `single`), `defaultFrom`, `defaultTo`; args `[from, to]` | `{from: "22:00", to: "07:00"}` |
| `statusPanel` | `title`, `source` (a value holding rows), `emptyText` | nothing |
| `section` | `title`, `subtitle`, `widgets`, `collapsed` | nothing (a group that folds) |

Any widget can have `visibleWhen`, such as `{"arg": "mode", "equals": 1}`, to show only while a value matches. A
page can also have `"toasts": [{"when": {"app_command": 2}, "text": "Reset"}]`, a short message when a command is
sent.

[apps/com.meterbit.pixel-arcade-wasm/arcade.c](../apps/com.meterbit.pixel-arcade-wasm/arcade.c) has a full page:
status panels, a game choice and a game pad.

Size limits: the manifest 2 KB, the widgets 24 KB, the values 4 KB. The widgets are measured compact: Pixlpal
strips the indentation, so lay the JSON out as you like. A page over a limit, or not valid JSON, is not shown (the
serial log says which), and the phone gets the page made from the package's manifest.

## The internet

An app can fetch data from the internet (0.3): weather, prices, scores, news. It can only reach the hosts its
`app.json` lists, over https:

```json
{ "...": "...", "api": "0.3", "net": ["api.open-meteo.com", "*.example.org"] }
```

`"*.example.org"` allows any subdomain of `example.org`, but not `example.org` itself; list that separately if you
need it. Owners see the list on the app's page in the App Store, and the review checks it. A redirect to a host
that isn't listed is not followed.

- **A service with no https** (0.5): list it as `"http://calendar.example.cz"`. The app may then reach that host over
  plain http too. The App Store tells owners it isn't encrypted, and the review asks why the service needs it. POST
  is https only.
- **Sites the owner chooses** (0.5): with `"ownerNet": true`, the app may also reach the sites its owner allows, such
  as an RSS feed or a website to watch. When a command from your phone page carries an address your app can't reach
  yet (a `textField` value, a picked search result), the phone app asks the owner first, and Pixlpal keeps the sites
  they allow (up to 16) where your app can't change them. Owners see and take them back on the app's page in the App
  Store. Until a site is allowed, a request to it returns `PXL_HTTP_NOT_ALLOWED`: tell the owner, rather than
  reporting the site down. Keep the hosts your app always uses (its ready-made feeds) in `net`.

A request doesn't stop your app: it returns at once, and Pixlpal calls your `pxl_http_done` when the answer is in.

```c
static char answer[4096];
static float temperature;

static void ask(void){
    int id = pxl_http_get("https://api.open-meteo.com/v1/forecast?latitude=6.5&longitude=3.4&current=temperature_2m",
                          "", answer, sizeof(answer));
    if (id < 0) pxl_log("didn't start");      /* PXL_HTTP_BUSY, _NOT_ALLOWED, _BAD, _OFFLINE or _TOO_OFTEN */
}

PXL_EXPORT("pxl_http_done") void pxl_http_done(int id, int status, int length){
    if (status != 200) return;                 /* or PXL_HTTP_FAILED (no answer), PXL_HTTP_TOO_BIG (cut short) */
    /* answer holds length bytes, then a 0 if there was room: {"current": {"temperature_2m": 27.4, ...}, ...} */
    temperature = pxl_json_float(answer, "current.temperature_2m", temperature);
}
```

| Call | What it does |
|---|---|
| `pxl_http_get(url, headers, buffer, size)` | Starts a GET. `headers` is `""` or lines of `"Name: value"` (at most 4), such as `"X-Api-Key: abc"`. Returns the request's number, or a negative reason. |
| `pxl_http_post(url, headers, body, bytes, buffer, size)` | Starts a POST with a body of up to 8 KB. Give its `Content-Type` among the headers. |

- One request at a time, at most 30 a minute. Ask when you need fresh data, every few minutes, not every frame.
- Answers of up to 64 KB: make `buffer` as big as the answers you expect. Many APIs can return less, such as only the
  fields you ask for.
- **Reading JSON:** `pxl_json_int`, `pxl_json_float`, `pxl_json_str` and `pxl_json_count` take a path through objects
  and arrays, such as `"current.temperature_2m"` or `"list.0.name"` (`"list.0"` is the first entry). Each call reads
  the whole answer again, so read the values you need once, when the answer arrives, and keep them.
- **API keys.** If a service needs the owner's own key, ask for it on your phone page with a `textField` with
  `"secret": true`, which hides it and never keeps it on the phone. Keep it with `pxl_save_write`, and send it in a
  header or the URL. It can only ever reach the hosts you listed. Never put your own key in an app's source: anyone
  can read it there.

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
| Internet | one request at a time, 30 a minute, answers up to 64 KB, POST bodies up to 8 KB, only the hosts in `net` |
| Speech | 200 characters, once every 2 seconds |

A frame has about 21 ms. If yours take longer, the app still works, but runs slower: its next frame starts at the
panel's next frame (about every 10.5 ms), so each one is still shown whole.

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

## C++, the C library, and loop-style apps

An app can also use the C and C++ libraries (`malloc`, `snprintf`, `strtol`, maths, `new`, containers): write `.cpp`
files, or put `"libc": true` in `app.json` for a C app. `build.py` then builds it for WASI, and Pixlpal provides the few
WASI functions the libraries need (the time, random numbers, and `printf` into its log). Files, sockets and threads
are not there: use the API's files (`pxl_file_*`) and internet (`pxl_http_*`). Exceptions and RTTI are off. Such an
app is bigger (about 50 KB more), and its memory can grow to 4 MB.

**Loop-style apps (0.4).** Instead of `pxl_frame`, an app can export `pxl_main` and run as one loop, waiting with
`pxl_yield(ms)`. Pixlpal shows the frame at each yield, and calls your `pxl_input` and `pxl_command` from inside it.
When `pxl_yield` returns 0, return from `pxl_main`: the app is closing.

```c
PXL_EXPORT("pxl_main") void pxl_main(void){
    while (pxl_yield(20)){
        pxl_fill(PXL_BLACK);
        /* draw, as in a frame */
    }
}
```

**Files (0.4)**: `pxl_file_read`, `pxl_file_write`, `pxl_file_remove`, `pxl_file_list`. Up to 64 KB a file and 256 KB
in all, kept across updates and removed with the app.

**The device (0.4)**: `pxl_usb_state()` (bit 0 a keyboard, bit 1 a mouse), `pxl_brightness()` and
`pxl_set_brightness(percent)`, `pxl_brightness_step(direction)` (what the knob usually does),
`pxl_open_app(group, number)`, `pxl_open_previous_app()`, `pxl_sound_busy()`, `pxl_status_text(text, color)` (a notice
along the status bar of a `"screen": "statusbar"` app). `PXL_IN_CLICK` carries how many clicks (2 for a double).

**The phone (0.4)**: `pxl_phone_data` (the datasets list widgets name), `pxl_phone_search` (a searchPicker's results),
`pxl_phone_send` (a message of your own on your app's route).

**Pictures (0.5)**: Pixlpal decodes PNG, JPEG (baseline) and SVG for you, from bytes in your app's memory (an
internet answer, a file): `pxl_image(x, y, w, h, data, bytes, background)` draws a picture fitted into a box, centred,
over `background` where it is transparent; `pxl_image_size(data, bytes)` gives its own size; and
`pxl_image_pixels(data, bytes, w, h, out, outBytes)` fits it into a box as RGBA pixels (4 bytes each) without
drawing, for an app that keeps it (a cache of logos) or blends it itself. Each returns `width << 16 | height`
(`pxl_image_w`, `pxl_image_h`), or 0. A PNG or JPEG is scaled by whole steps, so pixel art stays sharp; an SVG is drawn
at the largest size that fits. Pictures up to 256 KB, PNG and JPEG up to 512 x 512 pixels.

**Tasks (0.6)**: work that waits, such as a request or a slow parse, can run beside your app's loop instead of
stopping it. `pxl_task_start(fn, arg, stack, bytes)` runs `fn(arg)` on a task, with `stack` (a buffer of yours, at
least 4 KB, 16-byte aligned, kept until `fn` returns) as its C stack; up to three at a time, started from your main
code. A task and your main code take turns, never running at once, so they share variables without locks; the turn
changes only while one of them waits (`pxl_yield` in the main code, `pxl_task_wait(ms)` in a task). Wait in a task
now and then: one that runs about a second without waiting stops the app. `pxl_task_wait` returns 0 when the app is
closing; then return from `fn`. Internet answers still arrive in your main code's `pxl_http_done`.

```c
static char stack[16384] __attribute__((aligned(16)));
static volatile int ready;
static void fetch(void* arg){
    /* ... start a request; the answer arrives in pxl_http_done, which sets ready ... */
    while (!ready && pxl_task_wait(50)) {}
}
/* in pxl_start or pxl_main: */ pxl_task_start(fetch, 0, stack, sizeof stack);
```

**Apps written for Pixlpal's firmware** build almost unchanged with `"kit": "compat"`: the compat layer provides the
firmware's app framework on the API. [sdk/compat/README.md](../sdk/compat/README.md) explains it; the catalogue's
`com.meterbit.*` apps are all built that way.

## When something goes wrong

- **Pixlpal shows a problem screen** when an app stops. It says why: an out-of-bounds memory access, an endless
  loop, or Developer Mode being off.
- **App Manager's Status panel** in the phone app shows why an install failed, such as "could not download it"
  (check the address and that both are on the same network) or "the app does not load".
- **The serial log.** With Pixlpal connected to your computer by USB, a serial monitor shows your `pxl_log` lines.
- **Inspect a build:** `python3 tools/build.py my-apps/clock --keep-wasm` also keeps `clock.wasm`, and
  `python3 tools/wasm_info.py my-apps/clock/clock.wasm` lists what it imports and exports.

## app.json

| Field | Required | Meaning |
|---|---|---|
| `id` | yes | Unique to the app, lower case: `io.github.yourname.clock` (at most 47 bytes) |
| `name` | yes | What owners see (at most 39 bytes) |
| `version` | yes | Numbers and dots, like `1.2.0`. Raise it with every change you publish. |
| `author` | yes | You or your team (at most 31 bytes) |
| `api` | yes | The Pixlpal App API version the app was written for: `0.6` (or older, for an app that uses nothing newer; an app built with `"kit": "compat"` needs `0.6`). A Pixlpal with older software refuses an app that needs a newer API, and asks its owner to update. |
| `about` | catalogue | One or two sentences on what it does |
| `category` | catalogue | Where the phone app lists it, beside the built-in apps of that category: `clocks`, `calendars`, `news`, `weather`, `sports`, `audio`, `kids`, `games`, `productivity`, `ai`, `notifications`, `markets`, `art`, `maker`, `home`, `learning`, `travel`, `faith`, `health`, `tools` or `other` (only under Installed on Pixlpal). Personal apps can have one too. |
| `net` | no | The hosts the app reaches over the internet: `["api.example.com", "*.example.org"]`, and (0.5) `"http://host"` for one with no https. Up to 8 (24 with `api` 0.5). |
| `ownerNet` | no | `true` (0.5): it may also reach the sites its owner allows on the phone ([The internet](#the-internet)) |
| `screen` | no | `"full"` (the default) or `"statusbar"`: the app runs below Pixlpal's status bar (rows 10 and down are its) |
| `libc` | no | `true`: a C app built with the C library (C++ apps always are) |
| `kit` | no | `"compat"`: built with the compat layer, the firmware's app framework |
| `dataGroup` | no | Catalogue apps: a folder shared with your other apps in the group, `"com.example.games"`, under your own id's developer part |
| `migrateNvs`, `sharedNvs` | no | Meterbit's apps moved out of the firmware: the settings keys they kept, copied into their files when installed (`migrateNvs`), and those kept in their group's folder (`sharedNvs`) |
| `appID` | no | Given by Meterbit to catalogue apps (100/0 to 199/255). Leave it out. |

Ready to share your app? See [CONTRIBUTING.md](../CONTRIBUTING.md).
