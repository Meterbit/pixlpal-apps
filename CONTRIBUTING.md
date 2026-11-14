# Sharing your app

Once your app works on your own Pixlpal ([the developer guide](docs/GUIDE.md)), you can offer it to every Pixlpal
owner through this catalogue. Meterbit reviews each app, then builds it from your source code and signs it.

## What an app needs

- **Its source code:** `.c` files and nothing prebuilt. Meterbit builds the app itself, so what runs on Pixlpals is
  exactly the code that was reviewed.
- **`app.json`** with these fields:

  | Field | What to put | Example |
  |---|---|---|
  | `id` | A name only your app uses: a domain you own, reversed, then the app. Lower case. | `io.github.ana.moon-phase` |
  | `name` | What owners see (at most 39 bytes) | `Moon Phase` |
  | `version` | Numbers and dots | `1.0.0` |
  | `author` | You or your team (at most 31 bytes) | `Ana Silva` |
  | `api` | The Pixlpal App API version you wrote it for | `0.2` |
  | `category` | One of `games`, `kids`, `learning`, `clocks`, `art`, `productivity`, `tools`, `maker`, `other` | `art` |
  | `about` | One or two sentences on what it does (at most 300 characters) | `Tonight's moon, drawn to scale.` |

  Leave out `appID`: Meterbit gives the app its number.
- **`icon.png`:** square, 128 × 128 pixels is best (64 to 512 allowed), at most 32 KB. Pixlpal's own app icons are
  teal (`#008080`) line drawings on white, and that style fits the store best.
- **A folder named after the `id`:** `apps/io.github.ana.moon-phase/`.

## Submitting

1. Fork this repository and add your folder under `apps/`.
2. Run `python3 tools/publish.py check`. It builds every app and checks each one the way the review will. Fix
   anything it reports.
3. Open a pull request. Fill in the checklist in its description.
4. The automatic check runs the same command on GitHub. A reviewer then reads your code and tries the app on a
   Pixlpal.

You hear back through GitHub. Every comment, request for changes, approval and merge on your pull request reaches
you as a GitHub notification and an email. **Merged means your app is in the catalogue.** Meterbit then signs and
publishes it, usually the same day, and the pull request gets a comment saying so.

## Updating your app

Change the code, raise `version` in `app.json`, and open a pull request as before. A published version is never
changed: the check fails if the code changed but the version didn't. Each update is reviewed like a new app.
Pixlpals with your app see the update in the phone app's store.

## What the review looks for

An app is accepted if it:

- does what its name, `about` and icon say, and nothing hidden;
- works on a Pixlpal: it starts, responds to the knob, and doesn't freeze or crash;
- is something you have the right to share. Your own work, or work under a licence that allows it. Not a copy of
  someone else's game, characters, art or brand;
- is suitable for a device in homes and classrooms: no adult content, hate, harassment or gambling;
- doesn't imitate Pixlpal's system screens, or another app, to mislead owners;
- saves only what it needs, and uses the phone page only for the app itself.

Reviews are done by people and take time. Small, clear apps are reviewed fastest.

## The licence

By submitting, you agree that your app is under this repository's [MIT licence](LICENSE), and you confirm you have
the right to share it that way. You keep the copyright. Others may learn from it, change it and build on it.

## If something goes wrong after publishing

Anyone can [report an app](../../issues/new?template=report-an-app.md). If an app is harmful, or a version has a
serious bug, Meterbit withdraws it. Pixlpals then refuse to install or open the withdrawn version, and the store
offers the last good version if there is one. You'll be told on GitHub, and you can fix it and submit a new version.
