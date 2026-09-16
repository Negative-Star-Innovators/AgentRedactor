#pragma once

// App-wide light/dark theme for the Linux GUI: one Qt stylesheet plus a
// matching application palette built from the Windows HomePage palette
// (windows/HomePage.cpp ApplyTheme) so the card-based layout reads the same
// on both platforms. Dark vs light follows the platform theme's color scheme
// where the Qt build provides it (6.8+; CI and the shipped AppImage run
// distro Qt 6.4, which falls back to palette lightness — the AppImage
// launcher forces the GTK3 platform theme, which tracks the system light/dark
// setting). Both the palette and the stylesheet are forced together so the
// window can never render a hybrid of the two modes. Re-checks run on
// QStyleHints::colorSchemeChanged (6.8+), QEvent::ApplicationPaletteChange,
// every top-level window show, and a few delayed timers at startup — the
// early-login autostart races the settings portal, which can resolve the
// preference seconds after construction.

class QApplication;

namespace Theme {

void Apply(QApplication& app);

} // namespace Theme
