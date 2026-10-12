// washi for Firefox: the prefs the rice needs. Copy into the profile folder next to chrome/
// (about:support > Profile Folder); Firefox reads it at every start and it wins over
// about:config, so remove a line here to take a pref back. Written for Firefox 156/157.
//
// install.sh replaces file:///PATH/TO/startpage/index.html below with the real path of
// rices/washi/firefox/startpage/index.html. The start page finds the wallpaper at
// ../../wallpaper/, so it is linked from the rice where it lives rather than copied alone.
// SPDX-License-Identifier: GPL-3.0-or-later

// Load chrome/userChrome.css and chrome/userContent.css (off by default; needs a restart).
user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);

// --- the start page ---------------------------------------------------------------------

// Home (the Home button, Alt+Home and new windows) is the washi start page.
user_pref("browser.startup.homepage", "file:///PATH/TO/startpage/index.html");
// Open the home page at startup (1), not the last session (3) or a blank page (0).
user_pref("browser.startup.page", 1);

// --- the new tab page (restyled by userContent.css to match the start page) -------------
// Prefs cannot point new tabs at a file; userContent.css paints about:newtab as the start page
// instead: paper, a search card and the same eight stamped links as shortcuts.

// The eight shortcuts, pinned in the start page's order.
user_pref("browser.newtabpage.pinned", "[{\"url\":\"https://github.com/\",\"label\":\"GitHub\"},{\"url\":\"https://www.reddit.com/r/unixporn/\",\"label\":\"r/unixporn\"},{\"url\":\"https://wiki.archlinux.org/\",\"label\":\"Arch Wiki\"},{\"url\":\"https://github.com/Sondre234/paw/tree/main/docs\",\"label\":\"paw docs\"},{\"url\":\"https://www.youtube.com/\",\"label\":\"YouTube\"},{\"url\":\"https://mail.google.com/\",\"label\":\"Mail\"},{\"url\":\"https://news.ycombinator.com/\",\"label\":\"Hacker News\"},{\"url\":\"https://jisho.org/\",\"label\":\"jisho\"}]");
// One row of shortcuts is enough for eight.
user_pref("browser.newtabpage.activity-stream.topSitesRows", 1);
// No sponsored shortcuts or stories among them.
user_pref("browser.newtabpage.activity-stream.showSponsored", false);
user_pref("browser.newtabpage.activity-stream.showSponsoredTopSites", false);
// No Pocket/Discover stories under the shortcuts.
user_pref("browser.newtabpage.activity-stream.feeds.section.topstories", false);
// No weather widget (156 has both the old and the widget switch).
user_pref("browser.newtabpage.activity-stream.showWeather", false);
user_pref("browser.newtabpage.activity-stream.widgets.weather.enabled", false);
// No lists/timer widgets.
user_pref("browser.newtabpage.activity-stream.widgets.enabled", false);
// No new-tab wallpapers: the page is paper already.
user_pref("browser.newtabpage.activity-stream.newtabWallpapers.enabled", false);
// No Firefox logo/wordmark above the search box.
user_pref("browser.newtabpage.activity-stream.logowordmark.alwaysVisible", false);

// --- the browser chrome ------------------------------------------------------------------

// Draw Firefox's own widgets (buttons, checkboxes, scrollbars in panels) instead of the GTK
// theme's, so the GTK theme's colours don't leak into washi's.
user_pref("browser.theme.native-theme", false);
// Keep "Compact" in Customize > Density; washi pads the toolbar itself and looks right in
// either, so the density is left to you (browser.uidensity: 0 normal, 1 compact).
user_pref("browser.compactmode.show", true);
// The bookmarks bar stays hidden (Ctrl+Shift+B shows it), so the toolbar is one strip of paper.
user_pref("browser.toolbars.bookmarks.visibility", "never");
// The tab strip shares the titlebar; paw draws the window frame, and userChrome.css hides the
// minimise/maximise/close buttons Firefox would put there.
user_pref("browser.tabs.inTitlebar", 1);

// Vertical tabs work too; uncomment these to switch (or use the sidebar's Customize panel).
// user_pref("sidebar.revamp", true);
// user_pref("sidebar.verticalTabs", true);
// user_pref("sidebar.visibility", "always-show");   // or "expand-on-hover"

// washi or sumi follows the system's light/dark setting. To pin one regardless of it:
// user_pref("ui.systemUsesDarkTheme", 1);           // 1 sumi, 0 washi
