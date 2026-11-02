// Optional Modern UI entry point. This file is never embedded in SyncTERM.
// Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import "modern_ui" for ModernUI
import "modern_ui_model" for ModernModel

class MainMenu {
  static prepare() { ModernModel.prepare() }
  static run(current, connected) { ModernUI.run(current, connected) }
  static offerSave(source, name) { ModernModel.offerSave(source, name) }
  static registerCommand(key, modifiers, keyName, description, callback) {
    ModernUI.registerCommand(key, modifiers, keyName, description, callback)
  }
  static registerCommand(key, keyName, description, callback) {
    ModernUI.registerCommand(key, keyName, description, callback)
  }
}

ModernUI.splash()
