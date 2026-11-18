# Third-party libraries

This folder has third-party or otherwise external libraries needed for other parts to work.

## LocalPeer

Library for making only one instance of the application run at all times. BetterNMM uses it to hand `nxm://` links and
`--launch` requests to the copy that is already running.

BSD licensed, derived from [QtSingleApplication](https://github.com/qtproject/qt-solutions/tree/master/qtsingleapplication).

Changes are made to make the code more generic and useful in less usual conditions.

## rainbow

Color functions extracted from [KGuiAddons](https://inqlude.org/libraries/kguiaddons.html). Used for adaptive text coloring.

Available either under LGPL version 2.1 or later.
