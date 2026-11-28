/**
 * @file prn_Mobile_Ui.h
 * @brief 3D Printer Status's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (printerStatus.cpp):
 *   0 {"kind": 0|1}          0 OctoPrint, 1 Klipper (Moonraker: Mainsail, Fluidd)
 *   1 {"address": "http://192.168.1.50"}   the printer's address on the owner's network (a site the owner allows)
 *   2 {"apiKey": "..."}      OctoPrint's key (or Moonraker's, if it asks for one); empty to forget it
 *   3 {"name": "Ender 3"}    what Pixlpal calls it
 *   4 {"sound": 0|1}         a chime when a print finishes or stops
 */
#ifndef PRN_MOBILE_UI_H
#define PRN_MOBILE_UI_H

static const char PRN_UI_MANIFEST[] = R"json({
  "name": "3D Printer Status",
  "about": "Your 3D printer's print at a glance: how far along it is, the time left and when it will be done, the nozzle and bed temperatures, and a chime when it finishes. For OctoPrint and for Klipper (Mainsail, Fluidd) on your own network.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char PRN_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.printer-status.png", "height": 64, "alt": "3D Printer Status" },
    { "type": "statusPanel", "title": "The Printer", "source": "now", "emptyText": "Open 3D Printer Status on Pixlpal." },
    {
      "type": "radioGroup",
      "title": "Your Printer Runs",
      "default": 0,
      "options": [
        { "label": "OctoPrint", "value": 0 },
        { "label": "Klipper (Moonraker: Mainsail or Fluidd)", "value": 1 }
      ],
      "command": { "app_command": 0, "args": ["kind"] }
    },
    { "type": "textField", "title": "Its Address", "emptyHint": "http://192.168.1.50", "filledHint": "Change the address", "command": { "app_command": 1, "args": ["address"] } },
    { "type": "textField", "title": "API Key", "emptyHint": "OctoPrint: Settings, Application Keys", "filledHint": "A key is saved (send an empty one to forget it)", "secret": true, "command": { "app_command": 2, "args": ["apiKey"] } },
    { "type": "textField", "title": "Its Name", "emptyHint": "e.g. Prusa MK4", "filledHint": "Change the name", "command": { "app_command": 3, "args": ["name"] } },
    { "type": "switch", "title": "Chime When a Print Finishes or Stops", "default": 1, "command": { "app_command": 4, "args": ["sound"] } },
    {
      "type": "richTextBlock",
      "title": "Setting It Up",
      "blocks": [
        { "type": "bullet", "text": "The address is the one you open in your browser to see the printer, with http:// in front: for example http://192.168.1.50, or http://192.168.1.50:7125 for Moonraker on its own port. Use the number, not a name like octopi.local." },
        { "type": "bullet", "text": "The phone asks you to let the app reach that address. Your printer's own network is not encrypted, so the app asks for it over plain http." },
        { "type": "bullet", "text": "OctoPrint needs a key: in OctoPrint, Settings, then Application Keys, make one for Pixlpal. Klipper's Moonraker usually needs none on a home network." },
        { "type": "note", "text": "Pixlpal and the printer must be on the same network. It asks every 5 seconds while printing." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1 }, "text": "Address saved" },
    { "when": { "app_command": 1, "response": 0 }, "text": "Start it with http:// or https://" },
    { "when": { "app_command": 2 }, "text": "Key saved" },
    { "when": { "app_command": 3 }, "text": "Name saved" }
  ]
})json";

#endif
