// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause

// Alt+L sends the credentials for the active directory entry.
import "syncterm" for Hook, Key, Conn, Session, CTerm, ConnType, KeyEvent

class Connected {
  static sendComponent_(value) {
    if (value.count == 0) return
    var error = Conn.sendText(value).await()
    if (error != null) Fiber.abort(error)
    CTerm.sendKey(KeyEvent.new(Key.enter))
  }

  static sendLogin() {
    var ct = Session.current.profile.connType
    var sendUser = ct != ConnType.rlogin &&
                   ct != ConnType.rloginReversed &&
                   ct != ConnType.ssh &&
                   ct != ConnType.sshNoAuth
    var sendPass = ct != ConnType.rlogin &&
                   ct != ConnType.rloginReversed &&
                   ct != ConnType.ssh

    if (sendUser) sendComponent_(Session.current.profile.user)
    if (sendPass) sendComponent_(Session.current.profile.password)
    sendComponent_(Session.current.profile.syspass)
  }
}

Hook.onCommand_(108) { |key|
  Fiber.new { Connected.sendLogin() }.call()
  return true
}
