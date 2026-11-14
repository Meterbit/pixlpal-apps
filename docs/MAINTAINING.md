# Maintaining the catalogue (Meterbit)

Only Meterbit publishes, because publishing needs the app-signing key. Everything here runs on the maintainer's own
computer, with the key on that computer and never anywhere else.

## Once, on a new computer

1. Clone this repository and install the WASI SDK 34 ([the developer guide](GUIDE.md#setting-up-once)). Use the
   same version as the automatic check (`.github/workflows/check.yml`), so builds match it.
2. Put the app-signing key at `~/pixlpal-keys/pixlpal_app_key.pem`, copied from its backup. `publish.py` refuses a
   key inside any repository. Set `export PXP_APP_KEY=~/pixlpal-keys/pixlpal_app_key.pem` in your shell profile, or
   pass `--key` each time.

The key's public half is `catalogue/public_key.pem`. The same public key is built into Pixlpal's firmware
(`components/meterbit_wasm/include/mtb_app_trust.h`), and `publish.py` refuses any other key.

## Reviewing a pull request

1. **The automatic check** must pass. It builds every app and checks its `app.json`, icon and version.
2. **Read the code.** Look for:
   - anything the app does that its description doesn't say;
   - phone page text or links that mislead;
   - content that breaks the rules in [CONTRIBUTING.md](../CONTRIBUTING.md#what-the-review-looks-for).

   The sandbox already stops apps from reaching anything outside the API, so the review is about honesty, content
   and quality, not about the device's safety.
3. **Try it on a Pixlpal.** Check out the pull request's branch, then:
   ```
   python3 tools/build.py apps/<id>
   cd apps/<id> && python3 -m http.server
   ```
   Install from `http://<your computer>:8000/<name>.pxa` with Developer Mode on. Run it for a few minutes: the knob,
   the phone page, closing and reopening it.
4. **Ask for changes** in the review if needed, or approve and merge.

## Publishing

After merging, on `main`:

```
git pull
python3 tools/publish.py publish
python3 tools/publish.py verify
git add -A apps catalogue && git commit -m "Publish <app> <version>" && git push
```

`publish`:

- gives each new app the next free catalogue number (100/0 to 199/255) and writes it into its `app.json`;
- builds each app, and signs each version not yet published into `catalogue/packages/<id>/<version>.pxa`. A
  published version is never signed again or changed;
- copies the icons, writes `catalogue/index.json`, raises its sequence number, and signs it (`index.sig`).

Then comment on the pull request, such as "Published as 1.0.0, number 100/3". Pixlpal owners see it in the phone
app's store within a few minutes of the push, as GitHub's file server refreshes.

## Withdrawing an app

When an app is harmful, or a version has a serious bug:

```
python3 tools/publish.py withdraw <id> --reason "Shows the wrong time after midnight" [--versions 1.2.0]
python3 tools/publish.py verify
git add -A catalogue && git commit -m "Withdraw <id>" && git push
```

- Without `--versions`, every version is withdrawn.
- The reason is shown on Pixlpal and in the phone app, so write it for owners.
- This signs a new `catalogue/withdrawn.json`. Pixlpals fetch it once a day while online, and whenever they install
  from the store. From then on they refuse to install or open what it names. The store offers the newest version
  that isn't withdrawn, if there is one, and Pixlpal lets owners go back to it.
- Each list has a higher sequence number, and Pixlpal refuses a list older than one it has seen, so an old list
  can't be served to undo a withdrawal.

To undo a withdrawal: `python3 tools/publish.py restore <id>`, then verify, commit and push.

## Changing the API or the tools

The firmware repository holds the originals of `sdk/pixlpal.h` and `tools/` (`build.py`, `pxa.py`,
`wasm_info.py`): its `components/meterbit_wasm/`. After changing them there, copy them here in the same pull
request round.

The API only ever grows. Once apps are published, a firmware update must keep every existing function working the
same way. New functions raise the API version (`"api"` in `app.json`). The store then hides apps that need a newer
version from Pixlpals that don't have it.

## If the key is ever exposed

Anyone with the key can make apps every Pixlpal trusts, so act at once:

1. Make a new key, and release firmware whose `mtb_app_trust.h` holds the new public half
   (`tools/pxa.py trust NEW_KEY -o include/mtb_app_trust.h` in the firmware repository).
2. Replace `catalogue/public_key.pem`, then re-sign every package and the lists with the new key. The packages must
   be signed again: delete `catalogue/packages/` and run `publish`.
3. Pixlpals that update their firmware trust only the new key. Apps signed with the old one stop opening on them
   and must be installed again from the store.
