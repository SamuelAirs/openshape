# Launch checklist: what the owner does (FoldForm 0.3.0, build 54)

The App Store app is **free with every tool for one project**; a one-time
in-app purchase, **Unlimited Projects** (USD 9.99), unlocks as many
projects as you like. Windows stays a free download without a limit.

This page is the order to do things in; the details and the texts to
paste are in [APP_STORE.md](APP_STORE.md). Everything is in **App Store
Connect** (appstoreconnect.apple.com) unless it says otherwise. Apple then
reviews the app, usually within 24 to 48 hours, and you press Release (or
let it release itself).

**Where you are (2026-11-13):** 0.3.1 is on the App Store (released
2026-11-06). **0.3.2** (selecting several faces, the view turning all the
way round, a hole made larger, the Pencil's fixes) is tagged `v0.3.2`; its
build number and your list to tick are in the vault's
`DuoApps/FoldForm/FoldForm tests.md`. When its Gates pass, tell the App
Store chat "submit FoldForm 0.3.2 with build N"; What's New is in
docs/APP_STORE.md. The lines below are the launch's history.

**Where you were (2026-11-04):** **FoldForm 0.3.0 is submitted** with build
54 and waits for App Review (steps 1 to 9 done). Next: press Release once
it is approved (step 10), and the open items below. **0.3.1** is on
TestFlight for your Duo (build 55, last section).

## Ready (checked by the lead on 2026-10-31)

- **The build to submit is build 54**, version 0.3.0, uploaded to
  TestFlight from the tag `v0.3.0` (GitHub's public record of ipad.yml run
  41224892140 says "build 54 from v0.3.0 uploaded", license gate PASS
  15/15, iPhone and iPad, iOS 17 or later). The owner sees it processed in
  App Store Connect, and no email from Apple reported a problem with it.
  TestFlight shows it as 0.3.0 (54). No newer build will appear: `main` is
  0.3.1 and not uploaded.
- In that build: the home-screen name FoldForm, the product ID
  `io.github.samuelairs.openshape.unlimited` exactly, Restore purchase on
  the sheet and in About, About → Licenses with the source offer, no
  network code or analytics ("Data Not Collected" is true), and the
  export-compliance answer (`ITSAppUsesNonExemptEncryption` = NO, so
  TestFlight asks no encryption questions).
- The GitHub release v0.3.0 is published with
  `OpenShape-0.3.0-ios-sources.tar` (what the app's Licenses view names).
- The privacy policy and support pages are live, with your email.
- The texts in APP_STORE.md fit Apple's limits; the screenshots are at
  sizes App Store Connect accepts (iPhone 6.9" 1320 x 2868, iPad 13"
  2752 x 2064, no transparency), plus `docs/appstore/iap-review.png`.

**Still not seen on a device:** the iPhone Duo's real screen edges and
tap-to-select in a sketch (after submitting: docs/IPAD.md, "The iPhone
Duo", steps 7 and 8).

## Decided (2026-10-31)

- **Name:** App Information → **Name** `FoldForm: CAD for 3D Printing`
  (29 characters) or plain `FoldForm`; the home screen says FoldForm.
- **Version:** 0.3.0. **Contact email:** ayers177@gmail.com (also in App
  Review Information).
- **Price:** the app Free; the purchase USD 9.99.
- **Where it is sold:** everywhere except **China mainland** and the **27
  EU countries** (not a trader under the EU Digital Services Act).
- **License agreement:** Apple's standard EULA (a custom one would have
  to publish your address and phone).

## Done

- **Step 1:** ~~**Paid Apps Agreement**, tax form and bank account~~: **Active**
  (2026-10-31).
- **Step 2:** ~~**App Store Small Business Program**~~: enrollment sent (2026-10-31).
  Apple's commission becomes 15% instead of 30% from 15 days after the
  end of the month Apple approves it; nothing else to do.
- **Step 3:** ~~**EU Digital Services Act status**~~: **not a trader** (2026-10-31),
  so the 27 EU countries are out of the app's and the purchase's
  availability.
- **Step 4:** ~~**The in-app purchase** *Unlimited Projects*~~ (2026-11-01, 04:21
  UTC): Non-Consumable, product ID
  `io.github.samuelairs.openshape.unlimited`, USD 9.99, review screenshot
  and notes ([APP_STORE.md, section 4](APP_STORE.md#4-pricing)).
- **Step 7:** ~~**Pricing and Availability**~~ (04:25 UTC): **Free**, all countries
  and regions except China mainland and the 27 EU countries (Austria,
  Belgium, Bulgaria, Croatia, Cyprus, Czechia, Denmark, Estonia, Finland,
  France, Germany, Greece, Hungary, Ireland, Italy, Latvia, Lithuania,
  Luxembourg, Malta, Netherlands, Poland, Portugal, Romania, Slovakia,
  Slovenia, Spain, Sweden).
- **Step 8:** ~~**The tries on build 54**~~ (04:48 UTC; the owner's word,
  2026-11-01): the sheet showed the real price and the sandbox purchase
  went through; Export STL opened the share sheet; a project opened from
  the Files app. **Still owed, not blocking:** Restore purchase on the
  iPad ("I haven't yet tested on the iPad"); Restore was used on the
  iPhone before the purchase.
- **Steps 5 and 6:** ~~**the listing**: App Information, App Privacy~~
  ("Data Not Collected"), age rating 4+, Apple's standard EULA.
- **Step 9:** ~~**Submitted for Review**~~ (2026-11-04): version 0.3.0,
  build 54, with the in-app purchase *Unlimited Projects*.

## Waiting: App Review

Apple usually answers within 24 to 48 hours (an email, and the status in
App Store Connect: *Waiting for Review*, *In Review*, then *Pending
Developer Release* or *Ready for Distribution*).

If App Review rejects: a problem in the texts, screenshots or notes is
fixed in App Store Connect and resubmitted without a new build. A problem
in the app itself needs a fix in a tagged build (`v0.3.1`, which carries
the 0.3.1 work below): rename the App Store version to 0.3.1 and submit
that tag's build. That is your call when it comes; tell the lead what
Apple wrote.

## Still open on your board (none of them holds up the review)

- **The iPhone Duo screenshots** (six captures, APP_STORE.md, section 3b):
  screenshots cannot change while the version is in review; they go up
  with the next version (0.3.1), and give the page the "optimized for
  iPhone Duo" badge then.
- **The Duo line in the description** ("closed, the phone layout; opened,
  the full layout on the inner display"): your word that it matches the
  Duo. If it does not, it changes with the next version.
- **Restore purchase on a second device** (the iPad, or the Duo): File →
  About FoldForm → Restore purchase says "Purchase restored: unlimited
  projects".
- **The Duo log:** Files → On My iPhone → FoldForm → Logs →
  `openshape.log` → Share (the real screen sizes and edges, TD-150).
- **Featuring nomination** (optional, when you want): the name and text
  are ready in [APP_STORE.md, section 3b](APP_STORE.md#3b-the-iphone-duo-product-page)
  (type App Launch; publish date the day it is released).

## After approval

10. If you chose manual release, press **Release**. Send the lead the App
    Store link: it goes into the README and the GitHub release.

## 0.3.1 on TestFlight (your Duo)

Build 55 (version 0.3.1, from `main`, 2026-11-04): the answers to your
Duo session. It reaches TestFlight by itself (an email when Apple has
processed it). Its *What to Test* is the block in
[APP_STORE.md, "What to Test"](APP_STORE.md#what-to-test-for-the-build-you-add-update-it-with-each-build):

1. Hold a finger on a letter of raised text for half a second: all of the
   text is selected.
2. Drag the arrow into the box, or type -1.5: the letters are engraved
   0.5 mm; Undo brings them back in one step.
3. Tap a face, then another facing the same way: both stay selected, the
   tool stays Push/Pull; tap one again to take it out.
4. With a face selected, pinch and pan: no jumping; the value box sits at
   the top or bottom edge, not over the part.
5. Free version only (without the purchase): a project moved into a folder
   in the Files app still counts as your one project.

Then send `Logs/openshape.log` from the Duo.
