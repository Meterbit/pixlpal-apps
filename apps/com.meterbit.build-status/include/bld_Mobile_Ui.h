/**
 * @file bld_Mobile_Ui.h
 * @brief Build Status's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (buildStatus.cpp):
 *   0 {"repo0" ... "repo5": "owner/repo" | "owner/repo@branch" | "gitlab:group/project[@branch]"}   one per call; empty removes
 *   1 {"githubToken": "..."}   2 {"gitlabToken": "..."}   (empty: forget it)
 *   3 {"minutes": 1-30}        4 {"sound": 0|1}           5 {}  check them all now
 */
#ifndef BLD_MOBILE_UI_H
#define BLD_MOBILE_UI_H

static const char BLD_UI_MANIFEST[] = R"json({
  "name": "Build Status",
  "about": "Your team's GitHub Actions and GitLab pipelines at a glance: passing, failing or running, on which branch, by whom and when. A broken build sounds an alarm; fixing it gets a cheer.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char BLD_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.build-status.png", "height": 64, "alt": "Build Status" },
    { "type": "statusPanel", "title": "Builds", "source": "builds", "emptyText": "No projects yet. Add them below." },
    { "type": "borderButton", "label": "CHECK THEM ALL NOW", "command": { "app_command": 5 } },
    {
      "type": "section",
      "id": "repos",
      "title": "Projects to Watch",
      "widgets": [
        { "type": "textField", "title": "Project 1", "emptyHint": "owner/repo, or gitlab:group/project", "filledHint": "Change project 1 (empty removes it)", "command": { "app_command": 0, "args": ["repo0"] } },
        { "type": "textField", "title": "Project 2", "emptyHint": "owner/repo", "filledHint": "Change project 2", "command": { "app_command": 0, "args": ["repo1"] } },
        { "type": "textField", "title": "Project 3", "emptyHint": "owner/repo", "filledHint": "Change project 3", "command": { "app_command": 0, "args": ["repo2"] } },
        { "type": "textField", "title": "Project 4", "emptyHint": "owner/repo", "filledHint": "Change project 4", "command": { "app_command": 0, "args": ["repo3"] } },
        { "type": "textField", "title": "Project 5", "emptyHint": "owner/repo", "filledHint": "Change project 5", "command": { "app_command": 0, "args": ["repo4"] } },
        { "type": "textField", "title": "Project 6", "emptyHint": "owner/repo", "filledHint": "Change project 6", "command": { "app_command": 0, "args": ["repo5"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "GitHub: owner/repo, such as octocat/hello-world. GitLab: gitlab:group/project (gitlab.com)." },
          { "type": "bullet", "text": "Pixlpal watches the project's default branch. Add @branch for another: octocat/hello-world@release." }
        ] }
      ]
    },
    {
      "type": "section",
      "id": "tokens",
      "title": "Access",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "GitHub Token", "emptyHint": "github_pat_...", "filledHint": "A token is saved (send an empty one to forget it)", "secret": true, "command": { "app_command": 1, "args": ["githubToken"] } },
        { "type": "textField", "title": "GitLab Token", "emptyHint": "glpat-...", "filledHint": "A token is saved (send an empty one to forget it)", "secret": true, "command": { "app_command": 2, "args": ["gitlabToken"] } },
        { "type": "richTextBlock", "blocks": [
          { "type": "bullet", "text": "GitHub: Settings, Developer settings, Fine-grained tokens. Give it the repositories, with Actions: Read-only. Without a token Pixlpal sees public repositories only, and checks less often (GitHub allows 60 requests an hour)." },
          { "type": "bullet", "text": "GitLab: Preferences, Access tokens, with the read_api scope." },
          { "type": "note", "text": "Tokens are kept on Pixlpal only, and used only with GitHub and GitLab." }
        ] }
      ]
    },
    {
      "type": "section",
      "id": "settings",
      "title": "Checking",
      "collapsed": true,
      "widgets": [
        { "type": "slider", "title": "Check Every", "min": 1, "max": 30, "default": 2, "suffix": " min", "command": { "app_command": 3, "args": ["minutes"] } },
        { "type": "switch", "title": "Sound When a Build Breaks or Is Fixed", "default": true, "command": { "app_command": 4, "args": ["sound"] } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Green: the last run passed. Red: it failed. Blue: running now. Grey: cancelled or skipped." },
        { "type": "bullet", "text": "Turn the knob to see one project with its last commit, click to check them all now." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 0 }, "text": "Project saved" },
    { "when": { "app_command": 0, "response": 0 }, "text": "Write owner/repo, or gitlab:group/project" },
    { "when": { "app_command": 1 }, "text": "GitHub token saved" },
    { "when": { "app_command": 2 }, "text": "GitLab token saved" },
    { "when": { "app_command": 5 }, "text": "Checking now" },
    { "when": { "app_command": 5, "response": 0 }, "text": "Open Build Status on Pixlpal first" }
  ]
})json";

#endif
