# OpenShape for iPhone and iPad: proposed End User License Agreement

**Status: a proposal (2026-09-27), not in use yet.** It is meant for App
Store Connect → OpenShape → App Information → License Agreement → Edit →
"Apply a custom EULA to all chosen countries or regions" (plain text; paste
everything between the two lines below and fill in the bracketed parts).
Without it, Apple's standard EULA applies. Why a custom one: see
[LICENSING.md](LICENSING.md), "The iOS app and the App Store" (in short:
Apple's standard EULA forbids modifying and reverse engineering the app and
only generally excepts what open-source licenses permit; this one grants
what the LGPL requires in so many words, and contains the ten minimum terms
Apple requires of a custom EULA).

This is not legal advice. Before using it, the owner fills in the bracketed
parts and, ideally, has a lawyer check it against consumer law where the
app is sold (warranty and liability clauses in particular). Apple's
instructions: <https://www.apple.com/legal/internet-services/itunes/dev/minterms/>.

---

OPENSHAPE END USER LICENSE AGREEMENT

This agreement is between you and [OWNER'S FULL LEGAL NAME] ("we", "us"), the developer of the OpenShape app for iPhone and iPad (the "App"). By downloading or using the App you accept it.

1. The parties. This agreement is concluded between you and us only, not with Apple Inc. We, not Apple, are solely responsible for the App and its content.

2. Your license. We grant you a non-transferable license to use the App on any Apple-branded products that you own or control, as permitted by the Usage Rules in the Apple Media Services Terms and Conditions; the App may also be used by other accounts associated with the purchaser through Family Sharing or volume purchasing. This agreement does not provide usage rules that conflict with the Apple Media Services Terms and Conditions.

3. Open-source software. The App is free software built from open-source components, and nothing in this agreement limits the rights those components' licenses give you:
   a. OpenShape's own source code is licensed to you under the Mozilla Public License 2.0 (https://mozilla.org/MPL/2.0/). You can get the complete source code of every version of the App, with its build scripts, free of charge at https://github.com/SamuelAirs/openshape (each App Store version is a release tag there).
   b. The App contains Qt (GNU Lesser General Public License 3.0), Open CASCADE Technology (GNU Lesser General Public License 2.1 with the Open CASCADE exception) and PlaneGCS (GNU Lesser General Public License 2.1 or later), and other components under the licenses listed in the App (About, then Licenses). You may modify these libraries, rebuild and relink the App with your modified versions and use the result on your own devices, and you may reverse-engineer the App to debug such modifications. The exact source code of these libraries for each version is available free of charge as described in the App (About, then Licenses, then "Your rights to the LGPL libraries"), including a written offer valid for at least three years.
   c. Where this agreement and the license of an open-source component differ, that license prevails for that component. We do not use any power we may have to forbid the circumvention of technological measures to the extent such circumvention is done in exercising the rights these licenses give you.
   d. Apart from what these licenses allow, you may not sell, rent or redistribute copies of the App obtained from the App Store; building, using and distributing OpenShape from its source code is governed by the Mozilla Public License 2.0 and the licenses of its components, not by this agreement. This agreement grants no rights in the name "OpenShape".

4. Maintenance and support. We alone are responsible for any maintenance and support of the App, as described in this agreement or required by applicable law. Apple has no obligation whatsoever to provide any maintenance or support for the App.

5. Warranty. The App is provided "as is" to the extent the law allows: it is a design tool, and you are responsible for checking the parts you design before relying on them. Any warranty that is not effectively disclaimed is ours alone. If the App does not conform to a warranty that applies, you may tell Apple, and Apple will refund the price you paid for the App. Beyond that refund, and as far as the law allows, Apple has no warranty obligation for the App; any other claims, losses, liabilities, damages, costs or expenses caused by a failure to conform to a warranty are ours alone.

6. Liability. To the extent the law allows, we are not liable for indirect or consequential damages, loss of data or loss of profits arising from the use of or inability to use the App. Nothing in this agreement limits our liability beyond what applicable law permits, or your rights as a consumer under the law of your country.

7. Product claims. We, not Apple, are responsible for addressing any claims by you or any third party relating to the App or your possession and use of it, including (i) product liability claims, (ii) any claim that the App fails to conform to an applicable legal or regulatory requirement, and (iii) claims under consumer protection, privacy or similar legislation.

8. Intellectual property. If a third party claims that the App, or your possession and use of it, infringes that third party's intellectual property rights, we, not Apple, are solely responsible for the investigation, defense, settlement and discharge of that claim.

9. Legal compliance. You confirm that you are not in a country under a U.S. Government embargo or designated by the U.S. Government as "terrorist supporting", and that you are not on any U.S. Government list of prohibited or restricted parties.

10. Third-party terms. When you use the App, you must also keep to any third-party agreements that apply to you.

11. Third-party beneficiary. Apple Inc. and its subsidiaries are third-party beneficiaries of this agreement, as you and we agree. Once you accept this agreement, Apple has the right, and is deemed to have accepted the right, to enforce it against you as a third-party beneficiary.

12. Termination. This agreement ends if you fail to comply with it; you then stop using the App. Sections 3, 5 to 8 and 13 survive. Ending this agreement does not end the rights the open-source licenses in section 3 give you.

13. Contact. Questions, complaints or claims about the App: [OWNER'S FULL LEGAL NAME], [POSTAL ADDRESS], [TELEPHONE NUMBER], [E-MAIL ADDRESS]. Bugs and requests: https://github.com/SamuelAirs/openshape/issues.

---

Notes for the owner (not part of the text above):

- Apple's minimum terms and where they are: acknowledgement (1),
  scope of license (2), maintenance and support (4), warranty and the
  refund through Apple (5), product claims (7), intellectual property (8),
  legal compliance (9), developer name and address (13), third-party terms
  (10), Apple as third-party beneficiary (11).
- Section 3 is what the LGPL needs (terms that permit modification and
  reverse engineering for debugging; LGPL-2.1 section 6, LGPL-3.0
  section 4) and what the MPL needs (section 3.2(b): the executable's
  license may not limit the recipient's rights in the source code form).
- Apple requires the name, address, telephone and e-mail in a custom EULA
  (minimum term 8). Apple already shows EU customers a trader's contact
  details, but deciding what to publish here is the owner's call.
- Changing the EULA later is possible in App Store Connect; it applies to
  new downloads.
