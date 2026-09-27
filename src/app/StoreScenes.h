// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace os::ui {
class AppController;
}

namespace os::app {

// The App Store screenshots' scenes (scripts/dev/appstore_screenshots.sh,
// docs/APP_STORE.md): real parts built the way a user builds them (sketch,
// extrude, fillet, shell, holes, text, construction geometry), each left
// in a state worth showing. Run with --demo <name>; the window's size is
// the device's, so every click is placed from the camera at that size.
//
//   store-enclosure  a printed project box, a cable hole's diameter typed
//   store-text       a key tag, raised text being edited
//   store-planes     an axis through a hole, an offset plane being placed
//   store-sketch     a plate's sketch with sizes and constraints
//   store-history    the project box with its steps in the Model panel
//   store-home       Home with the projects above and their previews
QStringList storeSceneNames();
bool isStoreScene(const QString& name);
// Builds `name` into the (new, empty) document. "store-home" saves its
// projects into the app folder (--app-folder) if there is one, else into
// <dataDir>/projects. Returns false for an unknown name.
bool runStoreScene(ui::AppController& app, const QString& name, const QString& dataDir);
// The Model panel step shown open (its values editable) once `name` is
// built, as if its row had been tapped: the id of a step, or empty.
QString storeSceneOpenStep(ui::AppController& app, const QString& name);

} // namespace os::app
