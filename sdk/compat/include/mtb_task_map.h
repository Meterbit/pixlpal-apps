/**
 * @file mtb_task_map.h
 * @brief Where every time-critical Pixlpal task runs: core and FreeRTOS priority, in one place.
 *
 * Layout (2026-11-10), from measurements on the L2 (ESP32-S31, 2 cores at 320 MHz):
 *
 *   CORE 0, connectivity and audio engine
 *     23  Wi-Fi driver            (IDF, CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0)
 *     22  esp_timer               (IDF)
 *     ~21 NimBLE host, BT ctrl    (IDF, CONFIG_BT_NIMBLE_PINNED_TO_CORE_0, CONFIG_BT_CTRL_PINNED_TO_CORE=0)
 *     18  lwIP tcpip              (IDF, CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0, pinned here by this layout)
 *      6  audio playback          (ESP32-audioI2S decode + I2S feed, ChatGPT voice playback)
 *      5  audio capture           (microphones + echo cancelling: Aud In, ChatGPT mic, Baby Monitor ear)
 *      5  USB host + class drivers, USB speaker feed
 *      4  BLE command parsers, local image decoders (PNG/SVG/JPG)
 *      1  app tasks (Mtb_Applications)
 *
 *   CORE 1, display and user interface
 *      5  p125_refresh            (panel stream; encoding takes up to 75% of the core while the picture changes)
 *      4  audio source reader     (Aud Out Serv.: file and web-stream reads into the 655 KB input buffer)
 *      4  text scrollers, ChatGPT network task (TLS work off core 0)
 *      3  knob and button, GIF animation
 *      1  MQTT client, image download/draw workers
 *
 * Why this split:
 * - The panel can use most of core 1 whenever the picture changes, so nothing with a hard deadline lives there
 *   below it. Audio's deadline is the 53 ms I2S DMA cushion (10 x 256 frames at 48 kHz); on core 0 at priority 6
 *   only short, high-priority radio work can delay it (measured: longest gap 2-4 ms, 100% of frames real).
 * - lwIP is pinned to core 0 beside the Wi-Fi driver. Unpinned, its priority-18 bursts (web radio, ChatGPT's
 *   websocket) could land on core 1 and pre-empt the panel refresh.
 * - Streaming from the internet keeps both busy at once: the radio on core 0 above audio, which preempts it only
 *   in sub-millisecond bursts; the socket reader on core 1 with a 40 s input buffer; TLS decryption (ChatGPT)
 *   on core 1 so it doesn't take core 0 time from the audio engine.
 * - Capture sits one step below playback: both must meet a 53 ms DMA deadline, but a late playback frame is an
 *   audible gap while a late capture frame is only a later read.
 * - UI tasks that people feel (knob, scrolling) are above the bulk work on core 1, below the panel itself.
 */
#pragma once

#define MTB_CORE_RADIO_AUDIO            0
#define MTB_CORE_UI                     1

// Core 0
#define MTB_PRIO_AUDIO_PLAY             6   /**< Mirrors PIXLPAL_AUDIO_TASK_PRIO in ESP32-audioI2S/src/Audio.h. */
#define MTB_PRIO_AUDIO_CAPTURE          5
#define MTB_PRIO_USB_AUDIO              5
#define MTB_PRIO_APP                    1

// Core 1
#define MTB_PRIO_AUDIO_SOURCE           4
#define MTB_PRIO_TEXT_SCROLL            4
#define MTB_PRIO_NET_TLS                4
#define MTB_PRIO_INPUT                  3
#define MTB_PRIO_ANIMATION              3
#define MTB_PRIO_MQTT                   1
