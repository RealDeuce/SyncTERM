// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for Hook, Key, Transfer, Host, BBS, ConnType
import "transfer_pick" for TransferFlow
if (!Host.safeMode && BBS.connType != ConnType.mqtt) {
  Hook.onKey(Key.altU) {|key|
    TransferFlow.upload()
    return true
  }
  Hook.onKey(Key.altD) {|key|
    TransferFlow.download()
    return true
  }
  Hook.every(50) { TransferFlow.automatic() }
  Hook.onDisconnect { Transfer.endSession() }
}
