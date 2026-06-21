# Third-Party Notices

Xscraper is distributed under the MIT License. It is built with Qt.

## Qt

The macOS release bundles Qt runtime frameworks produced by `macdeployqt`.
Xscraper uses these Qt modules:

- Qt Core
- Qt Gui
- Qt Network
- Qt Qml
- Qt Quick
- Qt Quick Controls 2

Qt is available under commercial and open-source licenses. This project uses
the open-source Qt path for the modules above and packages Qt as dynamically
linked frameworks in the macOS `.app` bundle.

When distributing Qt under the open-source LGPL option, comply with the LGPLv3
requirements, including preserving notices, providing the LGPL license text,
and allowing users to replace or relink the Qt libraries as required by that
license. This notice is not legal advice; review the official Qt licensing
materials for your distribution use case.

Included license text:

- `LICENSES/LGPL-3.0-only.txt`

Official references:

- https://www.qt.io/development/qt-framework/qt-licensing
- https://www.qt.io/development/open-source-lgpl-obligations
