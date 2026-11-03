# OpenShape on the App Store: the owner's checklist

> **2026-10-31: the owner's new decisions.** The app is **free** with every
> tool for **one project**, and a one-time in-app purchase, **Unlimited
> Projects** (about USD 10), unlocks unlimited projects. It goes to App
> Review directly; there is no public beta first. The order of today's steps
> is in [LAUNCH_CHECKLIST.md](LAUNCH_CHECKLIST.md). Also decided that day: the
> product is called **FoldForm** (section 0), version **0.3.0**, contact
> email **ayers177@gmail.com** (privacy policy, support page). Sections 4 and 5 and
> the description below are updated for this; the timeline in section 7
> is history. The build to submit is **build 54** (tag `v0.3.0`, section 6).
> Apple's pages for screenshots, the DSA, the Small Business Program, in-app
> purchases and guidelines 2.3, 3.1.1 and 5.1.1 were read again on
> 2026-10-31 and the steps below follow them.

_Written 2026-09-27 for the owner's decisions of that day: OpenShape goes on
the App Store as a **paid** app, after a **public TestFlight beta**, in the
store around or before **2026-10-23** (the foldable iPhone Duo launch). Every
fact about Apple's rules was checked that day against the Apple pages listed
under [Sources](#sources); Apple changes App Store Connect often, so if a
button has moved, the step is still the same._

Everything happens in **App Store Connect** (appstoreconnect.apple.com),
signed in with the Apple Account of the developer program membership.
Nothing here needs a Mac. Each item is marked:

- **DONE**: already in the repository (nothing to do).
- **YOU**: you do it by hand, in App Store Connect or a browser.
- **DECIDE**: a product decision only you can make; a recommendation is given.
- **TRACK**: done by a separate work track (the licensing texts,
  docs/LICENSING.md). Listed so nothing is forgotten; not repeated here.

Already on `main` since 2026-09-27 (merged before this checklist): the
share sheet after exports and **File → Share Project…**, *Open in
OpenShape* from the Files app and Mail, debug symbols (dSYMs) with every
upload, and uploads that may go to external testers (the public beta).

The app record, bundle ID `io.github.samuelairs.openshape`, the API key and
the TestFlight upload from GitHub already exist (docs/IPAD.md, section 1).

## Contents

0. [First: the name](#0-first-the-name)
1. [Agreements, tax, banking and the Small Business Program](#1-agreements-tax-banking-and-the-small-business-program)
2. [App information](#2-app-information)
3. [The listing text](#3-the-listing-text)
4. [Pricing](#4-pricing)
5. [The public beta (TestFlight)](#5-the-public-beta-testflight)
6. [Submitting to the App Store](#6-submitting-to-the-app-store)
7. [Timeline to 2026-10-23](#7-timeline-to-2026-10-23)
8. [Sources](#sources)

---

## 0. First: the name

**DECIDED 2026-10-31: FoldForm.** No App Store app of that name, and no
trademark "FoldForm" in a web search that day (the iTunes Search API for
`FoldForm` returned origami and form apps only). Recommended App Store
name: `FoldForm: CAD for 3D Printing` (29 characters; the words after the
colon help search), or plain `FoldForm`; the home-screen name is
`FoldForm`. The code, repository, executable and file extension keep the
name OpenShape (docs/TECHNICAL_DEBT.md). The research below is how the
name question came up.

**DECIDE, before anything is submitted.** The name is the hardest thing to
change once the app is on sale (ratings, links and search history stay with
the listing, but people will have learned the old name). What a check of
the App Store and the web found on 2026-09-27:

| Who | What | Why it matters |
|---|---|---|
| **OpenShape3D** by Laan Labs (Laan Consulting Corp) | On the App Store since 2026-08-06 (version 1.3, 2026-09-18), **free**, "a direct-modeling CAD app built for touch" for iPhone and iPad, on OpenCASCADE, **open source** (MIT, github.com/laanlabs/openshape3d, openshape3d.com) | Almost the same name, the same kind of app, on the same devices, for free, and on sale first. People searching "OpenShape" will find both; some will buy the wrong one or ask why they should pay. Its maker could object (App Review guidelines 4.1 and 5.2.1 forbid names that copy another developer's product), and has the earlier use. |
| **Open—Shape** by Michael Hix | On the App Store since 2026-08-25: a guitar chord app whose description calls itself "OpenShape"; bundle ID `com.openshape.app` | A different field, but its "Open—Shape" suggests the plain name "OpenShape" was already taken in App Store Connect by August 2026 (App Store names are unique). Check what name your own app record has (App Store Connect → OpenShape → App Information → Name). |
| **Onshape** (PTC Inc.) | Cloud CAD; "ONSHAPE" is a US registered trademark of PTC for CAD software (registrations 4752666 and 5949177) | One letter pair apart ("Onshape" / "OpenShape"), same field. Not an App Store name clash, but a trademark owner in the same market could see a likelihood of confusion. |
| openshape.com | "OpenShape software solutions", a company making custom software and FileMaker solutions | Different field; low risk. |
| OpenShape (UC San Diego, 2023) | A research method for 3D shape recognition (machine learning) | Different field; it shares search results. |

No registered trademark "OpenShape" turned up in the search (a proper search
is at the USPTO, tmsearch.uspto.gov, and EUIPO; this is not legal advice).

**Recommendation.** For a *paid* app, do not launch under a name that is one
character away from a free, open-source app that does the same thing. Two
ways, in order of preference:

1. **Rename the product before launch** (a new App Store name, display name
   and README; the bundle ID, repository and file extension can stay). Pick a
   name that is free in the App Store search
   (`https://itunes.apple.com/search?term=<name>&entity=software`) and at the
   USPTO. Examples that returned no app of that name on 2026-09-27:
   *Shapewright*, *MakerShape* (examples of the kind of name, not checked as
   trademarks). The lead engineer does the renaming in the code and
   documents once you pick one.
2. **Keep OpenShape, but distinguish it in the store name**, e.g.
   `OpenShape: CAD for 3D Printing` (exactly 30 characters, the maximum) with
   a subtitle that says what it is, and keep the GitHub link and the open
   source line in the description so people can tell the two apart. This
   keeps the confusion with OpenShape3D; accept that risk knowingly.

The texts to paste below say FoldForm (since 2026-10-31); the rest of this
document still says OpenShape where it describes the code.

---

## 1. Agreements, tax, banking and the Small Business Program

**DONE by the owner (2026-10-31, about 23:05):** the Paid Apps Agreement is
**Active** (tax form and bank account in), the Small Business Program
enrollment is sent, and the DSA status is **not a trader**, with the 27 EU
countries left out of the availability (item 5, section 4). The steps
below stay as the record of how.

A paid app can be priced, and an in-app purchase sold (also in a free
app, as OpenShape is since 2026-10-31), only once the **Paid Apps
Agreement** is active, and it becomes active only after the tax and bank
details are in. Start
this first: Apple checks the details, and it can take days.

1. **YOU: accept the Paid Apps Agreement.** App Store Connect → **Business**
   → **Agreements** tab → the *Paid Apps* row → **View and Agree to Terms**
   (two-factor code) → read → **Agree**. Only the Account Holder can do this,
   and it cannot be undone.
2. **YOU: tax forms.** Business → Agreements → **Tax Forms** → **Add Tax
   Info**. A US person fills in the **W-9** (Social Security number or EIN);
   someone outside the US fills in **W-8BEN** (individuals) and possibly
   forms for their country (for example an Australian ABN, a Canadian GST
   number). Once submitted it cannot be edited in App Store Connect (Apple
   support has to change it), so check every field.
3. **YOU: bank account.** Business → Agreements → Bank Accounts → add the
   account that should receive the proceeds. Apple pays within 45 days of
   the end of the fiscal month in which the sales were made, once the
   proceeds of a country or region pass its minimum payment threshold (smaller
   amounts carry over to the next month).
4. **YOU: the App Store Small Business Program** (reduces Apple's commission
   from 30% to **15%**): developer.apple.com/app-store/small-business-program
   → **Enroll now**. It is for developers with up to USD 1 million in proceeds in
   the previous calendar year (new developers qualify). Needs: the Account
   Holder, the accepted Paid Apps Agreement, and a list of any associated
   developer accounts (none). The lower rate applies from 15 days after the
   end of the fiscal month in which Apple approves the enrollment, so enroll
   now, weeks before the first sale. It stays until your proceeds pass USD 1
   million in a year.
5. **DONE (2026-10-31): not a trader, the EU left out.** EU Digital
   Services Act trader status. Every developer
   must declare a status, **also one who leaves the EU out** (Apple asks for
   it when a new app is submitted): Business → Agreements → Compliance →
   **Complete Compliance Requirements** next to *Digital Services Act* →
   "This is a trader account." or "This is not a trader account." (Account
   Holder or Admin; an app's own status can be changed later under App
   Information → App Store Regulations and Permits). Apple's signs of a
   trader include making revenue from the app, **for example In-App
   Purchases**. A trader who is an individual gives an **address (or P.O.
   box), a phone number and an email address, shown publicly on the App
   Store pages in the 27 EU countries**, verified by codes and a document
   (and proof of the P.O. box). So decide one of:
   - **trader, EU included:** a **P.O. box** and a phone number and email
     you are happy to publish (verification can take Apple some time: start
     now), or
   - **EU left out** of the availability (section 4) and "This is not a
     trader account": Apple's page says a developer who distributes on the
     App Store only outside the EU is not acting as a trader there. Do not
     choose "not a trader" while selling in the EU.

   The owner chose the second. If FoldForm should ever be sold in the EU,
   the status becomes "trader" first (with the public contact details).
6. **DONE: export compliance.** `src/app/ios/Info.plist.in` sets
   `ITSAppUsesNonExemptEncryption` to `false`, so App Store Connect does not
   ask the encryption questions for each build. This is true for OpenShape:
   its own code makes no network connections and encrypts nothing (Qt's
   networking library is in the bundle only because Qt's QML engine depends
   on it; OpenShape never calls it and its QML loads only files inside the
   app), and the ZIP library in project files is
   built without any encryption (`scripts/ios/build-deps.sh`:
   `-DENABLE_COMMONCRYPTO=OFF -DENABLE_GNUTLS=OFF -DENABLE_MBEDTLS=OFF
   -DENABLE_OPENSSL=OFF`).

---

## 2. App information

App Store Connect → Apps → your app → **App Information** (general) and
the version page (**iOS App → 0.3.0 Prepare for Submission**).

| Field | What to enter | State |
|---|---|---|
| Name (max 30) | `FoldForm: CAD for 3D Printing` (or `FoldForm`; section 0) | **YOU** |
| Subtitle (max 30) | `Parametric parts, STL and 3MF` (29; the marked block in section 3; until 2026-11-03 `Precise modeling for makers`, which carried no search term) | **YOU** (paste) |
| Bundle ID | `io.github.samuelairs.openshape` (set by the app record; cannot change after a build was uploaded) | **DONE** |
| SKU | `openshape` (internal only, set when the record was made) | **DONE** |
| Primary language | English (U.S.) | **YOU** (check) |
| Primary category | **Graphics & Design** ("tools for art, design and graphics creation"; where makers browse SketchUp, Nomad Sculpt, uMake and 3D-print design apps) | **YOU** |
| Secondary category | **Productivity** (where Shapr3D, Onshape and Autodesk Fusion are) | **YOU** |
| Content rights | "Does your app contain, show, or access third-party content?" **Yes** → "I have the necessary rights": the bundled font (Noto Sans, SIL Open Font License) and libraries are used under their licenses (THIRD_PARTY.md). The app shows no other third-party content. | **YOU** |
| Age rating | the questionnaire below → **4+** | **YOU** |
| Made for Kids | No | **YOU** |
| Privacy Policy URL | `https://github.com/SamuelAirs/openshape/blob/main/docs/PRIVACY.md` (live; fetched 2026-10-31, with the purchase section and the contact email) | **DONE** (page) / **YOU** (paste) |
| User Privacy Choices URL | leave empty (optional; there is nothing to choose) | — |
| Support URL | `https://github.com/SamuelAirs/openshape/blob/main/docs/SUPPORT.md` (how to get help, with the contact routes and the email; Apple requires a page with real contact information; live, fetched 2026-10-31) | **DONE** (page) / **YOU** (paste) |
| Marketing URL | `https://github.com/SamuelAirs/openshape` (optional) | **YOU** |
| Copyright | `2026 Sam Ayers` (App Store Connect adds the ©; use your legal name if you prefer) | **YOU** |
| License agreement | **Apple's standard EULA** (the default: leave it as it is). Decided 2026-10-31; [EULA.md](EULA.md) is not used, because a custom EULA would publish the owner's address and phone. | **DONE** |
| Routing app coverage file | none (not a navigation app) | — |
| Accessibility (Nutrition Labels) | optional; leave empty for now: nothing has been checked with VoiceOver, and a label must not claim what was not tested | — |

### Age rating answers

App Store Connect → App Information → Age Rating → **Edit**. OpenShape is a
design tool with no content of its own, no web browser, no chat and no
accounts. Answer every question **None** or **No**:

| Section | Question (as grouped by Apple) | Answer |
|---|---|---|
| In-App Controls | Parental Controls; Age Assurance | No; No |
| Capabilities | Unrestricted Web Access | **No** (links in Help and About open Safari, outside the app; there is no web view) |
| | User-Generated Content | **No** (there is no place in the app where people see each other's content; a file sent with the system share sheet goes where the user sends it, like a photo from Files) |
| | Social Media; Messaging and Chat; Advertising | No; No; No |
| Mature Themes | Profanity or Crude Humor; Horror/Fear Themes; Alcohol, Tobacco, or Drug Use or References | None |
| Medical or Wellness | Medical or Treatment Information; Health or Wellness Topics | None / No |
| Sexuality or Nudity | all three | None |
| Violence | Cartoon or Fantasy; Realistic; Prolonged Graphic or Sadistic; Guns or Other Weapons | None |
| Chance-Based Activities | Gambling; Simulated Gambling; Contests; Loot Boxes | None / No |

Result: **4+**.

### App Privacy ("nutrition label")

App Store Connect → Apps → your app → **App Privacy** → **Get Started** →
**"No, we do not collect data from this app"** → **Save** → **Publish**.
Then **Privacy Policy → Edit** and paste the privacy policy URL.

**DONE: why "Data Not Collected" is true** (checked in the code on
2026-09-27; docs/PRIVACY.md says the same in plain words):

- Apple defines "collect" as sending data off the device so that the
  developer or a partner can keep it beyond the request. OpenShape has **no
  network code of its own**: no `QNetworkAccessManager`, sockets or
  `URLSession` anywhere in `src/`; the app asks CMake only for Qt Core, Gui,
  Qml, Quick and QuickControls2 (Qt Network comes along as a dependency of
  Qt Qml, which could fetch `http` URLs named in QML, but OpenShape's QML
  names only files compiled into the app, and the only web addresses in it
  are the links handed to Safari);
  no analytics, crash-reporting or advertising SDK. The only way out is a
  link the user taps (Help: the user guide; About: license, source code,
  THIRD_PARTY.md, the privacy policy), which opens Safari.
- Projects, exports, recovery copies, the log and the settings stay on the
  device (docs/PRIVACY.md lists where).
- Crash reports and TestFlight feedback are collected by **Apple** and shown
  to the developer in App Store Connect; Apple says developers do not
  disclose data Apple collects.
- The privacy manifest `src/app/ios/PrivacyInfo.xcprivacy` already says: no
  tracking, no tracking domains, no collected data types, and lists the
  "required reason" APIs Qt and OpenCASCADE use (file timestamps, disk space,
  system boot time) with their reasons. **DONE.**
- Guideline 5.1.1 wants the policy linked inside the app too: **File →
  About FoldForm → "Privacy policy: FoldForm collects no data about you"**
  (checked by the `privacy` acceptance scenario, on a desktop and a phone
  layout). **DONE.**
- Rechecked in the code of the `v0.3.0` tag (2026-10-31): no networking
  class, socket, `URLSession` or analytics, crash-reporting or advertising
  SDK anywhere in `src/`; the only web addresses are the GitHub and
  mozilla.org links handed to Safari (`Qt.openUrlExternally`); StoreKit
  asks Apple for the price and makes the purchase. **DONE.**

**DONE:** docs/PRIVACY.md and docs/SUPPORT.md give the contact address
ayers177@gmail.com, and both pages are live on `main` (guideline 2.1: no
placeholder text).

---

## 3. The listing text

Paste these into the version page (English (U.S.)). Their lengths are
checked by `python scripts/dev/check_appstore_texts.py` (run it after
editing a block below; limits from App Store Connect's reference). The
listing tool on this PC reads the same marked blocks (`<!-- appstore:...
-->`).

**Changed on 2026-11-03** (each marked `changed 2026-11-03` above its
block; re-apply them): the subtitle (new block below), the description
(the iPhone and iPad header, the Apple Pencil line and the iPhone Duo
line), the keywords, What's New (one real sentence instead of the
examples) and the featuring nomination (new blocks, section 3b). Why: the
outside critique (docs/CRITIQUE_2026-11-03.md, section 4, items 1, 2 and
10): "Made for iPhone Duo" is the wording of Apple's own badge, the palm
rejection had not been tried on a device, and the keywords repeated words
the name already indexes.

### Subtitle (max 30 characters)

Words in the subtitle are searchable, so it carries search terms that are
not in the name and not in the keywords.

<!-- changed 2026-11-03: re-apply this block -->
<!-- appstore:subtitle max-chars=30 -->
```text
Parametric parts, STL and 3MF
```

### Promotional text (max 170 characters; can be changed any time without review)

<!-- appstore:promotional-text max-chars=170 -->
```text
Design parts for your 3D printer by touch: push and pull faces, sketch with constraints, type exact sizes, then export STL or 3MF for your slicer.
```

### Description (max 4,000 characters)

Accurate to what version 0.3.0 does (the README's feature list and
CHANGELOG.md, checked against the code on 2026-10-31). Update it when a feature changes (guideline 2.3).

<!-- changed 2026-11-03: re-apply this block -->
<!-- appstore:description max-chars=4000 -->
```text
FoldForm is precise 3D CAD for makers and 3D printing, made for touch. Push and pull faces with a finger or Apple Pencil, sketch a profile and extrude it, and type exact sizes as you go. Every step stays editable.

MODEL
• Tap a flat face to see the part's size, then drag the arrow or type a new size; +5 or -5 changes it by that much.
• Round and chamfer edges, shell a body to a wall thickness, offset faces.
• Tap a hole's wall and type a new diameter. Delete a hole, fillet or chamfer and the faces around it close the gap.
• Extrude sketch profiles (a distance, both ways or up to a face; as a new body, joined, or cut, also through everything), revolve them, or loft between shapes on different planes.
• Sizes accept units and arithmetic: 25, 1in, 2.5cm, 20+5, (10+2)*3.

SKETCH
• Sketch on the ground, an origin plane, a construction plane or any flat face.
• Line, rectangle, center rectangle, polygon, circle, arc, tangent arc and slot; trim, round a corner, offset, mirror and pattern.
• Constraints (horizontal, vertical, parallel, perpendicular, equal, tangent, concentric, midpoint, symmetric, distances, angles) shown as small glyphs, and a "Fully defined" status.
• Drag a line, circle or point to change a sketch; tap a size to type it. On a face, points snap to its corners, edge middles, center and holes, with a magnifier under your finger.

BODIES
• Move, rotate and align bodies, lay a face on the build plate, mirror and pattern (in a row or around an axis), union, subtract and intersect.
• Construction axes and planes to rotate about, pattern around, mirror across, align onto or sketch on.
• The Model panel lists every sketch, body and step: change a value later, suppress or delete a step. Everything can be undone.

MADE FOR 3D PRINTING
• A Hole tool with ISO clearance and tap sizes, counterbores and countersinks from M2 to M6, with a print allowance (0.2 mm to start with) so screws fit, plus heat-set insert holes.
• Raised or engraved text on a face, for labels and version marks.
• Measure a face's size, the distance or gap between two faces or edges, and angles.
• Export STL or 3MF (millimeters; 3MF keeps each body a separate object) for your slicer, or STEP for other CAD programs. Import STEP files.

YOUR FILES
• Projects keep the full history, so sketches and steps stay editable after reopening.
• Export opens the share sheet: send the file straight to your slicer app, AirDrop or Mail. Share a whole project the same way.
• Projects are saved in FoldForm's folder in the Files app, exports in its Exports folder. Open projects and STEP files from the Files app or Mail.
• A Home screen with your projects and their previews.
• Unsaved work is kept safe in the background and offered again if the app closes.
• When a step cannot be done, FoldForm says why in plain words, often with a size that works, and your model stays as it was.

ON IPHONE AND IPAD
• One app for iPhone and iPad. On iPhone the tools sit along the bottom and the value box docks where it covers nothing you selected; on iPad the full layout, also in Split View and Stage Manager.
• One finger orbits, two fingers pan and zoom, a two-finger tap undoes and a three-finger tap redoes. A view cube shows how the view is turned; tap it to look from a side.
• Apple Pencil on iPad: pen mode lets the Pencil draw and select while your fingers only move the view.
• On iPhone Duo: closed, the phone layout; opened, the full layout on the inner display, with a larger canvas for your model.

FREE TO START
Every tool is free for one project at a time. A one-time in-app purchase unlocks unlimited projects on your iPhone and iPad.

OPEN SOURCE, NO ACCOUNT
FoldForm is open-source software (Mozilla Public License 2.0) using Qt, Open CASCADE Technology and PlaneGCS under the GNU LGPL. Source code, license texts, a user guide and a Windows version: github.com/SamuelAirs/openshape. No account, no advertising, no tracking: FoldForm collects no data.
```

Notes on the description:
- It mentions the Windows version once, at the end, because it is part of
  what OpenShape is. Guideline 2.3.10 forbids naming *other mobile
  platforms* and asks metadata to stay focused on the app; Windows is not a
  mobile platform, but if App Review objects, delete "a Windows version" and
  resubmit (no new build needed).
- No prices or "pay once" wording anywhere in the metadata (guideline 2.3.7
  forbids pricing information in names, subtitles, screenshots and
  previews; the price is shown by the App Store itself).
- The share sheet and "Open projects ... from the Files app or Mail" lines
  (and step 6 of the review notes) have not been seen working on a device
  yet: check them on build 54 before submitting (LAUNCH_CHECKLIST.md, step
  8). If one fails, take its line out here and in the review notes; that
  needs no new build.
- The last paragraph names the LGPL libraries, as the release steps in
  docs/LICENSING.md ask of the App Store description.
- "FREE TO START" says which part needs the purchase (guideline 2.3.2:
  the description must say what requires an in-app purchase).
- The iPhone Duo line says only what the layout does (the phone layout on
  the outer display, the regular one on the inner display), not Apple's
  badge wording "Made for iPhone Duo" (the badge comes from the Duo
  screenshots), and nothing about the Apple Pencil on the Duo (Apple has
  not enabled it there yet) or the screen edges (estimates until the
  owner's log, TD-150). The owner confirms it on his Duo before the
  listing is applied. The Apple Pencil line no longer promises palm
  rejection, which has not been tried on the iPad yet.

### Keywords (max 100 bytes, comma-separated, no spaces)

Each keyword must be longer than two characters (so not "3d"); the app
name (`FoldForm: CAD for 3D Printing`), the company name and the subtitle
(`Parametric parts, STL and 3MF`) are searchable already, so none of their
words is repeated (no cad, print, printing, parametric, part, stl, 3mf);
no "app" or "free"; no names of other apps or companies (guideline
2.3.7). The words are what makers search for when they need a part:
the kind of part, the hardware it holds, the tool they measure with.

<!-- changed 2026-11-03: re-apply this block -->
<!-- appstore:keywords max-bytes=100 -->
```text
modeling,design,sketch,step,enclosure,bracket,caliper,mount,spacer,knob,clip,insert,hole,screw,jig
```

### What's New

App Store Connect asks for it only from the second version on; if it
shows the field for 0.3.0, this is the text. For later versions
(guideline 2.3.12 wants new features listed specifically, not "bug
fixes"), shorten the version's section of CHANGELOG.md into one line per
change a user can notice.

<!-- changed 2026-11-03: re-apply this block -->
<!-- appstore:whats-new max-chars=4000 -->
```text
The first App Store version of FoldForm: precise CAD for 3D printing by touch on iPhone, iPad and iPhone Duo, free for one project.
```

### Screenshots

**DONE:** `docs/appstore/screenshots/` holds six screenshots for each of the
two sizes App Store Connect requires, made from the real app by
`scripts/dev/appstore_screenshots.sh` and checked by
`scripts/dev/check_appstore_screenshots.py` (exact size, no alpha channel,
not blank):

| File | Shows |
|---|---|
| `*-1-project-box.png` | a 60 x 40 x 22 mm project box: rounded corners, 2 mm walls, four countersunk M3 holes, a cable hole whose diameter is being typed with print clearance (the value box) |
| `*-2-key-tag.png` | a key tag with raised bold text being edited (size, depth, emboss/deboss, angle) |
| `*-3-sketch.png` | a mounting plate's sketch: typed sizes and constraint glyphs |
| `*-4-construction-plane.png` | an axis through a bore and the Plane tool placing a plane 25 mm above the part |
| `*-5-model-panel.png` | the Model panel with the fillet step open to change its radius (highlighted on the part) |
| `*-6-home.png` | Home with six projects and their previews, in "FoldForm (Files app)" |

- **iPhone** `iphone-*.png`: 1320 x 2868 pixels, portrait, the **6.9"
  display** size (Apple: "iPhone with Dynamic Island (large display)",
  iPhone 16, 17 and 18 Pro Max class; 440 x 956 points at 3x, with their
  safe areas); Apple scales them down for smaller iPhones. **Check at
  upload:** on 2026-10-31 Apple's page names "iPhone with Dynamic Island
  (medium display)" (6.3", 1206 x 2622, the iPhone 16/17/18 Pro) as the
  required iPhone size in its summary, while its table still fills that size
  from scaled large-display screenshots. If App Store Connect accepts the
  6.9" set and Media Manager (**View All Sizes**) shows the 6.3" size filled,
  nothing more is needed. **Only if it insists on its own 6.3"
  screenshots**, the owner takes them on the iPhone 16 Pro (decided
  2026-10-31), following "6.3-inch screenshots from the iPhone 16 Pro"
  below.
- **iPhone Duo** (the foldable): App Store Connect accepts its sizes now
  (outer display 1398 x 2034, inner 2007 x 2853, either way round);
  optional for this submission. They give the product page an "optimized
  for iPhone Duo" badge, and Apple will **require** them from April 2027
  for apps built with the iOS 27.1 SDK or later, which FoldForm is. The
  lead adds the two sizes to the script after the owner's Duo log (TD-95,
  TD-150).
- **iPad** `ipad-*.png`: 2752 x 2064 pixels, landscape, the **13" display**
  size (iPad Pro 13-inch M4 class; 1376 x 1032 points at 2x). Required for
  an app that runs on iPad; scaled down for smaller iPads.

**For 0.3.1:** the captioned set in `docs/appstore/captioned/` (the same
sizes and names, a caption above each; docs/MARKETING_RENDERS.md, piece 3)
replaces these.

**YOU:** version page → **Product Page Information** → **App Previews and
Screenshots** → the **iPhone** tab → drag in `iphone-1` … `iphone-6` in
that order; the **iPad** tab → `ipad-1` … `ipad-6` (Apple's help, "Upload
app previews and screenshots", 2026-10-31; **View All Sizes** in Media
Manager shows the scaled sizes). The first three show in search results.
Apple allows plain screenshots (no captions needed); captions or a device
frame are optional marketing. If the UI changes before submission, the lead
re-runs `bash scripts/dev/appstore_screenshots.sh` (about three minutes).
They are rendered on Windows at the devices' exact point sizes, pixel
ratios and safe areas, so the only visible difference from a device is the
UI font (Windows' Segoe UI instead of San Francisco) and the missing status
bar (docs/TECHNICAL_DEBT.md, TD-95). If you prefer, replace any of them with
a screenshot from your own iPhone 16 Pro Max or 13-inch iPad (same sizes);
an iPhone 16 Pro (1206 x 2622) or 11-inch iPad screenshot is a different
size class and cannot replace the 6.9" or 13" set (the 16 Pro's fill only
the 6.3" slot, below).

#### 6.3-inch screenshots from the iPhone 16 Pro (only if App Store Connect asks)

The iPhone 16 Pro's display is 2622 by 1206 pixels (Apple's iPhone 16 Pro
tech specs, support.apple.com/en-us/121031), so its screenshots (side
button + volume up) are **1206 x 2622** in portrait. Apple's screenshot
specifications (read 2026-10-31) list exactly that size, with the iPhone
16 Pro, under "iPhone with Dynamic Island (medium display)", the 6.3"
slot. Take them in portrait, in TestFlight build 54, in this order, as in
the 6.9" set (open each `iphone-*.png` beside you to match the view):

1. **Project box:** the 60 x 40 x 22 mm box with rounded corners, 2 mm
   walls and four countersunk M3 holes; tap the cable hole's wall in the
   end and type a diameter with print clearance, so the value box and its
   arrow show (do not apply).
2. **Key tag:** the 70 x 24 x 3 mm tag with raised bold text, the text
   being edited (size, depth, raised or engraved, angle on screen).
3. **Sketch:** a mounting plate's sketch on the ground, 80 x 50 with four
   5 mm screw holes and a 20 mm opening, sizes typed and constraint glyphs
   shown, the Select tool on.
4. **Construction plane:** the 50 x 50 flange plate with an axis through
   its bore and the Plane tool placing a plane 25 mm above it.
5. **Model panel:** the project box seen from the back left, the Model
   panel open with the fillet step open to change its radius.
6. **Home:** six projects with their previews (Project box, Key tag,
   Mounting plate, Flange plate, Spacer M8, Pen cup).

The six projects the 6.9" set was made from are on the PC in
`C:\Users\ayers\OpenShape\build\appstore-screenshots\iphone-6-home\OpenShape\` (`.openshape`
files; until the next screenshot run or a clean build): email them to
yourself or put them in iCloud Drive, then open each in FoldForm (after
the purchase in step 8 of LAUNCH_CHECKLIST.md, so the one-project limit
does not get in the way). Upload the six in the same order to the 6.3"
slot (Media Manager, **View All Sizes**). The status bar shows on a
device; that is fine.

---

## 3b. The iPhone Duo product page

The badge ("optimized for iPhone Duo") comes with screenshots in App Store
Connect's iPhone Duo slot (section 3), so they go into this submission.
They are taken by the owner on his Duo with build 54; the listing tool
finds Apple's exact Duo sizes and uploads them in file-name order.

### The captures (the owner, on the Duo, build 54)

**First:** TestFlight build 54 installed; File → About FoldForm →
**Restore purchase** on the Duo (it was bought on the iPhone; this also
shows that Restore works on a second device), so the sample projects open
without the sheet; the
six sample projects on the phone (the `.openshape` files in
`C:\Users\ayers\OpenShape\build\appstore-screenshots\iphone-6-home\OpenShape\`,
mailed to yourself or put in iCloud Drive, each opened in FoldForm); Focus
on (no notifications); Light appearance. Every capture with the phone
**opened flat or closed**, never half folded (FoldForm does not place
things around the crease yet, TD-140). Before each one, look along every
edge and the side bar: if anything is cut off or hidden, skip that capture
and tell the lead. A capture is the side button and volume up. Save them
as these files in `docs/appstore/duo/` (the folder the listing tool reads,
in this order):

1. `01-inner-project-box.png`: **inner display, opened flat, wide** (the
   way it opens, side bar along the side). *Project box*, Model panel
   open, tap the cable hole's wall and type `8.4` on the keypad; do not
   apply (the value box and its arrow on the part).
2. `02-inner-sketch.png`: **inner display, opened flat, wide.** *Mounting
   plate* → Model panel → its sketch → edit: the sizes and constraint
   glyphs showing, the Select tool on.
3. `03-inner-tall-holes.png`: **inner display, opened flat, turned tall**
   (portrait). *Flange plate* → Hole tool, M3 countersunk, the allowance
   0.2, two holes placed, not applied.
4. `04-outer-project-box.png`: **outer display, closed, upright**
   (portrait). Right after capture 1, fold the phone without touching
   anything else: the same project, the same value still typed.
5. `05-outer-key-tag.png`: **outer display, closed, upright.** *Key tag*,
   tap the text: its size and depth showing, with the keypad.
6. `06-outer-sideways-model.png`: **outer display, closed, sideways**
   (landscape). *Project box*, nothing selected, the part filling the
   view.

Then send **Logs/openshape.log** (Files → On My iPhone → FoldForm →
Logs): its `window ...` lines give the real screen sizes and insets for
0.3.1 (TD-150).

### Captions (for a later, captioned set; the captures above go up plain)

1. The whole part, every step: open it up.
2. Sketch with your finger. Type the size.
3. Holes that fit M3 screws.
4. Fold it: your work stays where it was.
5. Raised text for labels and version marks.
6. Precise CAD that fits in your pocket.

(Caption 4 only once capture 4 shows the same value as capture 1.)

### Featuring nomination (App Store Connect → Featuring → Nominations)

Type **App Launch**; platforms iOS (iPhone) and iOS (iPad); countries:
those the app is sold in; publish date: the day it is released (Apple
asks for at least two to three weeks' notice, so this is a late
nomination; an **App Enhancements** nomination for 0.3.1's Duo work can go
in with notice). Supplemental material: https://github.com/SamuelAirs/openshape.

<!-- changed 2026-11-03: re-apply this block -->
<!-- appstore:nomination-name max-chars=60 -->
```text
FoldForm: precise CAD for 3D printing on iPhone Duo
```

<!-- changed 2026-11-03: re-apply this block -->
<!-- appstore:nomination-description max-chars=1000 -->
```text
FoldForm is new: precise, open-source CAD by touch for people who design parts for their 3D printers, with the phone layout on iPhone Duo's outer display, the full layout on its inner display, no account, no data collected and every tool free for one project.
```

---

## 4. Pricing

**DECIDED 2026-10-31:** the app is **free**; a **non-consumable in-app
purchase** unlocks unlimited projects (the owner's "one-time USD 10").

| | Free | Unlimited Projects (one-time) |
|---|---|---|
| Every modeling, sketch, export and share tool | yes | yes |
| Projects in OpenShape's folder | one at a time (delete it to start another) | as many as you like |
| Price | 0 | **9.99** USD base price (decided 2026-10-31) |

How the app enforces it (code: the purchase track of 2026-10-31,
ARCHITECTURE.md): only the iPhone/iPad app has the limit. Adding a project
while one exists opens the *Unlimited projects* sheet: New project, Import
STEP as a project, Duplicate, saving under a new name, and a project or STEP
file opened from another app. Opening, editing, renaming, deleting,
exporting and sharing are never limited, and people who already have
several projects keep all of them. Restore is on the sheet and in File →
About. Windows has no limit.

At 9.99 with the Small Business Program (15%) each sale brings about 8.49
(before VAT in countries where the price includes it); 12 sales a year
pay the USD 99 membership. A free app with an unlock answers the problem
the 4.99 recommendation below worried about, because people can try the app
fully before paying.

**YOU, the app's price:** Apps → your app → **Monetization → Pricing and
Availability** → Price Schedule → base country **United States** → **USD 0
(Free)**. **Availability** (same page): all countries and regions except
**China mainland** (apps there need a Chinese ICP filing number) and the
**27 EU countries** (decided 2026-10-31: not a trader, section 1, item 5):
Austria, Belgium, Bulgaria, Croatia, Cyprus, Czechia, Denmark, Estonia, Finland, France, Germany, Greece, Hungary, Ireland, Italy, Latvia, Lithuania, Luxembourg, Malta, Netherlands, Poland, Portugal, Romania, Slovakia, Slovenia, Spain, Sweden. Norway, Iceland, Liechtenstein and Switzerland are not in the
EU and stay in.

**YOU, the in-app purchase:** Apps → your app → **Monetization → In-App
Purchases** → **+** → Type **Non-Consumable**, Reference Name `Unlimited
Projects`, Product ID **`io.github.samuelairs.openshape.unlimited`**. The
app asks the store for exactly this ID. Apple: a product ID "isn't editable
after you save the In-App Purchase" and can't be reused within the app,
even after deleting it. Then:

- **Price Schedule:** United States, 9.99; accept Apple's other
  storefronts.
- **Availability:** the same countries as the app (all except China
  mainland and the 27 EU countries).
- **App Store Localization**, English (U.S.): the display name and
  description below.
- **Review Information:** screenshot `docs/appstore/iap-review.png`, a
  6.9" iPhone screenshot of the sheet. Apple accepts any screenshot size
  the app supports; once uploaded it can be replaced but not removed.
  Notes: the block below.
- **Family Sharing:** off. It can be turned on later; once on, Apple
  says, it can't be turned off.
- Leave **"Show to all App Store users"** as it is (the app handles a
  purchase started on its App Store page).

It needs the active Paid Apps Agreement (section 1). Without it the store
returns no product: TestFlight and App Review then see "The App Store
can't be reached" on the sheet, and App Review rejects the app. Metadata
changes can take up to an hour to reach the sandbox. The first in-app
purchase must be submitted with an app version (section 6).

### In-app purchase display name (max 30)

<!-- appstore:iap-display-name max-chars=30 -->
```text
Unlimited Projects
```

### In-app purchase description (max 45)

<!-- appstore:iap-description max-chars=45 -->
```text
Create as many projects as you like.
```

### In-app purchase review notes

<!-- appstore:iap-review-notes max-chars=4000 -->
```text
Unlimited Projects is a non-consumable purchase. FoldForm is free with every tool for one project; this purchase allows any number of projects on the purchaser's Apple Account (iPhone and iPad).

To see it: on the Home screen, tap New project, tap Box, then File, Save and give the project a name. Go back to Home (File, Home) and tap New project again: the "Unlimited projects" sheet opens with the price, Unlock, Restore purchase and Not now. The sheet also opens from the line "Unlock unlimited projects" on Home and from File, About FoldForm (Unlimited projects row, which also has Restore purchase). After the purchase, New project goes ahead and creates a second project.
```

### Before 2026-10-31: the paid-app recommendation (history)

**DECIDE.** Recommendation: **one-time purchase, USD 4.99** (US base price;
Apple fills in the other 174 storefronts), universal (one purchase for
iPhone and iPad), no in-app purchases, no subscription.

What that covers (Small Business Program, 15% commission):

| Price (US) | Your proceeds per sale | Sales per year for the USD 99 membership |
|---|---|---|
| 2.99 | about 2.54 | 39 |
| **4.99** | **about 4.24** | **24** |
| 6.99 | about 5.94 | 17 |
| 9.99 | about 8.49 | 12 |

(US prices exclude sales tax, which Apple adds and pays. In countries where
the price includes VAT, e.g. the EU at about 20%, a 4.99 sale leaves about
3.50 after VAT and commission, so plan on roughly 30 sales a year. At the
standard 30% commission the proceeds would be about 3.49; enroll in the
Small Business Program. Your own income tax is not included.)

Why 4.99, from the comparable apps checked in the App Store on 2026-09-27:

| App | Price model |
|---|---|
| OpenShape3D (touch direct-modeling CAD, open source) | **free** |
| Shapr3D (the UX north star) | free with 2 projects; Pro USD 299 per year |
| Onshape, Autodesk Fusion, SketchUp, uMake | free to download; subscriptions for the full product |
| AutoCAD | USD 9.99 a month or 99.99 a year |
| Nomad Sculpt, Valence 3D (3D sculpting) | USD 19.99 once |
| Procreate (iPad drawing) | USD 12.99 once; Procreate Pocket (iPhone) USD 5.99 |

- A one-time price fits the maker audience, which dislikes subscriptions,
  and is simplest to run (no in-app purchase code, no server).
- Paid apps have no free trial on the App Store, and a free app of almost
  the same name exists (section 0), so the price has to be an easy yes:
  4.99 is a coffee, 24 sales a year pay the membership.
- OpenShape is at 0.2: the store listing should not promise more than a
  young app. Raise the price later (e.g. 7.99 or 9.99 at 1.0 with more
  features): a price change never affects people who already bought, and
  can be scheduled in advance.
- Apple's price points: every 0.10 up to 10 USD, every 0.50 up to 50.

(The 2026-09-27 steps for a 4.99 price are superseded by the free price
and the in-app purchase above.)

---

## 5. The public beta (TestFlight)

External testers (anyone with the link, up to 10,000) need the first build
of each version to pass **Beta App Review**; later builds of the same
version usually do not. Builds expire after 90 days.

1. **DONE: allow external testing.** Since 2026-09-27,
   `scripts/ios/testflight.sh` uploads builds from `main` with
   `testFlightInternalTestingOnly` = false, so every build uploaded since
   then can go to an external group (older builds stay internal only). Use
   a build whose TestFlight page shows it was uploaded on or after that day.
2. **YOU: test information.** App Store Connect → OpenShape → **TestFlight**
   → **Test Information** (English): paste the *Beta App Description* below;
   *Feedback Email*: the address you want tester email at (it is shown to
   testers; the privacy policy's contact address is fine); *Marketing URL*:
   `https://github.com/SamuelAirs/openshape`; *Privacy Policy URL*: the one
   from section 2. **Beta App Review Information**: your first and last
   name, phone (international format, e.g. `+1 555 123 4567`) and email;
   **Sign-in required: unchecked**; *Review Notes*: paste the notes below.
3. **YOU: an external group.** TestFlight → the **+** next to *External
   Testing* → name `Public beta` → Create. (An internal group must exist
   first; yours does, docs/IPAD.md step 7.)
4. **YOU: add the build.** In `Public beta` → **Add Builds** → the newest
   build → *What to Test*: paste the text below → tick *Automatically notify
   testers* → **Submit Review**. Only one build per version can be in review
   at a time, and at most 6 builds a day.
5. **Wait for Beta App Review** (an email says when; plan 1 to 2 days;
   Apple publishes no figure for beta review and says 90% of App Review
   submissions are reviewed within 24 hours).
6. **YOU: the public link.** `Public beta` → **Testers** → **Create Public
   Link** → *Open to Anyone* (or *Filter by Criteria*: device iPhone/iPad,
   iOS 17 or later) → optionally a *Tester Limit* (e.g. 500) → copy the link
   (`https://testflight.apple.com/join/...`) and share it: the README, the
   GitHub release notes, maker forums. People install TestFlight and tap it.
7. **Feedback** arrives in TestFlight → **Feedback** (screenshots with notes,
   crash reports). What Apple shares with you about testers: docs/PRIVACY.md
   ("TestFlight beta testing"); for public-link testers, not their name or
   email.

### Beta App Description

<!-- appstore:beta-description max-chars=4000 -->
```text
FoldForm is precise 3D CAD for makers and 3D printing, made for touch: push and pull faces, sketch with constraints, type exact sizes, add holes that fit screws and raised text, and export STL, 3MF or STEP. One app for iPhone and iPad.

This beta is what will go on the App Store. Please model a real part you want to print and tell us what was hard to find, hard to tap, slow or missing. The app needs no account and collects no data; your projects stay on your device (Files app, On My iPhone or On My iPad, FoldForm).
```

### What to Test (for the build you add; update it with each build)

For 0.3.1 build 58 (2026-11-02: the five sample projects on Home; on the
iPhone Duo, Open... and Import STEP... explain the Files app's Share,
TD-158; build 57's edges, not tried yet). Build 55's five checks passed on
the owner's Duo (his Board ticks, 2026-11-02). Before: builds 56 and 57
(File → Open is Home; the window fills the screen).

<!-- changed 2026-11-02: re-apply this block -->
<!-- appstore:what-to-test max-chars=4000 -->
```text
FoldForm 0.3.1, on iPhone Duo. First: if the View buttons show Pen on (it turns on when the Apple Pencil touches the screen), tap Pen to turn it off; with Pen on, fingers only move the view.
1. Closed and opened: the model reaches every edge (no black bar at the right or bottom), and no button or text sits under the clock, the side bar or the home bar. Turn it tall on the inner display too.
2. Home now has five Samples below your projects: Lamp shade, Twisted vase, Electronics box, Phone stand, Honeycomb organizer. Open each one: it opens named after the sample, with every step in the Model panel. Say whether they look good.
3. In one sample, change a step in the Model panel (for example the Lamp shade's Shell wall, or a size in its Outline sketch): the part follows. Then go Home and open another sample: FoldForm asks whether to save the changes. The sample itself never changes.
4. On Home, tap Open...: instead of the file picker (which shows no files on the Duo), a card explains sharing from the Files app. Tap Open Files, touch and hold a project there, tap Share, then FoldForm: it opens in FoldForm. Import STEP... says the same for STEP files.
5. File, Open: Home appears with your projects, and nothing closes.
Then send the log: Files app, FoldForm, Logs, openshape.log, Share. Screenshots with a note go from TestFlight; crash reports reach us automatically.
```

### Beta App Review notes (also used for App Review, section 6)

<!-- appstore:review-notes max-bytes=4000 -->
```text
FoldForm is a CAD (3D modeling) app for designing parts to 3D print. It needs no account or sign-in and makes no network connections of its own (only Apple's StoreKit for the in-app purchase); everything works offline. Links in Help and About open Safari.

In-app purchase: the app is free with every tool for one project; the non-consumable "Unlimited Projects" allows more. To see it, follow steps 1 to 6 below (they save one project), go back to Home (File, Home) and tap New project: the "Unlimited projects" sheet opens (Unlock, Restore purchase, Not now). It is also reachable from "Unlock unlimited projects" on Home and from File, About FoldForm.

A 2-minute test path (iPhone or iPad):
1. Launch: the Home screen appears. Tap New project.
2. Tap Box (tool strip at the bottom on iPhone, tool palette on the left on iPad): a 20 mm cube appears.
3. Tap the cube's top face. Drag the blue arrow up, or tap the value box, type 35 and tap the check mark: the cube is now 35 mm tall.
4. Tap a vertical edge of the cube, tap the value box, type 4 and tap the check mark: the edge is rounded (fillet).
5. Tap the top face, then Sketch. Tap Circle and draw a circle with a finger. Tap Finish sketch. Tap inside the circle, tap the value box, type -10 and tap the check mark: a 10 mm deep hole is cut.
6. File, Save: type a name and tap Save. The project is in the Files app: On My iPhone (or iPad), FoldForm. File, Export STL writes Exports/<name>.stl there and opens the share sheet (close it, or pick Save to Files).
7. The question mark button opens the help card with all gestures. File, About FoldForm shows the version, the license, the source code link and the privacy policy link.

Gestures: one finger on empty space orbits the view, two fingers pan and pinch to zoom, a two-finger tap undoes, a three-finger tap redoes.

Features in this version: direct modeling (push/pull faces, fillets, chamfers, shell, offset, holes with screw presets, raised or engraved text), sketches with constraints and typed dimensions, extrude and revolve, move, rotate, align, mirror, pattern, construction axes and planes, union, subtract and intersect, an editable Model panel with the full history, STEP import, STL, 3MF and STEP export through the share sheet, opening projects and STEP files from Files and Mail, recovery of unsaved work after a crash.

FoldForm is open source (Mozilla Public License 2.0): https://github.com/SamuelAirs/openshape. It uses the open-source libraries Open CASCADE Technology, Qt, PlaneGCS, Eigen, libzip and nlohmann/json and the Noto Sans font under their licenses (listed in About).
```

---

## 6. Submitting to the App Store

1. **DONE: the build.** Submit **build 54**: the TestFlight upload of the
   **version tag** `v0.3.0` (ipad.yml run 41224892140, whose public notice
   says "build 54 from v0.3.0 uploaded"; version 0.3.0, license gate PASS
   15/15). Its GitHub release v0.3.0 is published with
   `OpenShape-0.3.0-ios-sources.tar` (docs/LICENSING.md). Builds of `main`
   are for testing only, and `main` is 0.3.1 since the tag.
2. **YOU: the version page** (iOS App → *0.3.0 Prepare for Submission*):
   screenshots (section 3), promotional text, description, keywords, support
   and marketing URLs, version, copyright; **Build** → **+** → build
   **54**; **In-App Purchases and Subscriptions** → add *Unlimited
   Projects* (the first in-app purchase must go with an app version).
3. **YOU: App Review Information** (same page, at the bottom): sign-in
   **not required**; contact name, phone, email; *Notes*: the review notes
   from section 5 (they must describe the features specifically: guideline
   2.3.1(a)). No attachment needed.
4. **YOU: version release.** Choose **Manually release this version** (you
   press *Release* when you want) or **Automatically release this version
   after App Review, no earlier than** a date and time. Phased release
   only applies to updates.
5. **YOU: submit.** Top right **Add for Review** → **Submit for Review**.
   The status goes *Waiting for Review* → *In Review* → *Pending Developer
   Release* or *Ready for Distribution*. On average 90% of submissions are
   reviewed within 24 hours. A rejection arrives with a message in App Store
   Connect (App Review section); answer or fix and resubmit. An **expedited
   review** can be requested for a critical bug fix
   (developer.apple.com/contact/app-store/?topic=expedite); the Duo launch is
   not OpenShape's own event, so do not count on it.

### Rejection reasons that could apply, and how OpenShape meets them

| Guideline | The risk | How OpenShape meets it | State |
|---|---|---|---|
| 2.1 App completeness (over 40% of rejections) | crashes, broken links, placeholder text, incomplete information | real-UI acceptance run (4356/4356) and TestFlight on the owner's iPhone and iPad; every link in the app and the listing answered on 2026-10-31 (privacy policy, support page, user guide, repository, `tree/v0.3.0`, release v0.3.0); the contact email is real; review notes give a test path | **DONE** |
| 2.3 Accurate metadata, 2.3.3 screenshots | screenshots or text promising what the app does not do | the screenshots are produced from the real app's own scenes (FoldForm 0.3.0); the description lists features of 0.3.0; the share sheet and Open-in lines wait for the device check (LAUNCH_CHECKLIST.md, step 8) | **DONE** / **YOU** (check) |
| 2.3.2 in-app purchases in the metadata | not saying what needs a purchase | "FREE TO START" in the description; the in-app purchase's own name and description | **DONE** |
| 2.3.7 names and keywords | trademarks or other apps' names in the metadata, prices in name/subtitle | none used; the name is FoldForm (section 0) | **DONE** |
| 2.3.10 other platforms | mentioning other platforms | only Windows (not a mobile platform), once; remove it if asked | **DONE** |
| 4.1 Copycats, 5.2.1 intellectual property | a name close to another developer's app or a trademark | renamed FoldForm (section 0) | **DONE** |
| 4.2 Minimum functionality | a thin app or a website in a wrapper | a native CAD app (OpenCASCADE, Metal through Qt RHI) with 30+ tools, working offline | **DONE** |
| 2.5.2 Self-contained | downloading or running code | none: everything is in the bundle | **DONE** |
| 3.1.1 In-app purchase | unlocking features outside Apple's in-app purchase; no way to restore a non-consumable | the only unlock (unlimited projects) is Apple's in-app purchase; Restore purchase on the sheet and in About; no link to buying elsewhere (the Windows version is free, not a purchase) | **DONE** (code, 2026-10-31) / **YOU** (create it, section 4) |
| 2.1 In-app purchase completeness | the reviewer's purchase does not load or fails | Paid Apps Agreement **Active** and the in-app purchase submitted with the version; try it in TestFlight first (LAUNCH_CHECKLIST.md, step 10) | **YOU** |
| 5.1.1 Privacy | a missing or vague privacy policy; data collected without saying so | docs/PRIVACY.md (what is kept, where, deletion, Apple's crash reports and TestFlight, contact); linked in App Store Connect and in About; "Data Not Collected" is accurate; privacy manifest in the bundle | **DONE** / **YOU** (answers) |
| 2.4.1 / iPad | an iPhone app that does not work well on iPad | universal app with its own iPad layout, all orientations, Split View | **DONE** |
| Licenses | LGPL libraries linked statically on iOS | About → Licenses with every text and the written source offer, in the `v0.3.0` code; build 54's license gate PASS 15/15; the release v0.3.0 carries the iOS sources; the description names the LGPL libraries (docs/LICENSING.md); Apple's standard EULA (decided 2026-10-31) | **DONE** |

---

## 7. Timeline to 2026-10-23

Dates are 2026. App Review needs no more than a day in most cases, but a
rejection costs a round trip, so the plan keeps a week of buffer.

| When | What | Who |
|---|---|---|
| **Mon 28 Sep** | Decide the name (section 0), the price (section 4), the version number (section 6) and EU trader status (section 1). Accept the Paid Apps Agreement; tax form; bank account; enroll in the Small Business Program; DSA declaration. | **YOU** |
| Mon 28 – Wed 30 Sep | Merge this branch and the licensing track; push to `main` (the privacy policy and support pages go live; the upload from that push is the first beta build). Rename if decided. Real email address in PRIVACY.md and SUPPORT.md. Try the share sheet and Open in OpenShape on the iPhone and iPad (docs/IPAD.md: iPad items 14 to 16, iPhone item 14). (The share sheet, Open in OpenShape, dSYMs and external-testing uploads are on `main` since 27 Sep.) | lead, **TRACK**, **YOU** |
| **Wed 30 Sep** | TestFlight test information, external group `Public beta`, add the build, **submit for Beta App Review**. | **YOU** |
| Thu 1 – Fri 2 Oct | Beta App Review (buffer 2 days). Create the **public link**; share it (README, release notes, maker forums). | **YOU** |
| 2 – 12 Oct | **Public beta**, about 10 days. Read TestFlight feedback daily; the lead fixes; new builds reach testers without a new review (same version). Check that the Paid Apps Agreement shows *Active*. | **YOU**, lead |
| **Fri 9 Oct** | Feature freeze for the App Store version. If the UI changed, re-run the screenshots script. | lead |
| **Mon 12 Oct** | Final build uploaded. Fill in the App Store version page, pricing, availability, App Privacy, age rating, screenshots, review information. | **YOU** |
| **Tue 13 Oct** | **Submit for App Review**, release option *automatically, no earlier than Wed 21 Oct 09:00* (or manual). | **YOU** |
| 14 – 15 Oct | App Review (typically under 24 hours). | Apple |
| 15 – 20 Oct | Buffer for one or two rejections: fix, resubmit. **Last safe submission: Fri 16 Oct.** | **YOU**, lead |
| **Wed 21 Oct** | **On sale** (automatic release), two days before the Duo. Post the App Store link: README, GitHub release, testers (TestFlight testers can keep testing betas after release). | **YOU** |
| Fri 23 Oct | iPhone Duo launch: try OpenShape on it (docs/IPAD.md, iPhone item 12: fold and unfold, Split View). | **YOU** |

---

## Sources

Read again on 2026-10-31 (the 0.3.0 submission): the screenshot
specifications, "Upload app previews and screenshots"
(https://developer.apple.com/help/app-store-connect/manage-app-information/upload-app-previews-and-screenshots),
the DSA trader requirements, the Small Business Program, "In-App Purchase
information"
(https://developer.apple.com/help/app-store-connect/reference/in-app-purchases-and-subscriptions/in-app-purchase-information),
"Submit an In-App Purchase"
(https://developer.apple.com/help/app-store-connect/manage-submissions-to-app-review/submit-an-in-app-purchase)
and the App Review Guidelines 2.3.2, 2.3.3, 2.3.7, 2.3.10, 3.1.1 and
5.1.1(i).

Apple pages read on 2026-09-27:

- Screenshot specifications: https://developer.apple.com/help/app-store-connect/reference/app-information/screenshot-specifications
- App information (name 30, subtitle 30, categories, content rights, age rating, privacy policy URL, DSA): https://developer.apple.com/help/app-store-connect/reference/app-information/app-information
- Platform version information (promotional text 170, description 4,000, keywords 100 bytes, support URL, copyright, release options, review notes 4,000 bytes): https://developer.apple.com/help/app-store-connect/reference/app-information/platform-version-information
- Age ratings values and definitions: https://developer.apple.com/help/app-store-connect/reference/app-information/age-ratings-values-and-definitions
- Categories: https://developer.apple.com/app-store/categories/
- App privacy details ("collect", data collected by Apple): https://developer.apple.com/app-store/app-privacy-details/
- Manage app privacy (the "No, we do not collect data" answer, publishing, the policy URL): https://developer.apple.com/help/app-store-connect/manage-app-information/manage-app-privacy
- Encryption export regulations: https://developer.apple.com/documentation/security/complying-with-encryption-export-regulations
- Sign and update agreements: https://developer.apple.com/help/app-store-connect/manage-agreements/sign-and-update-agreements
- Provide tax information: https://developer.apple.com/help/app-store-connect/manage-tax-information/provide-tax-information
- App Store Small Business Program: https://developer.apple.com/app-store/small-business-program/
- EU Digital Services Act trader requirements: https://developer.apple.com/help/app-store-connect/manage-compliance-information/manage-european-union-digital-services-act-trader-requirements
- Set a price (price points, base country, equalization): https://developer.apple.com/help/app-store-connect/manage-app-pricing/set-a-price
- 900 price points, every 0.10 up to 10 USD: https://www.apple.com/newsroom/2022/12/apple-announces-biggest-upgrade-to-app-store-pricing-adding-700-new-price-points/
- China mainland compliance (ICP filing): https://developer.apple.com/help/app-store-connect/manage-compliance-information/view-china-mainland-compliance-information
- TestFlight overview (10,000 external testers, Beta App Review, 90 days, feedback): https://developer.apple.com/help/app-store-connect/test-a-beta-version/testflight-overview
- Invite external testers (groups, public link, What to Test, 6 builds a day): https://developer.apple.com/help/app-store-connect/test-a-beta-version/invite-external-testers
- Provide test information (Beta App Description, feedback email): https://developer.apple.com/help/app-store-connect/test-a-beta-version/provide-test-information
- App Review information properties: https://developer.apple.com/help/app-store-connect/reference/app-review-information
- What TestFlight shares with developers: https://testflight.apple.com/
- App Review (90% within 24 hours, common rejections, expedited review): https://developer.apple.com/distribute/app-review/
- App Review Guidelines (2.1, 2.3.1(a), 2.3.3, 2.3.7, 2.3.10, 2.3.12, 2.5.2, 3.1.1, 4.1, 4.2, 5.1.1, 5.2.1): https://developer.apple.com/app-store/review/guidelines/
- Submit an app: https://developer.apple.com/help/app-store-connect/manage-submissions-to-app-review/submit-an-app
- Receiving payments (within 45 days of the fiscal month's end, thresholds): https://developer.apple.com/help/app-store-connect/getting-paid/overview-of-receiving-payments/
- App Analytics (aggregated, opt-in): https://developer.apple.com/app-store/app-analytics/

Name and price research, 2026-09-27: the iTunes Search API
(`https://itunes.apple.com/search?term=OpenShape&entity=software`, and
`lookup?bundleId=com.laan.labs.openshape3d`, `lookup?bundleId=com.openshape.app`,
`lookup?id=1091675654`), openshape3d.com, github.com/laanlabs/openshape3d,
the USPTO records for ONSHAPE (Justia Trademarks, registrations 4752666 and
5949177), shapr3d.com/pricing.
