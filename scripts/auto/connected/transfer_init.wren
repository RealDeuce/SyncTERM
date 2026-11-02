// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for Hook, Key, Transfer, Host, BBS, ConnType, Mod
import "transfer_pick" for TransferFlow
if (!Host.safeMode && BBS.connType != ConnType.mqtt) {
  Hook.onCommand_(117) {|key|
    TransferFlow.upload()
    return true
  }
  Hook.onCommand_(100) {|key|
    TransferFlow.download()
    return true
  }
  Hook.every(50) { TransferFlow.automatic() }
  Hook.onDisconnect { Transfer.endSession() }
}
