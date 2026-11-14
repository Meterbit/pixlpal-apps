/**
 * @file bcc_Mobile_Ui.h
 * @brief Mobile UI of Business & Creator Counters {16,0} (BUSINESS), rendered by the Pixlpal phone app
 *        (docs/MOBILE_UI_FILES, format 2).
 *
 * Commands (businessCounters.cpp):
 *   0 ask every source again now
 *   1 {"stripeOn"} | {"paystackOn"} | {"shopifyOn"} | {"youtubeOn"} | {"githubOn"} | {"plausibleOn"} | {"goal"} | {"chime"} |
 *     {"announce"} | {"showAmounts"} | {"quietOn"} | {"cycle"} | {"volume"}
 *   2 {"shopStore"} | {"shopClientId"} | {"ytChannel"} | {"ghRepos"} | {"ghUser"} | {"plSite"} | {"plHost"} | {"goalCurrency"}
 *   3 secrets: {"stripeKey"} | {"paystackKey"} | {"shopSecret"} | {"ytKey"} | {"ghToken"} | {"plKey"} (empty clears one)
 *   4 {"quietFrom", "quietTo"}   5 a test sale (the sound and the banner)
 * Status rows: "today", "shops", "creators" ({label, value, state}).
 */
#ifndef BCC_MOBILE_UI_H
#define BCC_MOBILE_UI_H

static const char BCC_UI_MANIFEST[] = R"json({
  "appID": "16/0",
  "name": "Business Counters",
  "about": "Today's sales, orders and revenue from Stripe, Paystack and Shopify, with a daily goal and a cash register's ring for every new sale. For creators: YouTube subscribers, GitHub stars and your website's visitors, with a fanfare at each milestone. Pixlpal asks each service itself with your own keys, which never leave it.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char BCC_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "asset:businessCounters.png", "height": 64, "alt": "Business Counters" },
    { "type": "statusPanel", "title": "Today", "source": "today" },
    { "type": "statusPanel", "title": "Shops", "emptyText": "No shop switched on yet.", "source": "shops" },
    { "type": "statusPanel", "title": "Creator Counters", "emptyText": "No creator counter switched on yet.", "source": "creators" },
    { "type": "borderButton", "label": "REFRESH NOW", "command": { "app_command": 0 } },
    {
      "type": "section",
      "id": "goal",
      "title": "Goal, Sound and Screen",
      "widgets": [
        { "type": "textField", "title": "Today's Sales Goal (Empty: None)", "emptyHint": "500", "command": { "app_command": 1, "args": ["goal"] } },
        { "type": "textField", "title": "Goal Currency (Shown Large)", "emptyHint": "USD, NGN, EUR... Empty: the first shop's", "command": { "app_command": 2, "args": ["goalCurrency"] } },
        { "type": "switch", "title": "Show Amounts (Off: Only the Number of Sales)", "default": true, "command": { "app_command": 1, "args": ["showAmounts"] } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Switch amounts off if customers can see the screen: Pixlpal then shows how many sales, not how much." } ] },
        {
          "type": "radioGroup",
          "title": "Sound",
          "default": 2,
          "options": [ { "label": "Off", "value": 0 }, { "label": "Sales", "value": 1 }, { "label": "Sales and Milestones", "value": 2 } ],
          "command": { "app_command": 1, "args": ["chime"] }
        },
        { "type": "switch", "title": "Say the Amount (\"New Sale, 49 Dollars\")", "default": false, "command": { "app_command": 1, "args": ["announce"] }, "visibleWhen": { "arg": "chime", "in": [1, 2] } },
        { "type": "slider", "title": "Volume", "min": 1, "max": 10, "default": 6, "step": 1, "suffix": "", "command": { "app_command": 1, "args": ["volume"] }, "visibleWhen": { "arg": "chime", "in": [1, 2] } },
        { "type": "switch", "title": "Quiet Hours", "default": false, "command": { "app_command": 1, "args": ["quietOn"] }, "visibleWhen": { "arg": "chime", "in": [1, 2] } },
        { "type": "timeRange", "title": "No Sound", "mode": "range", "defaultFrom": "22:00", "defaultTo": "07:00", "fromLabel": "From", "toLabel": "Until", "command": { "app_command": 4, "args": ["quietFrom", "quietTo"] }, "visibleWhen": { "arg": "quietOn", "equals": 1 } },
        { "type": "borderButton", "label": "PLAY A TEST SALE", "command": { "app_command": 5 } },
        { "type": "slider", "title": "Sales and Creator Pages Turn Every (0: Stay)", "min": 0, "max": 120, "default": 20, "step": 5, "suffix": "Secs", "command": { "app_command": 1, "args": ["cycle"] } }
      ]
    },
    {
      "type": "section",
      "id": "stripe",
      "title": "Stripe",
      "subtitle": "Payments, Checkout, Payment Links",
      "collapsed": true,
      "widgets": [
        { "type": "switch", "title": "Count Stripe Sales", "default": false, "command": { "app_command": 1, "args": ["stripeOn"] } },
        { "type": "textField", "title": "Restricted API Key", "emptyHint": "rk_live_...", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 3, "args": ["stripeKey"] }, "visibleWhen": { "arg": "stripeOn", "equals": 1 } },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "bullet", "text": "In the Stripe Dashboard: Developers, API keys, Create restricted key. Give it Charges: Read and nothing else, so the key can only look." },
            { "type": "bullet", "text": "A test key (rk_test_) counts test payments, shown as TEST MODE." },
            { "type": "paragraph", "spans": [ { "text": "Keys: " }, { "text": "dashboard.stripe.com/apikeys", "link": "https://dashboard.stripe.com/apikeys" } ] }
          ],
          "visibleWhen": { "arg": "stripeOn", "equals": 1 }
        }
      ]
    },
    {
      "type": "section",
      "id": "paystack",
      "title": "Paystack",
      "subtitle": "Nigeria, Ghana, Kenya, South Africa, Côte d'Ivoire",
      "collapsed": true,
      "widgets": [
        { "type": "switch", "title": "Count Paystack Sales", "default": false, "command": { "app_command": 1, "args": ["paystackOn"] } },
        { "type": "textField", "title": "Secret Key", "emptyHint": "sk_live_...", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 3, "args": ["paystackKey"] }, "visibleWhen": { "arg": "paystackOn", "equals": 1 } },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "bullet", "text": "In the Paystack Dashboard: Settings, API Keys & Webhooks. Paystack has no read-only keys, so the secret key stays on Pixlpal: it is never sent back to the phone or anywhere but Paystack." },
            { "type": "bullet", "text": "Successful transactions count; abandoned and failed ones do not." }
          ],
          "visibleWhen": { "arg": "paystackOn", "equals": 1 }
        }
      ]
    },
    {
      "type": "section",
      "id": "shopify",
      "title": "Shopify",
      "subtitle": "Your store's orders",
      "collapsed": true,
      "widgets": [
        { "type": "switch", "title": "Count Shopify Orders", "default": false, "command": { "app_command": 1, "args": ["shopifyOn"] } },
        { "type": "textField", "title": "Store", "emptyHint": "my-shop.myshopify.com", "command": { "app_command": 2, "args": ["shopStore"] }, "visibleWhen": { "arg": "shopifyOn", "equals": 1 } },
        { "type": "textField", "title": "App Client ID (Empty for an Admin API Token)", "emptyHint": "From the app's Settings in the Dev Dashboard", "command": { "app_command": 2, "args": ["shopClientId"] }, "visibleWhen": { "arg": "shopifyOn", "equals": 1 } },
        { "type": "textField", "title": "Client Secret, or Admin API Token", "emptyHint": "The client secret, or shpat_...", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 3, "args": ["shopSecret"] }, "visibleWhen": { "arg": "shopifyOn", "equals": 1 } },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "bullet", "text": "New stores: in Shopify's Dev Dashboard, create an app with the read_orders access scope and install it on your store (the app and the store must be in the same organization). Paste its Client ID and Client secret here. Pixlpal gets a new 24-hour access token by itself." },
            { "type": "bullet", "text": "A custom app made earlier in the Shopify admin still works: leave the Client ID empty and paste its Admin API access token (shpat_...)." },
            { "type": "bullet", "text": "Cancelled orders do not count. Test orders count only on a development store. Your Shopify plan is shown above." },
            { "type": "paragraph", "spans": [ { "text": "Dev Dashboard: " }, { "text": "dev.shopify.com/dashboard", "link": "https://dev.shopify.com/dashboard/" } ] }
          ],
          "visibleWhen": { "arg": "shopifyOn", "equals": 1 }
        }
      ]
    },
    {
      "type": "section",
      "id": "youtube",
      "title": "YouTube Subscribers",
      "collapsed": true,
      "widgets": [
        { "type": "switch", "title": "Count YouTube Subscribers", "default": false, "command": { "app_command": 1, "args": ["youtubeOn"] } },
        { "type": "textField", "title": "Channel", "emptyHint": "@yourhandle, or UC... channel ID", "command": { "app_command": 2, "args": ["ytChannel"] }, "visibleWhen": { "arg": "youtubeOn", "equals": 1 } },
        { "type": "textField", "title": "YouTube Data API Key", "emptyHint": "AIza...", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 3, "args": ["ytKey"] }, "visibleWhen": { "arg": "youtubeOn", "equals": 1 } },
        {
          "type": "richTextBlock",
          "blocks": [
            { "type": "bullet", "text": "In the Google Cloud console: turn on YouTube Data API v3 for a project, then Credentials, Create credentials, API key. It is free: 10,000 units a day, and Pixlpal uses about 290." },
            { "type": "bullet", "text": "YouTube rounds subscriber counts it shows publicly (12,345 shows as 12,300), so the count moves in steps." },
            { "type": "paragraph", "spans": [ { "text": "Console: " }, { "text": "console.cloud.google.com/apis/library/youtube.googleapis.com", "link": "https://console.cloud.google.com/apis/library/youtube.googleapis.com" } ] }
          ],
          "visibleWhen": { "arg": "youtubeOn", "equals": 1 }
        }
      ]
    },
    {
      "type": "section",
      "id": "github",
      "title": "GitHub Stars and Followers",
      "collapsed": true,
      "widgets": [
        { "type": "switch", "title": "Count GitHub Stars", "default": false, "command": { "app_command": 1, "args": ["githubOn"] } },
        { "type": "textField", "title": "Repos (Up to Three)", "emptyHint": "owner/repo, owner/other", "command": { "app_command": 2, "args": ["ghRepos"] }, "visibleWhen": { "arg": "githubOn", "equals": 1 } },
        { "type": "textField", "title": "Followers Of (Optional)", "emptyHint": "Your user or organisation name", "command": { "app_command": 2, "args": ["ghUser"] }, "visibleWhen": { "arg": "githubOn", "equals": 1 } },
        { "type": "textField", "title": "Token (Optional)", "emptyHint": "github_pat_... (only for private repos or faster updates)", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 3, "args": ["ghToken"] }, "visibleWhen": { "arg": "githubOn", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Without a token GitHub allows 60 requests an hour, so Pixlpal looks every 10 minutes or so; with one (a fine-grained token with read-only access is enough), every 2 minutes." } ], "visibleWhen": { "arg": "githubOn", "equals": 1 } }
      ]
    },
    {
      "type": "section",
      "id": "plausible",
      "title": "Website Visitors (Plausible)",
      "collapsed": true,
      "widgets": [
        { "type": "switch", "title": "Count Website Visitors", "default": false, "command": { "app_command": 1, "args": ["plausibleOn"] } },
        { "type": "textField", "title": "Site", "emptyHint": "example.com (as named in Plausible)", "command": { "app_command": 2, "args": ["plSite"] }, "visibleWhen": { "arg": "plausibleOn", "equals": 1 } },
        { "type": "textField", "title": "Stats API Key", "emptyHint": "From Account settings, API Keys", "filledHint": "Saved on Pixlpal: paste again to change it", "secret": true, "obscureText": true, "command": { "app_command": 3, "args": ["plKey"] }, "visibleWhen": { "arg": "plausibleOn", "equals": 1 } },
        { "type": "textField", "title": "Server (Self-Hosted Only)", "emptyHint": "plausible.io", "command": { "app_command": 2, "args": ["plHost"] }, "visibleWhen": { "arg": "plausibleOn", "equals": 1 } },
        { "type": "richTextBlock", "blocks": [ { "type": "note", "text": "Plausible's Stats API comes with its Business plan (and self-hosted Plausible). Pixlpal shows visitors in the last five minutes, refreshed every minute, and today's visitors and pageviews: about 72 of the 600 requests an hour a key may make." } ], "visibleWhen": { "arg": "plausibleOn", "equals": 1 } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Click: the other page (sales, creators), or close a banner. Double click: ask everything again now. Long press: sound off or on. Turn: brightness." },
        { "type": "bullet", "text": "Shops are asked every 30 seconds, so a sale shows within half a minute. Sales made before the app opened are counted, without a ring." },
        { "type": "bullet", "text": "Today is today by Pixlpal's clock: midnight to midnight where you are." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Asking now" },
    { "when": { "app_command": 1 }, "text": "Saved" },
    { "when": { "app_command": 2 }, "text": "Saved" },
    { "when": { "app_command": 3 }, "text": "Saved on Pixlpal" },
    { "when": { "app_command": 4 }, "text": "Quiet hours saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Open Business Counters on Pixlpal first" },
    { "when": { "app_command": 1, "response": 0 }, "text": "That number is not allowed here" },
    { "when": { "app_command": 2, "response": 0 }, "text": "That does not look right: check the name or address" },
    { "when": { "app_command": 3, "response": 0 }, "text": "That does not look like this service's key" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Open Business Counters on Pixlpal first" }
  ]
})json";

#endif
