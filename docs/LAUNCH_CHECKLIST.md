# Launch checklist: what the owner does (2026-10-31)

The App Store app is **free with every tool for one project**; a one-time
in-app purchase, **Unlimited Projects** (about USD 10), unlocks as many
projects as you like. Windows stays a free download without a limit.

"Post today" means **submit today**. Apple then reviews the app,
usually within 24 to 48 hours for a first app, and you press Release (or
let it release itself). Details and the texts to paste are in
[APP_STORE.md](APP_STORE.md); this page is the order to do them in.

Everything is in **App Store Connect** (appstoreconnect.apple.com) unless
it says otherwise. Steps 1 to 7 can be done **now**, while the lead
builds; steps 8 to 11 need the final build (the lead says when its build
number is in TestFlight).

## Decided (2026-10-31)

1. **Name: FoldForm.** No App Store app has that name. In App Store
   Connect → your app → **App Information → Name**, enter
   `FoldForm: CAD for 3D Printing` (29 characters; it helps people find
   the app in search), or plain `FoldForm`. App Store Connect refuses a
   name another developer already uses. The home-screen name in the
   build is `FoldForm`. The code and the GitHub repository keep the name
   openshape.
2. **Version: 0.3.0.** On the version page, set the version to `0.3.0`
   so the build appears there.
3. **Contact email: ayers177@gmail.com.** It goes on the privacy policy
   and support pages, which are public. Use the same address in App
   Review Information.

## Start now (these can take hours or days at Apple)

4. **Business → Agreements: the Paid Apps Agreement**, then **Tax Forms**
   (W-9 in the US) and a **Bank Account**. In-app purchases need it, even
   though the app itself is free. The purchase does not load (in TestFlight
   or for App Review) until the agreement shows **Active**. **Do not submit
   before it is Active.** [APP_STORE.md, section 1](APP_STORE.md#1-agreements-tax-banking-and-the-small-business-program)
5. **App Store Small Business Program**
   (developer.apple.com/app-store/small-business-program → Enroll):
   Apple's commission becomes 15% instead of 30%.
6. **EU Digital Services Act** (Business → Agreements → Compliance):
   declare yourself a trader (your address or a P.O. box, phone and email
   are shown publicly on EU store pages), or leave the 27 EU countries out
   of the availability in step 9.
7. **Create the in-app purchase:** Apps → your app → **Monetization → In-App
   Purchases** → **+**:
   - Type **Non-Consumable**; Reference Name `Unlimited Projects`;
     Product ID **exactly** `io.github.samuelairs.openshape.unlimited`
     (it can never be changed or reused; the app looks for this ID).
   - **Price:** base country United States, **9.99** or **10.00** (both
     are Apple price points; the app shows whatever you choose).
   - **Availability:** the same countries as the app.
   - **App Store Localization** (English U.S.): Display Name `Unlimited
     Projects` (max 30); Description `Create as many projects as you
     like.` (max 45).
   - **Review Information:** screenshot `docs/appstore/iap-review.png`
     (the lead makes it); Review Notes: the text in
     [APP_STORE.md, "In-app purchase review notes"](APP_STORE.md#in-app-purchase-review-notes).
   - **Family Sharing:** leave it off for now. You can turn it on later,
     but once on it can never be turned off.
   - Leave "Show to all App Store users" as it is.

## The listing (now, while the build runs)

8. **App Information, App Privacy, age rating, listing texts and
   screenshots:** paste them from [APP_STORE.md, sections 2 and 3](APP_STORE.md#2-app-information).
   - App Privacy: "Data Not Collected".
   - Age rating: every answer None or No, which gives 4+.
   - License agreement: keep **Apple's standard EULA** for this launch.
   - Screenshots: `docs/appstore/screenshots/` (the lead refreshes them
     today).
9. **Monetization → Pricing and Availability:** price **Free** (USD 0).
   Availability: all countries except **China mainland**, and except the
   EU if you chose that in step 6.

## Once the lead says the build is ready

10. **Try the purchase on your iPhone** with that TestFlight build (it
    uses Apple's sandbox and costs nothing):
    - save one project, then tap New project: the Unlimited projects
      sheet opens with your price;
    - tap Unlock, then Restore purchase;
    - check that a second project can be created.
    If the price does not show, the agreement (step 4) is not Active yet,
    or the in-app purchase was created less than an hour ago.
11. **Submit** (the version page, iOS App → *Prepare for Submission*):
    - set the **version** to the number from step 2;
    - **Build** → **+** → the build the lead names (the one built from
      the `v0.3.0` tag, not a plain `main` build);
    - **In-App Purchases and Subscriptions** → add *Unlimited Projects*.
      The first in-app purchase must go in with an app version; you can
      also do it from the purchase's own page: **Add for Review** → pick
      this version;
    - **App Review Information:** sign-in not required, your name, phone
      and email, Notes: paste them from APP_STORE.md, section 5;
    - release: **Manually** (you press Release) or **Automatically**;
    - **Add for Review → Submit for Review**.

## After approval

12. Press **Release** if you chose manual. Send the lead the App Store
    link: it goes into the README and the GitHub release.

## What the lead does today

- Turns on the one-project limit and the purchase (iPhone and iPad only),
  with tests that click every path.
- Fixes Shell's fallback path, which could return a wrong shape.
- Bumps the version, refreshes the screenshots and adds the purchase's
  review screenshot.
- Puts your email in the privacy and support pages.
- Tags the release, so the GitHub release carries the source code the
  open-source licenses require. Only that tag's build may be submitted.
- Tells you the build number to pick in step 11.
