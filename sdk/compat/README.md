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
| Brightness, opening other apps, `statusBarNotif`, `Mtb_ScrollText_t`, `Mtb_FixedText_t` | `pxl_brightness*`, `pxl_open_app`, `pxl_status_text`; the ticker scrolled by Pixlpal (`pxl_ticker`) in an app that says `"api": "0.7"`, otherwise drawn by the layer; the text widget drawn by the layer |
| Tasks (`xTaskCreatePinnedToCore`) | Pixlpal tasks (`pxl_task_start`, API 0.6): see below |
| FreeRTOS queues, semaphores, critical sections, task notifications | Work as the tasks take turns (a wait hands the turn on) |
| FreeRTOS software timers (`xTimerCreate`...) | Their callbacks run at the app's waits once due |
| `mtb_kit_image` (`mtb_Kit_Draw_Svg`, `_Png`, `_Image`, `mtb_Kit_Decode_Image`), `mtb_Draw_Local_Png` | Pixlpal's decoders (`pxl_image`, `pxl_image_pixels`, API 0.5) |
| `mtb_Download_Github_File_To_SPIFFS` / `_To_PSRAM` | A public file from raw.githubusercontent.com (in `net`), into the app's files or memory |

## Tasks

An installed app runs on Pixlpal tasks (API 0.6) that take turns: the app's loop, and up to three tasks. App code
never runs on two of them at once; the turn changes only while one waits (its `delay()`, a queue or notification
wait, an internet request). So the firmware's code needs no change for this, and its locks do nothing.

- **A task the app starts** (`xTaskCreatePinnedToCore`) gets a Pixlpal task of its own, if one of two is free, with a C
  stack of about twice the firmware task's size (16 to 64 KB). Its loop may run forever, as on the firmware; when the
  app closes, its next wait ends it.
- **Otherwise it waits its turn in the background task**, which also runs the workers' steps
  (`pxl_compat_add_background`, from apps ported before 0.6) and the timers. Tasks there run one after another, so a
  task that never ends would hold the rest up.
- **The scrolling text** (`statusBarNotif`, `Mtb_ScrollText_t`) is drawn with the loop's frames.
- **Internet requests** wait in whichever task made them; the loop keeps drawing meanwhile. Pixlpal has one request
  out at a time per app, so a second waits for the first. A request that didn't start has its reason as the code
  (`PXL_HTTP_NOT_ALLOWED`: a site its owner hasn't allowed yet).
- **Drawing from a task** shows with the loop's next frame.
- **Big answers:** an installed app is interpreted, and ArduinoJson takes it about a second for every 30 KB of JSON
  (a task stops after about two seconds without waiting). For long arrays of numbers, ask for less or read the
  numbers straight from the text, as Tides does for its river history; answers are at most 64 KB.

## Not here

- **Raw audio** (`mtb_Audio_Pcm_*`, the audio player): `mtb_Audio_Pcm_Begin` returns false. Play notes or speech
  instead. (Prayer Times and Workout Focus stay built in for this.)
- **Other connections** than HTTP: SMTP email, MQTT, WebSockets. (Medicine stays built in for its email.)
- **Raw JPEG pixels:** `mtb_Decode_Jpg_RGB888` fails. Draw a JPEG (or PNG, SVG) with `mtb_Kit_Draw_Image`, or get
  its pixels fitted to a box with `mtb_Kit_Decode_Image`: Pixlpal decodes them.
- **Response headers** from HTTP, and methods other than GET and POST.
- **Hosts the owner types in freely** without `"ownerNet": true`. With it, the phone app asks the owner before an app
  reaches a new site; a request to a site not allowed yet returns `PXL_HTTP_NOT_ALLOWED` as its code.
- **The device's shared files** (the GIFs on Pixlpal, the microSD card).

## Porting a firmware app

1. Copy its source (and the kits it uses) into `apps/<id>/`, and write `app.json`: its old number as `appID`,
   `"kit": "compat"`, `"screen": "statusbar"` for a status-bar app, `net` for the hosts it reaches, and
   `migrateNvs` for the NVS keys its settings were in (Pixlpal copies them into its files when it is installed, so
   owners keep their settings).
2. Build it: `python3 tools/build.py apps/<id>`. The usual changes: a file layer for apps that used folders, and
   build settings (`sdkconfig.h`) an app's kits read.
3. Remove it from the firmware, and from `meterbit_engine`'s requirements.

The originals of this layer are in the firmware repository (`components/meterbit_wasm/compat`); the catalogue has a
copy in `sdk/compat`.
