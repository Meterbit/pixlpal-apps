/**
 * @file cc_Mobile_Ui.h
 * @brief Cloud Cost Meter's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (cloudCost.cpp), each for account 1-4 by its key's last digit:
 *   0 {"prov1": 0-4}       none, DigitalOcean, Akamai (Linode), AWS, a JSON address of your own
 *   1 {"label1": "..."}    2 {"secret1": "..."}  (the token; AWS "key ID:secret"; your own: its https address)
 *   3 {"path1": "a.b.0"}   your own: where the month's cost is in its answer
 *   4 {"budget1": 0-10000000}   whole dollars a month (0: no budget)
 *   5 {"awsHours": 1-24}   6 {"sound": 0|1}   7 {} check now
 */
#ifndef CC_MOBILE_UI_H
#define CC_MOBILE_UI_H

static const char CC_UI_MANIFEST[] = R"json({
  "name": "Cloud Cost Meter",
  "about": "This month's cloud bill so far, from AWS, DigitalOcean, Akamai (Linode) or your own JSON address: up to four accounts, the total, where the month is heading, and each budget. A chime when an account heads over its budget.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

#define CC_ACCOUNT(N, HINT) \
    "{ \"type\": \"section\", \"id\": \"a" #N "\", \"title\": \"Account " #N "\", \"collapsed\": " HINT ", \"widgets\": [" \
    "{ \"type\": \"radioGroup\", \"title\": \"Cloud\", \"default\": 0, \"options\": [" \
      "{ \"label\": \"None\", \"value\": 0 }, { \"label\": \"DigitalOcean\", \"value\": 1 }, { \"label\": \"Akamai (Linode)\", \"value\": 2 }," \
      "{ \"label\": \"AWS\", \"value\": 3 }, { \"label\": \"My own JSON address\", \"value\": 4 } ]," \
      "\"command\": { \"app_command\": 0, \"args\": [\"prov" #N "\"] } }," \
    "{ \"type\": \"textField\", \"title\": \"Name on Pixlpal\", \"emptyHint\": \"Production\", \"filledHint\": \"Change it\", \"command\": { \"app_command\": 1, \"args\": [\"label" #N "\"] }, \"visibleWhen\": { \"arg\": \"prov" #N "\", \"notEquals\": 0 } }," \
    "{ \"type\": \"textField\", \"title\": \"API Token\", \"emptyHint\": \"dop_v1_... or the Linode token\", \"filledHint\": \"A token is saved (send an empty one to forget it)\", \"secret\": true, \"command\": { \"app_command\": 2, \"args\": [\"secret" #N "\"] }, \"visibleWhen\": { \"arg\": \"prov" #N "\", \"in\": [1, 2] } }," \
    "{ \"type\": \"textField\", \"title\": \"Access Key ID:Secret Access Key\", \"emptyHint\": \"AKIA...:wJalr...\", \"filledHint\": \"A key is saved (send an empty one to forget it)\", \"secret\": true, \"command\": { \"app_command\": 2, \"args\": [\"secret" #N "\"] }, \"visibleWhen\": { \"arg\": \"prov" #N "\", \"equals\": 3 } }," \
    "{ \"type\": \"textField\", \"title\": \"Address (https)\", \"emptyHint\": \"https://example.com/cost.json\", \"filledHint\": \"An address is saved (send an empty one to forget it)\", \"secret\": true, \"command\": { \"app_command\": 2, \"args\": [\"secret" #N "\"] }, \"visibleWhen\": { \"arg\": \"prov" #N "\", \"equals\": 4 } }," \
    "{ \"type\": \"textField\", \"title\": \"Where the Cost Is in Its Answer\", \"emptyHint\": \"month_to_date or data.0.cost\", \"filledHint\": \"Change it\", \"command\": { \"app_command\": 3, \"args\": [\"path" #N "\"] }, \"visibleWhen\": { \"arg\": \"prov" #N "\", \"equals\": 4 } }," \
    "{ \"type\": \"textField\", \"title\": \"Monthly Budget ($)\", \"emptyHint\": \"500 (0: none)\", \"filledHint\": \"Change it (0: none)\", \"command\": { \"app_command\": 4, \"args\": [\"budget" #N "\"] }, \"visibleWhen\": { \"arg\": \"prov" #N "\", \"notEquals\": 0 } }" \
    "] },"

static const char CC_UI_API[] = "{ \"widgets\": ["
    "{ \"type\": \"image\", \"src\": \"https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.cloud-cost.png\", \"height\": 64, \"alt\": \"Cloud Cost Meter\" },"
    "{ \"type\": \"statusPanel\", \"title\": \"This Month\", \"source\": \"now\", \"emptyText\": \"Add an account below.\" },"
    "{ \"type\": \"borderButton\", \"label\": \"CHECK NOW\", \"command\": { \"app_command\": 7 } },"
    CC_ACCOUNT(1, "false") CC_ACCOUNT(2, "true") CC_ACCOUNT(3, "true") CC_ACCOUNT(4, "true")
    "{ \"type\": \"section\", \"id\": \"how\", \"title\": \"Getting a Key\", \"collapsed\": true, \"widgets\": ["
      "{ \"type\": \"richTextBlock\", \"blocks\": ["
        "{ \"type\": \"bullet\", \"text\": \"DigitalOcean: API, Tokens, Generate New Token, Custom Scopes, with billing read only.\" },"
        "{ \"type\": \"bullet\", \"text\": \"Akamai (Linode): Cloud Manager, your profile, API Tokens, Create, with Account set to Read Only and the rest to None.\" },"
        "{ \"type\": \"bullet\", \"text\": \"AWS: in IAM, a user of its own with only the ce:GetCostAndUsage permission, then an access key for it. Write the key ID, a colon, and the secret. Cost Explorer must be turned on in the billing console.\" },"
        "{ \"type\": \"bullet\", \"text\": \"My own JSON address: an https address answering with JSON that holds this month's cost so far, as a number or as text, and where it is, such as total or data.0.cost. The phone asks you to allow the address.\" },"
        "{ \"type\": \"note\", \"text\": \"AWS charges $0.01 for each Cost Explorer request. Pixlpal asks AWS every few hours (below): every 8 hours is about $0.90 a month. The others are asked every 30 minutes, at no charge.\" },"
        "{ \"type\": \"note\", \"text\": \"Keys are kept on Pixlpal only, and each is used only with its own cloud.\" }"
      "] }"
    "] },"
    "{ \"type\": \"section\", \"id\": \"settings\", \"title\": \"Checking\", \"collapsed\": true, \"widgets\": ["
      "{ \"type\": \"slider\", \"title\": \"Ask AWS Every\", \"min\": 1, \"max\": 24, \"default\": 8, \"suffix\": \" h\", \"command\": { \"app_command\": 5, \"args\": [\"awsHours\"] } },"
      "{ \"type\": \"switch\", \"title\": \"Chime When an Account Heads Over Its Budget\", \"default\": true, \"command\": { \"app_command\": 6, \"args\": [\"sound\"] } }"
    "] },"
    "{ \"type\": \"richTextBlock\", \"title\": \"On Pixlpal\", \"blocks\": ["
      "{ \"type\": \"bullet\", \"text\": \"The month so far for all the accounts, where the month is heading at this pace, and each account against its budget: green within it, amber heading over it, red over it.\" },"
      "{ \"type\": \"bullet\", \"text\": \"Costs are as each cloud reports them, in US dollars: AWS's are about a day behind. Click the knob to check now.\" }"
    "] }"
  "],"
  "\"toasts\": ["
    "{ \"when\": { \"app_command\": 2 }, \"text\": \"Saved\" },"
    "{ \"when\": { \"app_command\": 2, \"response\": 0 }, \"text\": \"That doesn't fit this cloud: check it, or choose the cloud first\" },"
    "{ \"when\": { \"app_command\": 3, \"response\": 0 }, \"text\": \"Write it like total or data.0.cost\" },"
    "{ \"when\": { \"app_command\": 4 }, \"text\": \"Budget saved\" },"
    "{ \"when\": { \"app_command\": 4, \"response\": 0 }, \"text\": \"Write whole dollars, such as 500\" },"
    "{ \"when\": { \"app_command\": 7 }, \"text\": \"Checking now\" },"
    "{ \"when\": { \"app_command\": 7, \"response\": 0 }, \"text\": \"Open Cloud Cost Meter on Pixlpal first\" }"
  "] }";

#endif
