// Self-tests for menu_ui.

import "ui_widget" for Rect
import "menu_ui" for ChoiceViewState, MenuUi
import "menu_settings_ui" for SettingsMenu
import "menu_sort_profiles" for SortProfiles
import "syncterm_menu" for Menu

class MenuUiTest {
  static run() {
    __pass = 0
    __fail = 0
    System.print("=== menu_ui self-test starting ===")

    testChoiceViewState_()
    testBrowserMaximumHeight_()
    testThemeIdentityAndLabels_()
    testAsciiNames_()
    testSortFieldLabels_()
    testSortOrderComparison_()

    var total = __pass + __fail
    System.print("=== menu_ui: %(total) tests, %(__pass) pass, " +
        "%(__fail) fail ===")
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
    var cloud = [["cached", "Same", null, null, null, true, true, null],
        ["online", "Other", null, null, null, false, false, null]]
    var entries = SettingsMenu.installedThemeEntries_(local, cloud)
    var rows = SettingsMenu.themeRows_(entries)
    check_(entries.count == 3 && rows[0][0] == "classic" &&
        rows[1][0] == "file:local.ini" && rows[2][0] == "package:cached" &&
        rows[1][1] == "Same (local.ini)" &&
        rows[2][1] == "[Update] Same (cached)",
        "theme catalog preserves identity and disambiguates duplicate names")
    var online = SettingsMenu.onlineThemeEntries_(cloud)
    check_(online.count == 1 && online[0][0] == "online",
        "online catalog excludes already cached packages")
  }

  static testAsciiNames_() {
    check_(MenuUi.namesEqual("AbC", "aBc") &&
        MenuUi.namesEqual("", "") &&
        !MenuUi.namesEqual("\u00c9", "\u00e9") &&
        !MenuUi.namesEqual("a", "aa"),
        "name matching folds ASCII only without locale-dependent casing")
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

  static testBrowserMaximumHeight_() {
    check_(MenuUi.browserMaximumHeight_(25, 0) == 21 &&
           MenuUi.browserMaximumHeight_(25, 1) == 20,
           "MenuUi browser bottom clearance reserves the shadow row")
  }
}
