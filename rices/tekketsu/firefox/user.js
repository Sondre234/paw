// tekketsu (鉄血) for Firefox: the prefs the rice needs. Copy into the profile folder next to
// chrome/ (about:support > Profile Folder); Firefox reads it at every start and it wins over
// about:config, so remove a line here to take a pref back. Written for Firefox 156/157.
//
// install.sh puts the real path of the start page's index.html (installed with the rice's
// wallpapers beside it) into the commented start page line below. The start page finds the
// wallpaper at ../../wallpaper/, so it is linked from where it lives with them.
// SPDX-License-Identifier: GPL-3.0-or-later

// Load chrome/userChrome.css and chrome/userContent.css (off by default; needs a restart).
user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);

// --- the glass ---------------------------------------------------------------------------

// tekketsu's own switch: the toolbars and the sidebar as black glass the desktop shows through
// (paw blurs it), dark as old lacquer. false keeps them solid black iron, for a compositor
// that cannot show a translucent window or a taste for solid toolbars.
user_pref("tekketsu.glass", true);
// Let the new tab page be clear as well, so the window's glass is all there is behind its
// shortcuts. It makes every page that paints no background of its own see-through too (few
// sites, but plain text files among them), so it is left to you:
// user_pref("browser.tabs.allow_transparent_browser", true);

// --- the home page -----------------------------------------------------------------------

// Home (the Home button, Alt+Home and new windows) is about:home, which userContent.css paints
// like the new tab page, so the first tab and every new one look the same.
user_pref("browser.startup.homepage", "about:home");
// Or make home the tekketsu start page instead (the wallpaper behind riveted glass plates,
// falling ash, a summoning circle, a big clock and the elf mage; new tabs stay as they are, since
// prefs cannot point them at a file):
// user_pref("browser.startup.homepage", "file:///PATH/TO/startpage/index.html");
// Open the home page at startup (1), not the last session (3) or a blank page (0).
user_pref("browser.startup.page", 1);

// --- the new tab page (restyled by userContent.css to match the start page) -------------
// Prefs cannot point new tabs at a file; userContent.css paints about:newtab as the start page
// instead: cracked black iron under a faint circle, a glass search box and the same eight links
// as riveted plates.

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
// No new-tab wallpapers: the page has its cracked iron already.
user_pref("browser.newtabpage.activity-stream.newtabWallpapers.enabled", false);
// No Firefox logo/wordmark above the search box.
user_pref("browser.newtabpage.activity-stream.logowordmark.alwaysVisible", false);

// --- the browser chrome ------------------------------------------------------------------

// tekketsu is dark: Firefox's own dark widgets (menus' marks, scrollbars in panels) under it,
// whatever the system's light or dark preference says. Pages still follow the system.
user_pref("browser.theme.toolbar-theme", 0);
// Draw Firefox's own widgets (buttons, checkboxes, scrollbars in panels) instead of the GTK
// theme's, so the GTK theme's colours don't leak into tekketsu's.
user_pref("browser.theme.native-theme", false);
// Keep "Compact" in Customize > Density; tekketsu pads the toolbar itself and looks right in
// either, so the density is left to you (browser.uidensity: 0 normal, 1 compact).
user_pref("browser.compactmode.show", true);
// The bookmarks bar stays hidden (Ctrl+Shift+B shows it), so the toolbar is one strip of glass.
user_pref("browser.toolbars.bookmarks.visibility", "never");
// The tab strip shares the titlebar; paw draws the window frame, and userChrome.css hides the
// minimise/maximise/close buttons Firefox would put there.
user_pref("browser.tabs.inTitlebar", 1);

// Vertical tabs work too; uncomment these to switch (or use the sidebar's Customize panel).
// user_pref("sidebar.revamp", true);
// user_pref("sidebar.verticalTabs", true);
// user_pref("sidebar.visibility", "always-show");   // or "expand-on-hover"
