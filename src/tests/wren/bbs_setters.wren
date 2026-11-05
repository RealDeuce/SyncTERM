// BBS collection contracts, exercised in the normal application.
// Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
if (true) {
  import "syncterm_menu" for Menu
  if (Menu.load(null) != null) Fiber.abort("fixture load failed")
  var entry = Menu.entries[0]
  var bad = Fiber.new { entry.profile.palette = [99, "bad"] }
  bad.try()
  if (bad.error == null) Fiber.abort("invalid palette accepted")
  if (entry.profile.palette.count != 2 || entry.profile.palette[0] != 1 || entry.profile.palette[1] != 2 || entry.dirty) Fiber.abort("rejected palette changed entry")
}
if (true) {
  import "syncterm_menu" for Menu
  var entry = Menu.entries[0]
  var bad = Fiber.new { entry.profile.wrenScripts = ["x", false] }
  bad.try()
  if (bad.error == null) Fiber.abort("invalid scripts accepted")
  if (entry.profile.wrenScripts.count != 1 || entry.profile.wrenScripts[0] != "original" || entry.dirty) Fiber.abort("rejected scripts changed entry")
}
if (true) {
  import "syncterm_menu" for Menu
  var entry = Menu.entries[0]
  var bad = Fiber.new { entry.profile.palette = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16] }
  bad.try()
  if (bad.error == null || entry.profile.palette.count != 2 || entry.profile.palette[0] != 1 || entry.profile.palette[1] != 2 || entry.dirty) Fiber.abort("oversized palette changed entry")
}
if (true) {
  import "syncterm_menu" for Menu
  var entry = Menu.entries[0]
  var huge = "a"
  while (huge.bytes.count <= Menu.wrenScriptsMaxLength) huge = huge + "a"
  var bad = Fiber.new { entry.profile.wrenScripts = [huge] }
  bad.try()
  if (bad.error == null || entry.profile.wrenScripts.count != 1 || entry.profile.wrenScripts[0] != "original" || entry.dirty) Fiber.abort("oversized scripts changed entry")
}
if (true) {
  import "syncterm_menu" for Menu
  var entry = Menu.entries[0]
  for (name in ["syncterm_menu", "syncterm_picker", "picker_bootstrap"]) {
    var bad = Fiber.new { entry.profile.wrenScripts = ["ok", name] }
    bad.try()
    if (bad.error == null || entry.profile.wrenScripts[0] != "original" || entry.dirty) Fiber.abort("reserved name changed entry")
  }
}
if (true) {
  import "syncterm_menu" for Menu
  var entry = Menu.entries[0]
  entry.profile.encoding = 255
  if (entry.save() != null || Menu.load(null) != null || Menu.entries[0].profile.encoding != 255) Fiber.abort("entry encoding persistence")
  var defaults = Menu.defaults
  defaults.encoding = 12
  if (Menu.saveDefaults() != null || Menu.load(null) != null || Menu.defaults.encoding != 12) Fiber.abort("default encoding persistence")
}
if (true) {
  import "syncterm_menu" for Menu
  var entry = Menu.entries[0]
  if (entry.id < 0) Fiber.abort("identities")
  var id = entry.id
  entry.profile.connType = 11
  entry.profile.ghostProgram = "program"
  if (entry.profile.password != "program") Fiber.abort("ghost storage")
  entry.profile.password = "edited"
  entry.profile.wrenScripts = ["one", "two", "one"]
  entry.profile.bpsRate = 4294967295
  if (entry.profile.ghostProgram != "edited" || entry.profile.wrenScripts.count != 2 || entry.save() != null || entry.id != id) Fiber.abort("save model")
  if (Menu.load(null) != null || Menu.entries[0].profile.ghostProgram != "edited" || Menu.entries[0].profile.bpsRate != 4294967295) Fiber.abort("reload model")
}
if (true) {
  import "syncterm_menu" for Menu
  var quick = Menu.quickConnect("ghost://example.invalid/program")
  if (quick.readOnly || quick.ghostProgram != "program") Fiber.abort("quick GHost")
  var copy = Menu.copy(quick, "ZZGhost")
  if (copy == null || copy.id != -1 || copy.profile.password != "program" || copy.save() != null || copy.id < 0) Fiber.abort("copy GHost")
  if (Menu.load(null) != null) Fiber.abort("reload GHost")
  copy = Menu.entries.where {|entry| entry.name == "ZZGhost" }.toList[0]
  if (copy.profile.ghostProgram != "program" || copy.delete() != null) Fiber.abort("persist GHost")
}
if (true) {
  import "syncterm_menu" for Menu
  var encoding = Menu.sortFields.where {|row| row[0] == 32 }.toList
  if (encoding.count != 1 || encoding[0][1] != "Encoding" || encoding[0][2]) Fiber.abort("encoding sort catalog")
  var index = Menu.sortProfiles.count
  if (Menu.addSortProfile(index, "Encoding", [32, 1]) != null) Fiber.abort("encoding sort add")
  if (Menu.setActiveSortProfile(index) != null) Fiber.abort("encoding sort select")
  if (Menu.updateSortProfile(index, "Encoding", [-32, 1]) != null || Menu.sortProfiles[index][1][0] != -32) Fiber.abort("encoding sort reverse")
  var all = Menu.sortFields.where {|row| row[0] != 30 }.map {|row| row[0] }.toList
  if (all.count != 31 || Menu.updateSortProfile(index, "Encoding", all) != null) Fiber.abort("all sort fields")
  if (Menu.deleteSortProfile(index) != null) Fiber.abort("encoding sort cleanup")
}

if (true) {
  import "syncterm" for GraphicsMode
  import "syncterm_menu" for Menu
  for (mode in [GraphicsMode.skypix13, GraphicsMode.skypix31, GraphicsMode.none, GraphicsMode.v1, GraphicsMode.v3]) {
    var entry = Menu.entries[0]
    entry.profile.graphics = mode
    if (entry.save() != null || Menu.load(null) != null || Menu.entries[0].profile.graphics != mode) Fiber.abort("entry graphics persistence")
    Menu.defaults.graphics = mode
    if (Menu.saveDefaults() != null || Menu.load(null) != null || Menu.defaults.graphics != mode) Fiber.abort("default graphics persistence")
  }
}

if (true) {
  import "syncterm_menu" for Menu
  var entry = Menu.entries[0]
  entry.profile.graphics = 3
  entry.profile.screenMode = 3
  entry.profile.encoding = 255
  entry.profile.amigaFont = "Ruby.font"
  entry.profile.amigaFontSize = 12
  if (entry.save() != null || Menu.load(null) != null) Fiber.abort("SkyPix preferences save")
  var profile = Menu.entries[0].profile
  if (profile.screenMode != 3 || profile.encoding != 255 || profile.amigaFont != "Ruby.font" || profile.amigaFontSize != 12) Fiber.abort("SkyPix preferences reload")
}
