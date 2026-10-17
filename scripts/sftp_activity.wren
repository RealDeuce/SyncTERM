// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for CTerm

// Built-in SFTP clients must not clear another client's session hold.
class SftpActivity {
  static set(owner, active) {
    if (__owners == null) __owners = {}
    if (active) {
      __owners[owner] = true
    } else {
      __owners.remove(owner)
    }
    CTerm.sftpActive = __owners.count > 0
  }
}
