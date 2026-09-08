#pragma once

// App-wide light/dark theme for the Linux GUI: one Qt stylesheet built from
// the Windows HomePage palette (windows/HomePage.cpp ApplyTheme) so the
// card-based layout reads the same on both platforms. Dark vs light is
// detected from the application palette (the AppImage launcher forces the
// GTK3 platform theme, which follows the system light/dark setting); a live
// system theme switch re-applies via QEvent::ApplicationPaletteChange.

class QApplication;

namespace Theme {

void Apply(QApplication& app);

} // namespace Theme
