/**
 * @file sol_Mobile_Ui.h
 * @brief Solar Inverter's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (solarInverter.cpp):
 *   0 {"source": 0-2}            0 SolarEdge (cloud), 1 Fronius (on the owner's network), 2 any JSON address
 *   1 {"siteId": "1234567"}      SolarEdge's site number
 *   2 {"apiKey": "..."}          SolarEdge's key; empty to forget it
 *   3 {"address": "http://..."}  Fronius: the inverter; any JSON: the full address (sites the owner allows)
 *   4 {"header": "Name: value"}  any JSON: one request header, such as a token; empty for none
 *   5 {"paths": "/pv,/load,/soc,/grid,/battery"}   any JSON: where each value is (JSON Pointer), comma separated
 *   6 {"unit": 0|1}              any JSON: the values are in watts (0) or kilowatts (1)
 *   7 {"gridSign": 0|1}          any JSON: positive grid is buying (0) or selling (1)
 *   8 {"capacity": 1-100}        the panels' size in kW, for the sun's gauge
 */
#ifndef SOL_MOBILE_UI_H
#define SOL_MOBILE_UI_H

static const char SOL_UI_MANIFEST[] = R"json({
  "name": "Solar Inverter",
  "about": "Your solar system's power flow, live: what the panels make, what the house uses, the battery's charge and whether you are buying from or selling to the grid, with today's energy. For SolarEdge, Fronius, or any inverter or hub that gives JSON.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char SOL_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.solar-inverter.png", "height": 64, "alt": "Solar Inverter" },
    { "type": "statusPanel", "title": "Right Now", "source": "now", "emptyText": "Open Solar Inverter on Pixlpal." },
    {
      "type": "radioGroup",
      "title": "Your System",
      "default": 0,
      "options": [
        { "label": "SolarEdge (its monitoring cloud)", "value": 0 },
        { "label": "Fronius (the inverter on your network)", "value": 1 },
        { "label": "Any JSON address (Home Assistant, Node-RED, a hub)", "value": 2 }
      ],
      "command": { "app_command": 0, "args": ["source"] }
    },
    { "type": "textField", "title": "SolarEdge Site ID", "emptyHint": "The number in your monitoring link", "filledHint": "Change the site", "command": { "app_command": 1, "args": ["siteId"] }, "visibleWhen": { "arg": "source", "equals": 0 } },
    { "type": "textField", "title": "SolarEdge API Key", "emptyHint": "Admin, Site Access, API Access", "filledHint": "A key is saved (send an empty one to forget it)", "secret": true, "command": { "app_command": 2, "args": ["apiKey"] }, "visibleWhen": { "arg": "source", "equals": 0 } },
    { "type": "textField", "title": "Fronius Inverter Address", "emptyHint": "http://192.168.1.60", "filledHint": "Change the address", "command": { "app_command": 3, "args": ["address"] }, "visibleWhen": { "arg": "source", "equals": 1 } },
    {
      "type": "section", "title": "Any JSON Address", "subtitle": "One address that answers with all the values", "visibleWhen": { "arg": "source", "equals": 2 },
      "widgets": [
        { "type": "textField", "title": "Address", "emptyHint": "http://192.168.1.20:8123/api/...", "filledHint": "Change the address", "command": { "app_command": 3, "args": ["address"] } },
        { "type": "textField", "title": "Header (optional)", "emptyHint": "Authorization: Bearer your-token", "filledHint": "A header is saved (send an empty one to drop it)", "secret": true, "command": { "app_command": 4, "args": ["header"] } },
        { "type": "textField", "title": "Where Each Value Is", "emptyHint": "/pv,/load,/battery_soc,/grid,/battery_power", "filledHint": "Change the paths", "command": { "app_command": 5, "args": ["paths"] } },
        { "type": "radioGroup", "title": "The Values Are In", "default": 0, "options": [ { "label": "Watts", "value": 0 }, { "label": "Kilowatts", "value": 1 } ], "command": { "app_command": 6, "args": ["unit"] } },
        { "type": "radioGroup", "title": "A Positive Grid Value Means", "default": 0, "options": [ { "label": "Buying from the grid", "value": 0 }, { "label": "Selling to the grid", "value": 1 } ], "command": { "app_command": 7, "args": ["gridSign"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "paragraph", "text": "Paths are JSON Pointers, in this order: solar power, the house's use, the battery's charge in percent, the grid, the battery's power (positive while it discharges). Leave out what you don't have, keeping the commas: /pv,/load,,/grid" },
          { "type": "paragraph", "text": "For {\"data\": {\"pv\": 3200}}, the path is /data/pv. A list's first item is /0." }
        ] }
      ]
    },
    { "type": "slider", "title": "Your Panels' Size", "min": 1, "max": 100, "step": 1, "default": 5, "suffix": " kW", "command": { "app_command": 8, "args": ["capacity"] } },
    {
      "type": "richTextBlock",
      "title": "Good to Know",
      "blocks": [
        { "type": "bullet", "text": "SolarEdge allows 300 requests a day for a site, so Pixlpal asks every 6 minutes, and for today's energy every half hour. Your key stays on Pixlpal." },
        { "type": "bullet", "text": "Fronius and JSON addresses are on your own network: the phone asks you to allow the address. Pixlpal asks every 10 seconds." },
        { "type": "note", "text": "The moving dots show which way the power flows: faster for more power." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1 }, "text": "Site saved" },
    { "when": { "app_command": 1, "response": 0 }, "text": "The site ID is a number" },
    { "when": { "app_command": 2 }, "text": "Key saved" },
    { "when": { "app_command": 3 }, "text": "Address saved" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Start it with http:// or https://" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Each path starts with / (or leave it empty)" }
  ]
})json";

#endif
