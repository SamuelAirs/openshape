# OpenShape on the App Store: the owner's checklist

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
- **TRACK**: done by a separate work track (the share sheet, Open-in,
  dSYMs and external TestFlight testing; the licensing texts). Listed so
  nothing is forgotten; not repeated here.

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

**DECIDE, before anything is submitted.** The name is the hardest thing to
change once the app is on sale (ratings, links and search history stay with
the listing, but people will have learned the old name). What a check of
the App Store and the web found on 2026-09-27:

| Who | What | Why it matters |
|---|---|---|
| **OpenShape3D** by Laan Labs (Laan Consulting Corp) | On the App Store since 2026-08-06 (version 1.3, 2026-09-18), **free**, "a direct-modeling CAD app built for touch" for iPhone and iPad, on OpenCASCADE, **open source** (MIT, github.com/laanlabs/openshape3d, openshape3d.com) | Almost the same name, the same kind of app, on the same devices, for free, and on sale first. People searching "OpenShape" will find both; some will buy the wrong one or ask why they should pay. Its maker could object (App Review guidelines 4.1 and 5.2.1 forbid names that copy another developer's product), and has the earlier use. |
| **Open—Shape** by Michael Hix | On the App Store since 2026-08-25: a guitar chord app whose description calls itself "OpenShape"; bundle ID `com.openshape.app` | A different field, but it shows the plain name "OpenShape" was already unavailable to others in App Store Connect by August 2026 (App Store names are unique). Check what name your own app record has (App Store Connect → OpenShape → App Information → Name). |
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
   *Shapewright*, *MakerShape*. The lead engineer can do the renaming in the
   code and documents in an hour once you pick one.
2. **Keep OpenShape, but distinguish it in the store name**, e.g.
   `OpenShape: CAD for 3D Printing` (exactly 30 characters, the maximum) with
   a subtitle that says what it is, and keep the GitHub link and the open
   source line in the description so people can tell the two apart. This
   keeps the confusion with OpenShape3D; accept that risk knowingly.

The rest of this document uses "OpenShape"; replace it everywhere if you
rename.

---

## 1. Agreements, tax, banking and the Small Business Program

A paid app can be priced only once the **Paid Apps Agreement** is active,
and it becomes active only after the tax and bank details are in. Start
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
   → **Enroll**. It is for developers with up to USD 1 million in proceeds in
   the previous calendar year (new developers qualify). Needs: the Account
   Holder, the accepted Paid Apps Agreement, and a list of any associated
   developer accounts (none). The lower rate applies from 15 days after the
   end of the fiscal month in which Apple approves the enrollment, so enroll
   now, weeks before the first sale. It stays until your proceeds pass USD 1
   million in a year.
5. **YOU and DECIDE: EU Digital Services Act trader status.** Every developer
   must declare it: Business → Agreements → Compliance → **Complete
   Compliance Requirements** next to *Digital Services Act*. A **paid** app is
   a strong sign of a *trader* (Apple lists "a paid app" among the signs). A
   trader who is an individual must give an **address (or P.O. box), a phone
   number and an email address, which are shown publicly on the EU App Store
   pages**, verified by codes and a document. So decide one of:
   - declare trader status with a **P.O. box** and a separate phone number
     and email (recommended if you want EU customers without publishing your
     home address), or
   - leave the 27 EU countries out of the app's availability (section 4).
6. **DONE: export compliance.** `src/app/ios/Info.plist.in` sets
   `ITSAppUsesNonExemptEncryption` to `false`, so App Store Connect does not
   ask the encryption questions for each build. This is true for OpenShape:
   it has no network code at all, and the ZIP library in project files is
   built without any encryption (`scripts/ios/build-deps.sh`:
   `-DENABLE_COMMONCRYPTO=OFF -DENABLE_GNUTLS=OFF -DENABLE_MBEDTLS=OFF
   -DENABLE_OPENSSL=OFF`).

---

## 2. App information

App Store Connect → Apps → OpenShape → **App Information** (general) and
the version page (**iOS App → 1.0 Prepare for Submission**).

| Field | What to enter | State |
|---|---|---|
| Name (max 30) | see section 0; e.g. `OpenShape: CAD for 3D Printing` | **DECIDE / YOU** |
| Subtitle (max 30) | `Precise modeling for makers` (27) | **YOU** (paste) |
| Bundle ID | `io.github.samuelairs.openshape` (set by the app record; cannot change after a build was uploaded) | **DONE** |
| SKU | `openshape` (internal only, set when the record was made) | **DONE** |
| Primary language | English (U.S.) | **YOU** (check) |
| Primary category | **Graphics & Design** ("tools for art, design and graphics creation"; where makers browse SketchUp, Nomad Sculpt, uMake and 3D-print design apps) | **YOU** |
| Secondary category | **Productivity** (where Shapr3D, Onshape and Autodesk Fusion are) | **YOU** |
| Content rights | "Does your app contain, show, or access third-party content?" **Yes** → "I have the necessary rights": the bundled font (Noto Sans, SIL Open Font License) and libraries are used under their licenses (THIRD_PARTY.md). The app shows no other third-party content. | **YOU** |
| Age rating | the questionnaire below → **4+** | **YOU** |
| Made for Kids | No | **YOU** |
| Privacy Policy URL | `https://github.com/SamuelAirs/openshape/blob/main/docs/PRIVACY.md` (works once this branch is merged and pushed to `main`) | **DONE** (page) / **YOU** (paste) |
| User Privacy Choices URL | leave empty (optional; there is nothing to choose) | — |
| Support URL | `https://github.com/SamuelAirs/openshape/blob/main/docs/SUPPORT.md` (how to get help, with the contact routes; Apple requires a page with real contact information) | **DONE** (page) / **YOU** (paste) |
| Marketing URL | `https://github.com/SamuelAirs/openshape` (optional) | **YOU** |
| Copyright | `2026 Sam Ayers` (App Store Connect adds the ©; use your legal name if you prefer) | **YOU** |
| License agreement | Apple's standard EULA (the default). The MPL-2.0 license of the source code is unaffected; what the LGPL libraries need is the licensing track's (docs/LICENSING.md). | **TRACK** |
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
| | User-Generated Content | **No** (projects stay on the device; nothing is shared with other users) |
| | Social Media; Messaging and Chat; Advertising | No; No; No |
| Mature Themes | Profanity or Crude Humor; Horror/Fear Themes; Alcohol, Tobacco, or Drug Use or References | None |
| Medical or Wellness | Medical or Treatment Information; Health or Wellness Topics | None / No |
| Sexuality or Nudity | all three | None |
| Violence | Cartoon or Fantasy; Realistic; Prolonged Graphic or Sadistic; Guns or Other Weapons | None |
| Chance-Based Activities | Gambling; Simulated Gambling; Contests; Loot Boxes | None / No |

Result: **4+**.

### App Privacy ("nutrition label")

App Store Connect → Apps → OpenShape → **App Privacy** → **Get Started** →
**"No, we do not collect data from this app"** → **Save** → **Publish**.
Then **Privacy Policy → Edit** and paste the privacy policy URL.

**DONE: why "Data Not Collected" is true** (checked in the code on
2026-09-27; docs/PRIVACY.md says the same in plain words):

- Apple defines "collect" as sending data off the device so that the
  developer or a partner can keep it beyond the request. OpenShape has **no
  network code**: no `QNetworkAccessManager`, sockets or `URLSession`; the
  app links only Qt Core, Gui, Qml, Quick and QuickControls2, not Qt Network;
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
  About OpenShape → "Privacy policy: OpenShape collects no data about you"**
  (checked by the `privacy` acceptance scenario, on a desktop and a phone
  layout). **DONE.**

Before submitting: **YOU** replace the email placeholder at the end of
docs/PRIVACY.md (and docs/SUPPORT.md) with a real address, or ask the lead
to do it; an App Store listing must not point to placeholder text
(guideline 2.1).

---

## 3. The listing text

Paste these into the version page (English (U.S.)). Their lengths are
checked by `python scripts/dev/check_appstore_texts.py` (run it after
editing a block below; limits from App Store Connect's reference).

### Promotional text (max 170 characters; can be changed any time without review)

<!-- appstore:promotional-text max-chars=170 -->
```text
Design parts for your 3D printer by touch: push and pull faces, sketch with constraints, type exact sizes, then export STL or 3MF for your slicer.
```

### Description (max 4,000 characters)

Accurate to what version 0.2.0 does (the README's feature list, checked
against the code). Update it when a feature changes (guideline 2.3).

<!-- appstore:description max-chars=4000 -->
```text
OpenShape is precise 3D CAD for makers and 3D printing, made for touch. Push and pull faces with a finger or Apple Pencil, sketch a profile and extrude it, and type exact sizes as you go. Every step stays editable.

MODEL
• Tap a flat face to see the part's size, then drag the arrow or type a new size; +5 or -5 changes it by that much.
• Round and chamfer edges, shell a body to a wall thickness, offset faces.
• Tap a hole's wall and type a new diameter. Delete a hole, fillet or chamfer and the faces around it close the gap.
• Extrude sketch profiles (a distance, both ways or up to a face; as a new body, joined, or cut, also through everything) or revolve them.
• Sizes accept units and arithmetic: 25, 1in, 2.5cm, 20+5, (10+2)*3.

SKETCH
• Sketch on the ground, an origin plane, a construction plane or any flat face.
• Line, rectangle, center rectangle, polygon, circle, arc, tangent arc and slot; trim, round a corner, offset, mirror and pattern.
• Constraints (horizontal, vertical, parallel, perpendicular, equal, tangent, concentric, midpoint, symmetric, distances, angles) shown as small glyphs, and a "Fully defined" status.

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
• Projects are saved in OpenShape's folder in the Files app, exports in its Exports folder, ready for your slicer, AirDrop or iCloud Drive.
• A Home screen with your projects and their previews.
• Unsaved work is kept safe in the background and offered again if the app closes.
• When a step cannot be done, OpenShape says why in plain words, often with a size that works, and your model stays as it was.

MADE FOR IPHONE AND IPAD
• One app for iPhone and iPad. On iPhone the tools sit along the bottom and the value box docks where it covers nothing you selected; on iPad the full layout, also in Split View and Stage Manager. The layout follows the window as you rotate or resize.
• One finger orbits, two fingers pan and zoom, a two-finger tap undoes and a three-finger tap redoes. Pen mode lets Apple Pencil draw and select while your fingers only move the view.

OPEN SOURCE, NO ACCOUNT
OpenShape is open-source software (Mozilla Public License 2.0) built on the OpenCASCADE solid-modeling kernel. The source code, a user guide and a Windows version are at github.com/SamuelAirs/openshape. There is no account, no advertising and no tracking: OpenShape collects no data.
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
- "AirDrop or iCloud Drive" works from the Files app today; when the share
  sheet track lands, "Share" in the app can be added.

### Keywords (max 100 bytes, comma-separated, no spaces)

Each keyword must be longer than two characters (so not "3d"); the app
name, the company name and the subtitle are searchable already, so they are
not repeated; no names of other apps or companies (guideline 2.3.7).

<!-- appstore:keywords max-bytes=100 -->
```text
print,printing,stl,3mf,step,cad,sketch,parametric,solid,design,part,enclosure,fillet,extrude,bracket
```

(If the name becomes `OpenShape: CAD for 3D Printing`, these fit instead,
without repeating the name's words:
`slicer,stl,3mf,step,sketch,parametric,solid,design,part,enclosure,fillet,extrude,engineering,bracket`,
exactly 100 bytes.)

### What's New

Not asked for the first version on the App Store. For the next ones
(guideline 2.3.12 wants new features listed specifically, not "bug fixes"),
take the version's section of CHANGELOG.md and shorten it; a draft for a
first update:

<!-- appstore:whats-new max-chars=4000 -->
```text
• Share exports straight to your slicer, AirDrop or Mail from the app.
• Open .openshape projects from the Files app.
• Fixes and improvements from the public beta.
```

(Only use lines for things that are really in that build: the first two are
the share-sheet track's.)

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
| `*-6-home.png` | Home with six projects and their previews, in "OpenShape (Files app)" |

- **iPhone** `iphone-*.png`: 1320 x 2868 pixels, portrait, the **6.9"
  display** size (iPhone 16 Pro Max class; 440 x 956 points at 3x, with its
  safe areas). Apple requires 6.9" screenshots for an iPhone app and scales
  them down for smaller iPhones.
- **iPad** `ipad-*.png`: 2752 x 2064 pixels, landscape, the **13" display**
  size (iPad Pro 13-inch M4 class; 1376 x 1032 points at 2x). Required for
  an app that runs on iPad; scaled down for smaller iPads.

**YOU:** version page → **Previews and Screenshots** → **iPhone** → 6.9"
Display → drag in `iphone-1` … `iphone-6` in that order; **iPad** → 13"
Display → `ipad-1` … `ipad-6`. The first three show in search results.
Apple allows plain screenshots (no captions needed); captions or a device
frame are optional marketing. If the UI changes before submission, the lead
re-runs `bash scripts/dev/appstore_screenshots.sh` (about three minutes).
They are rendered on Windows at the devices' exact point sizes, pixel
ratios and safe areas, so the only visible difference from a device is the
UI font (Windows' Segoe UI instead of San Francisco) and the missing status
bar (docs/TECHNICAL_DEBT.md, TD-70). If you prefer, replace any of them with
a screenshot from your own iPhone 16 Pro Max or 13-inch iPad (same sizes);
an iPhone 16 Pro (1206 x 2622) or 11-inch iPad screenshot is a different
size class and cannot stand in for the required ones.

---

## 4. Pricing

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

**YOU:** Apps → OpenShape → **Monetization → Pricing and Availability** →
Price Schedule → **Add Pricing** → base country **United States** →
**4.99** → review Apple's prices for the other storefronts (accept them) →
**Confirm**. Needs the active Paid Apps Agreement (section 1).

**YOU: availability** (same page): all countries and regions, except

- **China mainland**: apps there need a Chinese ICP filing number; leave it
  unchecked;
- the **27 EU countries** only if you chose not to declare trader status
  (section 1, item 5).

---

## 5. The public beta (TestFlight)

External testers (anyone with the link, up to 10,000) need the first build
of each version to pass **Beta App Review**; later builds of the same
version usually do not. Builds expire after 90 days.

1. **TRACK: allow external testing.** `scripts/ios/testflight.sh` uploads
   builds with `testFlightInternalTestingOnly` = true (internal testers
   only); the external-testing track switches that off. Until a build without
   that flag has been uploaded, it cannot be added to an external group.
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
OpenShape is precise 3D CAD for makers and 3D printing, made for touch: push and pull faces, sketch with constraints, type exact sizes, add holes that fit screws and raised text, and export STL, 3MF or STEP. One app for iPhone and iPad.

This beta is what will go on the App Store. Please model a real part you want to print and tell us what was hard to find, hard to tap, slow or missing. The app needs no account and collects no data; your projects stay on your device (Files app, On My iPhone or On My iPad, OpenShape).
```

### What to Test (for the build you add; update it with each build)

<!-- appstore:what-to-test max-chars=4000 -->
```text
Thank you for testing! Try these, then anything you like:
1. New project, Box: tap the top face, drag the arrow or tap the value and type 35, then tap the check mark.
2. Tap an edge and type a radius to round it. Shell the box (tap the top face, Shell, type 2).
3. Sketch on a face: draw a rectangle and a circle with your finger or Apple Pencil, type their sizes, Finish sketch, then tap inside the circle and type -10 to cut a hole.
4. Hole (M3, with the print allowance) and Text on a flat face.
5. File, Save: give it a name. File, Export STL or 3MF, then open it from the Files app (OpenShape, Exports) in your slicer.
6. Turn the device and use Split View: the layout should follow. On iPhone, the value box should never cover what you tapped.
If something is confusing, take a screenshot in OpenShape and send it from TestFlight with a note. Crash reports reach us automatically.
```

### Beta App Review notes (also used for App Review, section 6)

<!-- appstore:review-notes max-bytes=4000 -->
```text
OpenShape is a CAD (3D modeling) app for designing parts to 3D print. It needs no account or sign-in, has no in-app purchases, and makes no network connections; everything works offline. Links in Help and About open Safari.

A 2-minute test path (iPhone or iPad):
1. Launch: the Home screen appears. Tap New project.
2. Tap Box (tool strip at the bottom on iPhone, tool palette on the left on iPad): a 20 mm cube appears.
3. Tap the cube's top face. Drag the blue arrow up, or tap the value box, type 35 and tap the check mark: the cube is now 35 mm tall.
4. Tap a vertical edge of the cube, type 4, tap the check mark: the edge is rounded (fillet).
5. Tap the top face, then Sketch. Tap Circle and draw a circle with a finger. Tap Finish sketch. Tap inside the circle, tap the value box, type -10 and tap the check mark: a 10 mm deep hole is cut.
6. File, Save: type a name and tap Save. The project is in the Files app: On My iPhone (or iPad), OpenShape. File, Export STL writes Exports/<name>.stl there.
7. The question mark button opens the help card with all gestures. File, About OpenShape shows the version, the license, the source code link and the privacy policy link.

Gestures: one finger on empty space orbits the view, two fingers pan and pinch to zoom, a two-finger tap undoes, a three-finger tap redoes.

Features in this version: direct modeling (push/pull faces, fillets, chamfers, shell, offset, holes with screw presets, raised or engraved text), sketches with constraints and typed dimensions, extrude and revolve, move, rotate, align, mirror, pattern, construction axes and planes, union, subtract and intersect, an editable Model panel with the full history, STEP import, STL, 3MF and STEP export, recovery of unsaved work after a crash.

OpenShape is open source (Mozilla Public License 2.0): https://github.com/SamuelAirs/openshape. It uses the open-source libraries Open CASCADE Technology, Qt, PlaneGCS, Eigen, libzip and nlohmann/json and the Noto Sans font under their licenses (listed in About).
```

---

## 6. Submitting to the App Store

1. **TRACK / lead: the build.** The version you submit is the build number
   of an upload from `main` (docs/IPAD.md). **DECIDE the version number**:
   the bundle says `0.2.0` today (CMake `project(VERSION)`). Recommendation:
   submit the next GitHub release's number so both platforms match, and
   call it **1.0.0** only if you are happy to drop "early pre-release" from
   the README at the same time; otherwise **0.3.0**. The lead changes it in
   `CMakeLists.txt`.
2. **YOU: the version page** (iOS App → *1.0 Prepare for Submission*):
   screenshots (section 3), promotional text, description, keywords, support
   and marketing URLs, version, copyright; **Build** → **+** → pick the
   build.
3. **YOU: App Review Information** (same page, at the bottom): sign-in
   **not required**; contact name, phone, email; *Notes*: the review notes
   from section 5 (they must describe the features specifically: guideline
   2.3.1(a)). No attachment needed.
4. **YOU: version release.** Choose **Manually release this version** (you
   press *Release* when you want) or **Automatically release this version
   after App Review, no earlier than** a date and time (e.g. 2026-10-21,
   09:00). Phased release only applies to updates.
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
| 2.1 App completeness (over 40% of rejections) | crashes, broken links, placeholder text, incomplete information | real-UI acceptance run and TestFlight on the owner's iPhone and iPad; all links go to `main` on GitHub, so **merge and push before submitting** and open each URL once; replace the email placeholder in docs/PRIVACY.md and docs/SUPPORT.md; review notes give a test path | **DONE** / **YOU** (email, push) |
| 2.3 Accurate metadata, 2.3.3 screenshots | screenshots or text promising what the app does not do | the screenshots are produced from the real app's own scenes; the description lists only features of 0.2.0 | **DONE** |
| 2.3.7 names and keywords | trademarks or other apps' names in the metadata, prices in name/subtitle | none used; see section 0 for the name itself | **DONE** / **DECIDE** (name) |
| 2.3.10 other platforms | mentioning other platforms | only Windows (not a mobile platform), once; remove it if asked | **DONE** |
| 4.1 Copycats, 5.2.1 intellectual property | a name close to another developer's app or a trademark | section 0 | **DECIDE** |
| 4.2 Minimum functionality | a thin app or a website in a wrapper | a native CAD app (OpenCASCADE, Metal through Qt RHI) with 30+ tools, working offline | **DONE** |
| 2.5.2 Self-contained | downloading or running code | none: everything is in the bundle | **DONE** |
| 3.1.1 In-app purchase | unlocking features outside Apple's in-app purchase | paid up front, nothing to unlock | **DONE** |
| 5.1.1 Privacy | a missing or vague privacy policy; data collected without saying so | docs/PRIVACY.md (what is kept, where, deletion, Apple's crash reports and TestFlight, contact); linked in App Store Connect and in About; "Data Not Collected" is accurate; privacy manifest in the bundle | **DONE** / **YOU** (answers) |
| 2.4.1 / iPad | an iPhone app that does not work well on iPad | universal app with its own iPad layout, all orientations, Split View | **DONE** |
| Licenses | LGPL libraries linked statically on iOS | the licensing track (docs/LICENSING.md, license texts in the app) | **TRACK** |

---

## 7. Timeline to 2026-10-23

Dates are 2026. App Review needs no more than a day in most cases, but a
rejection costs a round trip, so the plan keeps a week of buffer.

| When | What | Who |
|---|---|---|
| **Mon 28 Sep** | Decide the name (section 0), the price (section 4), the version number (section 6) and EU trader status (section 1). Accept the Paid Apps Agreement; tax form; bank account; enroll in the Small Business Program; DSA declaration. | **YOU** |
| Mon 28 – Wed 30 Sep | Merge this branch, the external-TestFlight / share-sheet track and the licensing track; push to `main` (the privacy policy and support pages go live; a new TestFlight build without the internal-only flag). Rename if decided. Real email address in PRIVACY.md and SUPPORT.md. | lead, **TRACK** |
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
