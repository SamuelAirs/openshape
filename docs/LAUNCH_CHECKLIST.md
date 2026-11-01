# Launch checklist: what the owner does (FoldForm 0.3.0, build 54)

The App Store app is **free with every tool for one project**; a one-time
in-app purchase, **Unlimited Projects** (USD 9.99), unlocks as many
projects as you like. Windows stays a free download without a limit.

This page is the order to do things in; the details and the texts to
paste are in [APP_STORE.md](APP_STORE.md). Everything is in **App Store
Connect** (appstoreconnect.apple.com) unless it says otherwise. Apple then
reviews the app, usually within 24 to 48 hours, and you press Release (or
let it release itself).

## Ready (checked by the lead on 2026-10-31)

- **The build to submit is build 54**, version 0.3.0, uploaded to
  TestFlight from the tag `v0.3.0` (GitHub's public record of ipad.yml run
  41224892140 says "build 54 from v0.3.0 uploaded", license gate PASS
  15/15, iPhone and iPad, iOS 17 or later). TestFlight shows it as
  0.3.0 (54). No newer build will appear: `main` is 0.3.1 and not uploaded.
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

**Not seen working on a device yet** (step 8 covers all but the Duo): the
purchase itself (Apple's sandbox), the share sheet after an export and
opening a project from the Files app (both in the review notes), and the
iPhone Duo's real screen edges and tap-to-select in a sketch (after
submitting: docs/IPAD.md, "The iPhone Duo", steps 7 and 8).

## Decided (2026-10-31)

- **Name:** App Information → **Name** `FoldForm: CAD for 3D Printing`
  (29 characters) or plain `FoldForm`; the home screen says FoldForm.
- **Version:** 0.3.0. **Contact email:** ayers177@gmail.com (also in App
  Review Information).
- **Price:** the app Free everywhere except China mainland; the purchase
  USD 9.99.

## Start now (these can take Apple hours or days)

1. **Business → Agreements: the Paid Apps Agreement**, then **Tax Forms**
   (W-9 in the US) and a **Bank Account**, until the agreement shows
   **Active**. The purchase does not load (in TestFlight or for App Review)
   before that, and App Review rejects an app whose purchase does not load:
   **do not submit before it is Active.**
   ([APP_STORE.md, section 1](APP_STORE.md#1-agreements-tax-banking-and-the-small-business-program))
2. **App Store Small Business Program:**
   developer.apple.com/app-store/small-business-program → **Enroll now**
   (after the agreement is accepted). Apple's commission becomes 15%
   instead of 30%, from 15 days after the end of the month Apple approves
   it.
3. **EU Digital Services Act status** (Business → Agreements →
   Compliance → **Complete Compliance Requirements** next to *Digital
   Services Act*). **A status is needed even if you leave the EU out**;
   Apple asks for it when you submit. Choose one:
   - **"This is a trader account."** and sell in the EU: an address or a
     P.O. box, a phone number and an email, verified by codes and a
     document, and **shown publicly** on the EU App Store pages. Apple
     counts an in-app purchase as a sign of a trader. Or
   - **leave the 27 EU countries out** in step 7 and choose **"This is
     not a trader account."** (Apple: distributing only outside the EU is
     not acting as a trader on the App Store).

## The in-app purchase

4. **Apps → FoldForm → Monetization → In-App Purchases → +**:
   - Type **Non-Consumable**; Reference Name `Unlimited Projects`;
     Product ID **exactly** `io.github.samuelairs.openshape.unlimited`
     (it can never be changed or reused; the app asks for this ID).
   - **Price Schedule:** base country United States, **9.99** (the app
     shows whatever price you set).
   - **Availability:** the same countries as the app (step 7).
   - **App Store Localization** (English U.S.): Display Name `Unlimited
     Projects`; Description `Create as many projects as you like.`
   - **Review Information:** screenshot `docs/appstore/iap-review.png`;
     Review Notes: the text in
     [APP_STORE.md, "In-app purchase review notes"](APP_STORE.md#in-app-purchase-review-notes).
   - **Family Sharing:** leave it off (once on, it can never be turned
     off). Leave "Show to all App Store users" as it is.

## The listing

5. **App Information:** Name (above), Subtitle, categories, content
   rights, age rating (every answer None or No: **4+**), Privacy Policy
   URL, copyright: paste them from
   [APP_STORE.md, section 2](APP_STORE.md#2-app-information).
   **License Agreement:** Apple's standard EULA unless you decide on the
   custom one (the question in the lead's report; APP_STORE.md section 2).
6. **App Privacy → Get Started → "No, we do not collect data from this
   app" → Save → Publish** ("Data Not Collected").
7. **Monetization → Pricing and Availability:** price **Free** (USD 0).
   Availability: all countries and regions except **China mainland**, and
   except the 27 EU countries if you chose that in step 3.

## With build 54 on your iPhone (TestFlight)

8. Install **0.3.0 (54)** from TestFlight (it uses Apple's sandbox: nothing
   is charged) and try what App Review will try:
   - with one project saved, tap **New project**: the *Unlimited projects*
     sheet opens with your price. Tap **Restore purchase** first: "No
     earlier purchase ... was found". Then **Unlock**, buy, and a new
     project opens. On your iPad (same Apple Account, build 54): File →
     About FoldForm → **Restore purchase**: "Purchase restored: unlimited
     projects". If the price does not show, the agreement (step 1) is not
     Active yet, or the purchase (step 4) was created or changed less than
     an hour ago or still misses a field.
   - **File → Export STL**: the share sheet opens (close it or pick Save
     to Files).
   - In the **Files app** → On My iPhone → FoldForm, tap a project: it
     opens in FoldForm.
   If the share sheet or the Files app step fails, tell the lead before
   submitting: its line comes out of the description and the review notes
   (no new build needed).

## Submit

9. The version page (**iOS App → 0.3.0 Prepare for Submission**):
   - **Product Page Information → App Previews and Screenshots:** the
     **iPhone** tab: `iphone-1` … `iphone-6` from
     `docs/appstore/screenshots/`, in that order; the **iPad** tab:
     `ipad-1` … `ipad-6`. Apple's page currently also names the 6.3"
     iPhone size (1206 x 2622) as required and fills it from the 6.9"
     ones; if App Store Connect still asks for 6.3" screenshots, tell the
     lead (APP_STORE.md, section 3).
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
