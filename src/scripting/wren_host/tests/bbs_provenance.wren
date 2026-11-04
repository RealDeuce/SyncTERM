// Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for IOErr, Profile
import "syncterm_menu" for Menu, MenuError, DirectoryEntry, BBSListSource

class ProvenanceTest {
  static check_(ok, message) {
    if (!ok) Fiber.abort(message)
  }
  static named_(name) {
    for (entry in Menu.entries) if (entry.name == name) return entry
    Fiber.abort("Missing entry: %(name)")
  }
  static stale_(action) {
    check_(Fiber.new { action.call() }.try() is String, "stale view accepted")
  }
  static readOnly_(entry) {
    check_(entry is DirectoryEntry && entry.readOnly && entry.profile.readOnly &&
        !entry.dirty && entry.cache == null, "hidden entry ownership")
    for (action in [Fn.new { entry.source = 0 },
        Fn.new { entry.sourceName = "changed" },
        Fn.new { entry.shadowed = null },
        Fn.new { entry.sortOrder = 123 },
        Fn.new { entry.profile.addr = "changed" }]) stale_(action)
    for (error in [entry.rename("Changed"), entry.save(), entry.delete()]) {
      check_(error is MenuError && error.code == IOErr.readOnly,
          "hidden mutation did not report read-only")
    }
  }
  static run() {
    check_(Menu.load(null) == null, "provenance load")
    var personal = named_("Personal")
    var shared = named_("Shared")
    var system = named_("SystemWeb")
    var named = named_("Named")
    check_(personal.source == BBSListSource.personal && personal.sourceName == null &&
        personal.shadowed == null, "personal provenance")
    check_(shared.source == BBSListSource.shared &&
        shared.sourceName.endsWith("provenance-shared.lst"), "shared provenance")
    check_(system.source == BBSListSource.webSystem &&
        system.sourceName == "System List", "system-web provenance")
    check_(named.source == BBSListSource.webNamed && named.sourceName == "Caf\u00e9",
        "named-web provenance")
    check_(personal.type == DirectoryEntry, "standard Object.type not restored")
    var overlap = named_("Overlap")
    var hidden = overlap.shadowed
    var hiddenProfile = hidden.profile
    check_(overlap.profile.addr == "personal.example" &&
        hidden.profile.addr == "shared.example" && hidden.source == BBSListSource.shared &&
        hidden.sourceName == shared.sourceName && hidden.shadowed == null,
        "native first-shadow retention")
    readOnly_(hidden)
    var dual = named_("Dual")
    var nestedSystem = dual.shadowed
    check_(nestedSystem.source == BBSListSource.webSystem && nestedSystem.shadowed == null,
        "shared entry shadows system web")
    var draft = Menu.create("Dual")
    var hiddenShared = draft.shadowed
    stale_(Fn.new { dual.source })
    check_(draft.source == BBSListSource.personal && draft.sourceName == null &&
        hiddenShared.source == BBSListSource.shared &&
        hiddenShared.shadowed.source == BBSListSource.webSystem &&
        nestedSystem.profile.addr == "system-dual.example", "working shadow chain")
    readOnly_(hiddenShared)
    readOnly_(hiddenShared.shadowed)
    var detached = hidden.profile.copy()
    check_(detached is Profile && !detached.readOnly, "explicit profile copy")
    detached.addr = "detached.example"
    var copy = Menu.copy(hidden, "Provenance Copy")
    check_(copy.source == BBSListSource.personal && copy.sourceName == null &&
        copy.shadowed == null && copy.profile.addr == "shared.example", "explicit entry copy")
    check_(copy.save() == null && Menu.sort() == null, "unrelated save/sort")
    System.gc()
    check_(hidden.sourceName == shared.sourceName &&
        hidden.profile.addr == "shared.example" &&
        hidden.searchKey == overlap.searchKey && nestedSystem.sourceName == "System List",
        "native rebuild invalidated model-owned provenance")
    check_(copy.delete() == null && draft.save() == null && draft.delete() == null,
        "persisted override restoration")
    stale_(Fn.new { hiddenShared.sourceName })
    stale_(Fn.new { dual.profile })
    check_(named_("Dual").source == BBSListSource.shared &&
        nestedSystem.source == BBSListSource.webSystem, "nested hidden view lost identity")
    check_(overlap.rename("Moved Provenance") == null && overlap.save() == null,
        "rename releases hidden entry")
    stale_(Fn.new { hidden.source })
    stale_(Fn.new { hiddenProfile.addr })
    check_(named_("Overlap").source == BBSListSource.shared &&
        named_("Overlap").sourceName == shared.sourceName &&
        detached.addr == "detached.example", "restored provenance or detached copy")
    check_(overlap.delete() == null, "renamed entry cleanup")
    var scratch = Menu.create("Provenance Draft")
    check_(scratch.source == BBSListSource.personal && scratch.sourceName == null &&
        scratch.shadowed == null && scratch.delete() == null, "unsaved provenance")
    __shared = shared
    hold_()
  }
  static shared { __shared }
  static hold_() {
    __visible = named_("Personal")
    __hidden = named_("Dual").shadowed
    __profile = __hidden.profile
    __copy = __profile.copy()
    __name = __hidden.sourceName
  }
  static retained_() {
    check_(__visible.source == BBSListSource.personal &&
        __hidden.sourceName == "System List" && __profile.addr == "system-dual.example",
        "failed load invalidated provenance")
  }
  static replaced_() {
    stale_(Fn.new { __visible.source })
    stale_(Fn.new { __hidden.sourceName })
    stale_(Fn.new { __hidden.shadowed })
    stale_(Fn.new { __profile.addr })
    check_(__name == "System List" && __copy.addr == "system-dual.example" &&
        !__copy.readOnly, "captured values did not survive reload")
    hold_()
    System.gc()
  }
}

ProvenanceTest.run()
