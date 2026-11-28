/**
 * @file fol_Mobile_Ui.h
 * @brief Follower Counter's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (followerCounter.cpp); slot k (0-3) is in the argument's name:
 *   0 {"kindK": 0-5}           none, YouTube subscribers, GitHub followers, GitHub stars, Bluesky, Mastodon
 *   1 {"accountK": "..."}      @handle, user, owner/repo, handle.bsky.social, or @user@server (Mastodon)
 *   2 {"labelK": "..."}        the name shown (empty: the account's own)
 *   3 {"ytKey": "..."}         the owner's YouTube Data API key; empty to forget it
 *   4 {"layout": 0|1}          one at a time, or all together
 *   5 {"seconds": 5-60}        each one for, one at a time
 *   6 {"celebrate": 0|1}       confetti and a fanfare at each milestone
 *   7 {}                       check them all now
 */
#ifndef FOL_MOBILE_UI_H
#define FOL_MOBILE_UI_H

static const char FOL_UI_MANIFEST[] = R"json({
  "name": "Follower Counter",
  "about": "Your follower counts as big numbers that roll when they change: YouTube subscribers, GitHub followers and stars, Bluesky and Mastodon followers. Up to four, with today's gain and a celebration at each milestone.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

#define FOL_KINDS R"json("options": [ { "label": "None", "value": 0 }, { "label": "YouTube subscribers", "value": 1 }, { "label": "GitHub followers", "value": 2 }, { "label": "GitHub stars (a repository)", "value": 3 }, { "label": "Bluesky followers", "value": 4 }, { "label": "Mastodon followers", "value": 5 } ])json"
#define FOL_SLOT(K, TITLE, KIND) \
    "{ \"type\": \"section\", \"title\": \"" TITLE "\", \"collapsed\": " KIND ", \"widgets\": [" \
    "{ \"type\": \"radioGroup\", \"title\": \"Count\", \"default\": 0, " FOL_KINDS ", \"command\": { \"app_command\": 0, \"args\": [\"kind" K "\"] } }," \
    "{ \"type\": \"textField\", \"title\": \"Account\", \"emptyHint\": \"@channel, user, owner/repo, you.bsky.social or @you@server\", \"filledHint\": \"Change the account\", \"command\": { \"app_command\": 1, \"args\": [\"account" K "\"] } }," \
    "{ \"type\": \"textField\", \"title\": \"Name Shown (optional)\", \"emptyHint\": \"The account's own name\", \"filledHint\": \"Change the name\", \"command\": { \"app_command\": 2, \"args\": [\"label" K "\"] } }" \
    "] },"

static const char FOL_UI_API[] = "{ \"widgets\": ["
    "{ \"type\": \"image\", \"src\": \"https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.follower-counter.png\", \"height\": 64, \"alt\": \"Follower Counter\" },"
    "{ \"type\": \"statusPanel\", \"title\": \"The Counts\", \"source\": \"counts\", \"emptyText\": \"Add an account below.\" },"
    FOL_SLOT("0", "First Counter", "false")
    FOL_SLOT("1", "Second Counter", "true")
    FOL_SLOT("2", "Third Counter", "true")
    FOL_SLOT("3", "Fourth Counter", "true")
    "{ \"type\": \"textField\", \"title\": \"YouTube API Key (for YouTube only)\", \"emptyHint\": \"From Google Cloud: YouTube Data API v3\", \"filledHint\": \"A key is saved (send an empty one to forget it)\", \"secret\": true, \"command\": { \"app_command\": 3, \"args\": [\"ytKey\"] } },"
    "{ \"type\": \"radioGroup\", \"title\": \"Show\", \"default\": 0, \"options\": [ { \"label\": \"One at a time, big\", \"value\": 0 }, { \"label\": \"All together\", \"value\": 1 } ], \"command\": { \"app_command\": 4, \"args\": [\"layout\"] } },"
    "{ \"type\": \"slider\", \"title\": \"Each One For\", \"min\": 5, \"max\": 60, \"step\": 5, \"default\": 10, \"suffix\": \" s\", \"command\": { \"app_command\": 5, \"args\": [\"seconds\"] }, \"visibleWhen\": { \"arg\": \"layout\", \"equals\": 0 } },"
    "{ \"type\": \"switch\", \"title\": \"Celebrate Milestones\", \"default\": 1, \"command\": { \"app_command\": 6, \"args\": [\"celebrate\"] } },"
    "{ \"type\": \"borderButton\", \"label\": \"CHECK THEM NOW\", \"command\": { \"app_command\": 7 } },"
    "{ \"type\": \"richTextBlock\", \"title\": \"The Accounts\", \"blocks\": ["
        "{ \"type\": \"bullet\", \"text\": \"YouTube: the channel's @handle (or its UC... ID), and an API key of your own: in Google Cloud, turn on the YouTube Data API v3 and make a key. YouTube rounds big counts (1.23 million).\" },"
        "{ \"type\": \"bullet\", \"text\": \"GitHub: a user name for followers, or owner/repository for its stars. No key: GitHub allows 60 checks an hour, so Pixlpal checks every 10 minutes.\" },"
        "{ \"type\": \"bullet\", \"text\": \"Bluesky: the handle, like you.bsky.social. Mastodon: @you@server, on one of the larger servers: mastodon.social, mastodon.online, mstdn.social, mas.to, mastodon.world, fosstodon.org, hachyderm.io, infosec.exchange, techhub.social, mastodon.art, mstdn.jp, universeodon.com, social.vivaldi.net, mastodon.ie, aus.social, mastodon.nz, toot.community, sfba.social, piaille.fr, mastodon.uno or chaos.social.\" },"
        "{ \"type\": \"note\", \"text\": \"Milestones: 100, 250, 500, 1,000, 2,500, 5,000, 10,000 and on. Turn the knob to go through the counters.\" }"
    "] }"
    "],"
    "\"toasts\": ["
        "{ \"when\": { \"app_command\": 1 }, \"text\": \"Saved\" },"
        "{ \"when\": { \"app_command\": 1, \"response\": 0 }, \"text\": \"That isn't an account for this counter\" },"
        "{ \"when\": { \"app_command\": 3 }, \"text\": \"Key saved\" },"
        "{ \"when\": { \"app_command\": 7 }, \"text\": \"Checking\" }"
    "] }";

#endif
