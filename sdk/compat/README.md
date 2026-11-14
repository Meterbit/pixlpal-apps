# The compat layer: firmware apps as installed apps

An app written for Pixlpal's firmware (its app framework, `meterbit_engine`, and kits) builds as an installed app
with few changes. This is how Meterbit moved 29 built-in apps out of the firmware into the catalogue. The layer
provides look-alikes of the headers those apps include, implemented on the Pixlpal App API (`sdk/pixlpal.h`).

Build an app with it by putting `"kit": "compat"` in its `app.json`. `tools/build.py` then compiles the app's C++
with the C and C++ libraries, this layer (`include/`, `src/`) and ArduinoJson.

## What it provides

| The firmware's | Here |
|---|---|
| The app's task, `while (THIS_APP_IS_ACTIVE)`, `delay()` | `pxl_main` runs the task; `delay()` is `pxl_yield`, so the loop shows its frame and gets input |
| Knob callbacks (`mtb_App_Set_EC11_Cb_Fns`) | Called from `pxl_input`; clicks keep their count (double clicks) |
| Phone commands (`mtb_App_Set_Ble_Comm_Fns`), `mtb_Ble_App_Cmd_Reject` | Called from `pxl_command` |
| The phone page (`mtb_App_Set_Mobile_UI`), values, search results, datasets | `pxl_phone_ui`, `pxl_phone_values`, `pxl_phone_search`, `pxl_phone_data` |
| `bleApplicationComSend` (files to the phone) | `pxl_phone_send`, on the app's own route |
| `mtb_Read_Nvs_Struct` / `mtb_Write_Nvs_Struct` | A file per key, `nvs.<key>` (app.json's `sharedNvs` keys in its group's folder) |
| `LittleFS`, `File` (Arduino) | The app's files: paths made flat names, read and written whole (64 KB a file) |
| `mtb_Panel_*` drawing | `pxl_rect`, `pxl_line`... ; text is drawn in the app with the firmware's own fonts |
| `mtb_kit_ui` (the firmware's file, compiled here) | The same, its text drawn by the layer |
| `mtb_Kit_Http_Get` / `_Send`, `HTTPClient`, `WiFiClientSecure` | `pxl_http_*`, waited for (the loop pauses; input keeps arriving). GET and POST, app.json's `net` hosts |
| `snd_Play`, `snd_Say`, `mtb_Silent_Mode`, `mtb_Clock_24h`, time, `localtime_r` | `pxl_sound`, `pxl_say`, `pxl_silent`, `pxl_clock_24h`, `pxl_time` |
| USB keyboard and mouse (`mtb_Usb_Input_Read`...) | A queue `pxl_input` fills |
| Brightness, opening other apps, `statusBarNotif`, `Mtb_ScrollText_t`, `Mtb_FixedText_t` | `pxl_brightness*`, `pxl_open_app`, `pxl_status_text`; the ticker and text widget drawn by the layer |
| FreeRTOS queues, semaphores, critical sections | Work within one thread (a queue wait yields) |

## One thread

An installed app is one thread, so other tasks don't run beside it:

- **A task that does one job and ends** (a fetch): `xTaskCreatePinnedToCore` queues it, and it runs to its end at the
  app's next wait. The app's loop pauses meanwhile, which for a fetch is a second or two.
- **A worker that loops forever** must be cut into steps. Register a step with `pxl_compat_add_background(step)`; the
  layer runs it at each of the app's waits, never inside another step. The catalogue's apps did this mechanically:
  the worker's prologue became `worker_Begin`, its loop body `worker_Step` (inside `do { } while (0)`, so its
  `continue`s end the step), its variables statics, and long waits a wake-up time.

## Not here

- **Raw audio** (`mtb_Audio_Pcm_*`): `mtb_Audio_Pcm_Begin` returns false. Play notes or speech instead.
- **JPEG decoding:** `mtb_Decode_Jpg_RGB888` fails. PNG (lodepng) and SVG (nanosvg) work in the app.
- **Response headers** from HTTP, and methods other than GET and POST.
- **Hosts the owner chooses** (a custom ntfy server, any RSS feed): only app.json's `net` hosts.
- **The device's shared files** (the GIFs on Pixlpal, the microSD card).

## Porting a firmware app

1. Copy its source (and the kits it uses) into `apps/<id>/`, and write `app.json`: its old number as `appID`,
   `"kit": "compat"`, `"screen": "statusbar"` for a status-bar app, `net` for the hosts it reaches, and
   `migrateNvs` for the NVS keys its settings were in (Pixlpal copies them into its files when it is installed, so
   owners keep their settings).
2. Build it: `python3 tools/build.py apps/<id>`. The usual changes: a forever worker cut into steps, a file layer for
   apps that used folders, raw audio replaced by notes.
3. Remove it from the firmware, and from `meterbit_engine`'s requirements.

The originals of this layer are in the firmware repository (`components/meterbit_wasm/compat`); the catalogue has a
copy in `sdk/compat`.
