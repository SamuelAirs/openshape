# Launch checklist: what the owner does (FoldForm 0.3.0, build 54)

The App Store app is **free with every tool for one project**; a one-time
in-app purchase, **Unlimited Projects** (USD 9.99), unlocks as many
projects as you like. Windows stays a free download without a limit.

This page is the order to do things in; the details and the texts to
paste are in [APP_STORE.md](APP_STORE.md). Everything is in **App Store
Connect** (appstoreconnect.apple.com) unless it says otherwise. Apple then
reviews the app, usually within 24 to 48 hours, and you press Release (or
let it release itself).

**Where you are (2026-11-01, 04:48 UTC):** steps 1 to 4, 7 and 8 are
done. **Next is the listing** (steps 5 and 6), **then the submission**
(step 9).

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

## Next: the listing

5. **App Information:** Name (above), Subtitle, categories, content
   rights, age rating (every answer None or No: **4+**), Privacy Policy
   URL, copyright: paste them from
   [APP_STORE.md, section 2](APP_STORE.md#2-app-information).
   **License Agreement:** leave Apple's standard EULA as it is.
6. **App Privacy → Get Started → "No, we do not collect data from this
   app" → Save → Publish** ("Data Not Collected").

## Then: submit

9. The version page (**iOS App → 0.3.0 Prepare for Submission**). The
   texts come from the marked blocks (`<!-- appstore:... -->`) in
   docs/APP_STORE.md; a tool on this PC that fills App Store Connect from
   those blocks may enter them for you. **First, on your Duo:** the six
   captures in [APP_STORE.md, section 3b](APP_STORE.md#3b-the-iphone-duo-product-page)
   into `docs/appstore/duo/`, and your word that the Duo line in the
   description ("closed, the phone layout; opened, the full layout on the
   inner display") is what you see. Then the listing is applied and you
   submit.
   - **Product Page Information → App Previews and Screenshots:** the
     **iPhone** tab: `iphone-1` … `iphone-6` from
     `docs/appstore/screenshots/`, in that order; the **iPad** tab:
     `ipad-1` … `ipad-6`; the **iPhone Duo** size (Media Manager, View All
     Sizes): `01` … `06` from `docs/appstore/duo/` (the badge). Only if App Store Connect then asks for 6.3"
     iPhone screenshots: take the six scenes on your iPhone 16 Pro (its
     screenshots are 1206 x 2622, the size Apple lists for that slot);
     the six scenes are listed in
     [APP_STORE.md, "6.3-inch screenshots"](APP_STORE.md#63-inch-screenshots-from-the-iphone-16-pro-only-if-app-store-connect-asks).
   - Promotional text, description, keywords, support and marketing URLs,
     copyright: [APP_STORE.md, section 3](APP_STORE.md#3-the-listing-text).
   - **Build → + → 54** (version 0.3.0; it declares no non-exempt
     encryption, so no export questions should come up).
   - **In-App Purchases and Subscriptions:** add *Unlimited Projects*
     (the first purchase must go in with an app version).
   - **App Review Information:** sign-in **not required**; your name,
     phone and email; Notes: the review notes in
     [APP_STORE.md, section 5](APP_STORE.md#beta-app-review-notes-also-used-for-app-review-section-6).
   - Release: **Manually** (you press Release) or **Automatically**.
   - **Add for Review → Submit for Review.**

## After

10. If you chose manual release, press **Release** once it is approved.
    Send the lead the App Store link: it goes into the README and the
    GitHub release.

If App Review rejects: a problem in the texts, screenshots or notes is
fixed in App Store Connect and resubmitted without a new build. A problem
in the app itself needs a fix, a new tag and a build 55, which restarts the
submission: that is your call when it comes.

Any time after submitting: the iPhone Duo checks in docs/IPAD.md ("The
iPhone Duo", steps 1 to 8) and **Logs/openshape.log** from the Duo; they
decide the Duo fixes in 0.3.1 (PROJECT_STATUS.md, "Next").
