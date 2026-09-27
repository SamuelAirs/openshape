# OpenShape privacy policy

_Last updated: 27 September 2026. Applies to OpenShape for iPhone and iPad
(App Store and TestFlight) and OpenShape for Windows, version 0.2 and
later._

**In short: OpenShape does not collect, send or sell any information about
you.** It has no account, no advertising, no analytics, no tracking and no
crash reporter of its own, and it never connects to the internet by
itself. Everything you make stays on your device, in files you control.

## What OpenShape keeps, and where

OpenShape stores only what it needs to work, on your own device:

| What | iPhone and iPad | Windows |
|---|---|---|
| Your projects (`.openshape`) and exports (STL, 3MF, STEP) | OpenShape's folder, visible in the Files app (*On My iPhone / On My iPad → OpenShape*; exports in its *Exports* folder), or wherever you open a project from | wherever you save them |
| Recovery copies of unsaved work (so a crash does not lose it) | the app's private data folder (*Library/Application Support*) | `%LOCALAPPDATA%\OpenShape\OpenShape\recovery\` |
| A log of what the app did, for troubleshooting: the version, the graphics chip it draws with, the names and folders of files you opened, saved or exported, the steps you applied and the messages it showed you | *OpenShape → Logs → openshape.log* in the Files app | `%LOCALAPPDATA%\OpenShape\OpenShape\logs\openshape.log` |
| Settings: preferences, the list of recent projects, the view's projection, on Windows the window's position | the app's private settings (*Library/Preferences*) | the registry, `HKEY_CURRENT_USER\Software\OpenShape\OpenShape` |

- A project file contains your model, its history, a small preview picture
  of it and the OpenShape version that saved it. Exports (STL, 3MF, STEP)
  contain the shapes and, depending on the format, the names of your bodies
  and of the program that wrote them; a STEP file also records the date and
  time it was written. None of them contain your name, your device's name or where
  you are.
- A recovery copy is deleted when you save, choose **Don't Save**, or
  choose **Discard** after a crash. The log starts over when it reaches
  4 MB.
- OpenShape reads only the files you open (or pick in the system's file
  picker) and the files in its own folders. It does not use your
  location, camera, microphone, photos, contacts or any other personal
  data, and it does not ask for any of these permissions.

**Deleting your data:** delete a project or export like any other file (in
the Files app or Explorer). Deleting the OpenShape app on iPhone or iPad
removes everything in its folders, including projects you saved there,
so move projects you want to keep elsewhere first (for example to iCloud
Drive in the Files app). On Windows, uninstalling keeps your projects and
the `%LOCALAPPDATA%\OpenShape` folder; delete that folder to remove the
recovery copies, the log and (with the registry key above) the settings.

Your device may include OpenShape's folders in its own backups (iCloud or
computer backups on iPhone and iPad), as it does for other apps; that is
between you and your device's backup service, and the OpenShape project
never receives them.

## Network access

OpenShape does not contain any code that connects to the internet. The only
time it goes online is when **you tap or click a link** (the user guide in
the help card, and the license, source code, third-party license and
privacy policy links in **About**): the link opens in your web browser
(Safari on iPhone and iPad), and that website's own privacy policy applies
(GitHub for the guide, the source code and this policy, mozilla.org for the
license text).

The app does not check for updates. On iPhone and iPad, updates come from
the App Store or TestFlight; on Windows, you download new versions
yourself from the project's Releases page.

## Information Apple shares with the developer

OpenShape on iPhone and iPad is distributed by Apple. Apple, not OpenShape,
handles your purchase and your Apple Account, under
[Apple's privacy policy](https://www.apple.com/legal/privacy/). What
reaches the developer through Apple is limited to the following:

- **Sales figures.** App Store Connect shows the developer how many copies
  were sold and downloaded, by country or region: numbers, not names.
- **App Store crash reports and usage statistics**, only if you have turned
  on *Settings → Privacy & Security → Analytics & Improvements → Share With
  App Developers*. Apple collects them and passes them on without your
  name; turn the setting off to stop it
  ([Apple: App Analytics](https://developer.apple.com/app-store/app-analytics/)).
- **TestFlight beta testing.** If you test a beta version through
  TestFlight, Apple shares with the developer: crash reports, how many
  sessions and crashes you had, when you installed it and which build, and
  any feedback (comments and screenshots) you choose to send from TestFlight.
  If you were invited by email, Apple also shares your name and email
  address; if you joined through a public link, your name and email address
  are hidden from the developer ([Apple: TestFlight](https://testflight.apple.com/)).

The developer uses this information only to find and fix problems in
OpenShape, does not combine it with anything else, and never sells or
passes it on. Crash reports and feedback stay in App Store Connect (Apple
keeps them for as long as Apple's own retention allows); anything the
developer copies out of them into a bug report is limited to what is needed
to fix the bug, with no names or email addresses.

On Windows, if OpenShape crashes, Windows' own error reporting may send a
report to Microsoft, depending on your Windows settings; OpenShape does not
receive those reports.

## When you contact us

If you report a problem or ask a question, you decide what to share:

- **GitHub issues** (https://github.com/SamuelAirs/openshape/issues) are
  public: everything you post, including attached projects, screenshots or
  logs, can be read by anyone, and
  [GitHub's privacy statement](https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement)
  applies. Look through a log before you attach it: it lists the names and
  folders of files you opened.
- **Email** (below): used only to answer you, and deleted when it is no
  longer needed for that.

## Children

OpenShape is suitable for all ages and collects no information from
anyone, children included.

## Changes to this policy

If what OpenShape does with information ever changes, this page changes
first, with a new date at the top; its full history is public at
https://github.com/SamuelAirs/openshape/commits/main/docs/PRIVACY.md.
A change that would send any information off your device would be
described here and asked of you in the app before it happened.

## Contact

- Questions and problems: https://github.com/SamuelAirs/openshape/issues
- Privacy questions by email: **[OWNER: add a contact email address here before submitting to the App Store]**

OpenShape is open source (Mozilla Public License 2.0): anyone can read its
code at https://github.com/SamuelAirs/openshape and check what this policy
says.
