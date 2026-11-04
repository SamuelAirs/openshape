# FoldForm privacy policy

_Last updated: 4 November 2026 (Measure: the camera, photos and motion sensors, used on your device only). Applies to FoldForm for iPhone and iPad
(App Store and TestFlight) and FoldForm for Windows, version 0.2 and
later._

**In short: FoldForm does not collect, send or sell any information about
you.** It has no account, no advertising, no analytics, no tracking and no
crash reporter of its own, and it never connects to the internet by
itself. Everything you make stays on your device, in files you control.

## What FoldForm keeps, and where

FoldForm stores only what it needs to work, on your own device:

| What | iPhone and iPad | Windows |
|---|---|---|
| Your projects (`.openshape`) and exports (STL, 3MF, STEP) | FoldForm's folder, visible in the Files app (*On My iPhone / On My iPad → FoldForm*; exports in its *Exports* folder). A project you open from elsewhere (iCloud Drive, another app, a Mail attachment) is copied into this folder and the copy is what FoldForm changes | wherever you save them |
| Recovery copies of unsaved work (so a crash does not lose it) | the app's private data folder (*Library/Application Support*) | `%LOCALAPPDATA%\OpenShape\OpenShape\recovery\` |
| A log of what the app did, for troubleshooting: the version, the graphics chip it draws with, the names and folders of files you opened, saved, exported or shared (and the kind of destination you picked in the share sheet, such as AirDrop or Mail, never the recipient), the steps you applied and the messages it showed you | *FoldForm → Logs → openshape.log* in the Files app | `%LOCALAPPDATA%\OpenShape\OpenShape\logs\openshape.log` |
| Settings: preferences, the list of recent projects, the view's projection, on Windows the window's position | the app's private settings (*Library/Preferences*) | the registry, `HKEY_CURRENT_USER\Software\OpenShape\OpenShape` |

- A project file contains your model, its history, a small preview picture
  of it and the FoldForm version that saved it. Exports (STL, 3MF, STEP)
  contain the shapes and, depending on the format, the names of your bodies
  and of the program that wrote them; a STEP file also records the date and
  time it was written. None of them contain your name, your device's name or where
  you are.
- A recovery copy is deleted when you save, choose **Don't Save**, or
  choose **Discard** after a crash. The log starts over when it reaches
  4 MB.
- A STEP file you open in FoldForm from another app is read from a
  temporary copy that is deleted after the import.
- FoldForm reads only the files you open (or pick in the system's file
  picker, or send to FoldForm from the Files app or Mail) and the files in
  its own folders. It does not use your location, microphone, contacts or
  any other personal data. The camera, your photos and the motion sensors
  are used only by **Measure**, only when you use it (next section).

## Measure: the camera, photos and sensors (version 0.4 and later)

FoldForm's **Measure** tools measure real things with your device. All of
it happens on your device; nothing is uploaded, kept by us or shared:

- **Photo**: a photo you take with the camera or choose from your photo
  library is copied into FoldForm's temporary folder while you measure on
  it, and iOS may delete that folder at any time. FoldForm reads only the
  photo you pick (the system's photo picker shows your library; FoldForm
  sees nothing else in it). iOS asks once whether FoldForm may use the
  camera; you can change that in *Settings → FoldForm*. On iPhone and iPad,
  Apple's Vision framework finds the reference card or sheet of paper in
  the photo on your device.
- **Camera (AR)**: Apple's ARKit uses the camera (and the LiDAR scanner,
  where there is one) to find surfaces while the AR view is open. FoldForm
  keeps only the points you place and the lengths between them; no camera
  image is stored.
- **Fold** and **Level**: the angle of the iPhone Duo's hinge and the
  direction of gravity from the motion sensors are read only while those
  tools are open, to show angles; they are not stored (iOS needs no
  permission for them).
- **Measurements** you keep (a name, a value, which tool made it) and the
  scale you checked with a card are stored in FoldForm's settings on your
  device.

**Deleting your data:** delete a project or export like any other file (in
the Files app or Explorer). Deleting the FoldForm app on iPhone or iPad
removes everything in its folders, including projects you saved there,
so move projects you want to keep elsewhere first (for example to iCloud
Drive in the Files app). On Windows, uninstalling keeps your projects and
the `%LOCALAPPDATA%\OpenShape` folder; delete that folder to remove the
recovery copies, the log and (with the registry key above) the settings.

Your device may include FoldForm's folders in its own backups (iCloud or
computer backups on iPhone and iPad), as it does for other apps; that is
between you and your device's backup service, and the FoldForm project
never receives them.

## Sharing files

On iPhone and iPad, **Export** and **File → Share Project…** open the
system's share sheet with that one file. Where it goes (AirDrop, Mail,
Messages, Save to Files, a slicer app) is your choice, and iOS and the
app you pick deliver it, not FoldForm; FoldForm learns only whether the
file was sent and the kind of destination, which it writes to its log on
your device. Closing the share sheet sends nothing.

## The purchase "Unlimited projects" (iPhone and iPad)

FoldForm from the App Store is free for one project; a one-time in-app
purchase unlocks unlimited projects. Apple handles the purchase entirely:
the price, the payment, your Apple Account, Ask to Buy and Restore
purchase go through Apple's App Store, under
[Apple's privacy policy](https://www.apple.com/legal/privacy/), and
FoldForm never sees your payment details or who you are. When you open the
purchase sheet, FoldForm asks the App Store for the price; that request and
the purchase itself are made by your device's App Store service, not by
FoldForm's own code. All FoldForm keeps is an "unlocked" flag in its
settings on your device (and a line in its log on your device saying that
a purchase or restore happened). It sends nothing anywhere: still no data
is collected.

## Network access

FoldForm's own code never connects to the internet (the libraries it is
built with include networking functions, which FoldForm does not use). The
only time anything goes online is when **you tap or click a link** (the user guide in
the help card, and the license, source code, third-party license and
privacy policy links in **About**): the link opens in your web browser
(Safari on iPhone and iPad), and that website's own privacy policy applies
(GitHub for the guide, the source code and this policy, mozilla.org for the
license text).

The app does not check for updates. On iPhone and iPad, updates come from
the App Store or TestFlight; on Windows, you download new versions
yourself from the project's Releases page.

## Information Apple shares with the developer

FoldForm on iPhone and iPad is distributed by Apple. Apple, not FoldForm,
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
FoldForm, does not combine it with anything else, and never sells or
passes it on. Crash reports and feedback stay in App Store Connect (Apple
keeps them for as long as Apple's own retention allows); anything the
developer copies out of them into a bug report is limited to what is needed
to fix the bug, with no names or email addresses.

On Windows, if FoldForm crashes, Windows' own error reporting may send a
report to Microsoft, depending on your Windows settings; FoldForm does not
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

FoldForm is suitable for all ages and collects no information from
anyone, children included.

## Changes to this policy

If what FoldForm does with information ever changes, this page changes
first, with a new date at the top; its full history is public at
https://github.com/SamuelAirs/openshape/commits/main/docs/PRIVACY.md.
A change that would send any information off your device would be
described here and asked of you in the app before it happened.

## Contact

- Questions and problems: https://github.com/SamuelAirs/openshape/issues
- Privacy questions by email: ayers177@gmail.com

FoldForm is open source (Mozilla Public License 2.0): anyone can read its
code at https://github.com/SamuelAirs/openshape and check what this policy
says.
