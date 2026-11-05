// Self-tests for menu_ui.

import "ui_widget" for Rect
import "menu_ui" for ChoiceMessage, ChoiceViewState, MenuUi
import "menu_settings_ui" for SettingsMenu
import "menu_sort_profiles" for SortProfiles
import "syncterm_menu" for Menu
import "syncterm" for Key, KeyEvent
import "ui_style" for Style, Theme

// UI fixture with the named cloud-catalog interface.
class CloudThemeFixture {
  construct new(package, name, author, description, version, cached, update, error) {
    _package = package
    _name = name
    _author = author
    _description = description
    _version = version
    _cached = cached
    _update = update
    _error = error
  }
  package { _package }
  name { _name }
  author { _author }
  description { _description }
  version { _version }
  cached { _cached }
  updateAvailable { _update }
  error { _error }
}

class MenuUiTest {
  static run() {
    __pass = 0
    __fail = 0
    System.print("=== menu_ui self-test starting ===")

    testChoiceViewState_()
    testBrowserMaximumHeight_()
    testThemeIdentityAndLabels_()
    testUnicodeNames_()
    testSortFieldLabels_()
    testSortOrderComparison_()

    var total = __pass + __fail
    System.print("=== menu_ui: %(total) tests, %(__pass) pass, " +
        "%(__fail) fail ===")
    return [__pass, __fail]
  }

  // Requires the C fixture's synthetic double-width ASCII font in slot 255.
  static runSyntheticFontAudit() {
    __pass = 0
    __fail = 0
    testChoiceMessageUsesPaintedFont_()
    return [__pass, __fail]
  }

  static check_(ok, label) {
    if (ok) {
      __pass = __pass + 1
    } else {
      __fail = __fail + 1
      System.print("  FAIL %(label)")
    }
  }

  static testSortFieldLabels_() {
    var valid = true
    for (row in Menu.sortFields) {
      var positive = row[2] ? "%(row[1]) (reversed)" : row[1]
      var negative = row[2] ? row[1] : "%(row[1]) (reversed)"
      if (SortProfiles.fieldLabel_(row[0]) != positive ||
          SortProfiles.fieldLabel_(-row[0]) != negative) valid = false
    }
    check_(valid && Menu.sortFields.count > 0,
        "sort labels account for each field's default descending direction")
  }

  static testSortOrderComparison_() {
    var empty = SortProfiles.profileLabels_([])
    check_(SortProfiles.sameOrder_([], []) &&
        SortProfiles.sameOrder_([1, -2], [1, -2]) &&
        !SortProfiles.sameOrder_([1, -2], [1, 2]) &&
        !SortProfiles.sameOrder_([1], [1, 2]) &&
        empty.count == 1 && empty[0] == "",
        "sort order equality preserves sign and empty profiles keep an add row")
  }

  static testThemeIdentityAndLabels_() {
    var local = [["", "Same", null, null, null, null],
        ["local.ini", "Same", null, null, null, null]]
    var cloud = [CloudThemeFixture.new("cached", "Same", null, null, null, true, true, null),
        CloudThemeFixture.new("online", "Other", null, null, null, false, false, null)]
    var entries = SettingsMenu.installedThemeEntries_(local, cloud)
    var rows = SettingsMenu.themeRows_(entries)
    check_(entries.count == 3 && rows[0][0] == "classic" &&
        rows[1][0] == "file:local.ini" && rows[2][0] == "package:cached" &&
        rows[1][1] == "Same (local.ini)" &&
        rows[2][1] == "[Update] Same (cached)",
        "theme catalog preserves identity and disambiguates duplicate names")
    var online = SettingsMenu.onlineThemeEntries_(cloud)
    check_(online.count == 1 && online[0].package == "online",
        "online catalog excludes already cached packages")
  }

  static testUnicodeNames_() {
    check_(MenuUi.namesEqual("AbC", "aBc") &&
        MenuUi.namesEqual("", "") &&
        MenuUi.namesEqual("\u00c9", "e\u0301") &&
        MenuUi.namesEqual("Stra\u00dfe", "STRASSE") &&
        !MenuUi.namesEqual("a", "aa"),
        "name matching uses Unicode canonical caseless matching")
  }

  static testChoiceViewState_() {
    var state = ChoiceViewState.new()
    var first = MenuUi.choiceList_(state)
    first.bounds = Rect.new(1, 1, 20, 5)
    first.items = (0...20).map {|i| "old %(i)" }.toList
    first.selected = 10
    first.scrollTop = 8

    var second = MenuUi.choiceList_(state)
    second.items = (0...20).map {|i| "new %(i)" }.toList
    var fresh = MenuUi.choiceList_(null)
    check_(first == second && fresh != first && second.selected == 10 &&
           second.scrollTop == 8,
           "ChoiceViewState: repeated choices retain ListView viewport")
  }

  static testChoiceMessageUsesPaintedFont_() {
    var message = ChoiceMessage.new("AAA")
    message.theme = Theme.new({
      "default": Style.new(255, 0x07, 0xaaaaaa, 0x000000)
    }, Theme.default.glyphs)
    message.bounds = Rect.new(1, 1, 4, 1)
    message.draw()
    message.handle(KeyEvent.new(Key.end))
    check_(message.scrollTop == 2,
        "choice messages wrap with the font used to paint them")
  }

  static testBrowserMaximumHeight_() {
    check_(MenuUi.browserMaximumHeight_(25, 0) == 21 &&
           MenuUi.browserMaximumHeight_(25, 1) == 20,
           "MenuUi browser bottom clearance reserves the shadow row")
  }
}
