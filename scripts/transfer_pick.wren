// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for Transfer, Host, BBS, CTerm, Emulation, Screen, Input,
                       Key, KeyEvent, Timer, FilePickerOptions
import "ui_app" for App
import "ui_picker" for ListPicker
import "ui_popup" for Alert, LinePrompt
import "transfer_app" for TransferApp

class TransferFlow {
  static busy { __busy ? true : false }
  static uploadRows {
    return [["zmodem", "ZMODEM", false], ["zmodem", "ZMODEM Batch", true],
      ["ymodem", "YMODEM", false], ["ymodem", "YMODEM Batch", true],
      ["xmodem-1k", "XMODEM-1K", false], ["xmodem-128", "XMODEM-128", false],
      ["ascii", "ASCII", false], ["raw", "Raw", false]]
  }
  static downloadRows {
    var rows = [["zmodem", "ZMODEM"], ["ymodem-g", "YMODEM-g"],
      ["ymodem", "YMODEM"], ["xmodem-crc", "XMODEM-CRC"],
      ["xmodem-chksum", "XMODEM-CHKSUM"]]
    if (CTerm.emulation == Emulation.prestel) rows.add(["cet", "CET Telesoftware"])
    return rows
  }
  static launch_(work) {
    if (busy || Host.safeMode) return
    __busy = true
    Fiber.new {
      var result = Fiber.new { work.call() }.try()
      if (Transfer.active) Transfer.endSession()
      Transfer.cancelAuto_()
      __busy = false
      Input.setupMouseEvents()
      if (result is String) Screen.modalRun(Fn.new {
        Alert.runStandalone(App.new(), "File Transfer", result)
      })
    }.call()
  }
  static upload() { upload(false, 0) }
  static upload(autoZ, lastCh) {
    launch_(Fn.new {
      var rows = uploadRows
      var selected = 0
      if (!autoZ) Screen.modalRun(Fn.new {
        selected = ListPicker.pick("Upload Protocol", uploadHelp, rows.map {|row| row[1] }.toList)
      })
      if (selected < 0) return
      var row = rows[selected]
      var files = null
      Screen.modalRun(Fn.new {
        if (row[2]) {
          files = Host.pickFiles(BBS.ulDir, null, 0)
        } else {
          var file = Host.pickFile(BBS.ulDir, null, FilePickerOptions.allowEntry)
          if (file != null) files = [file]
        }
      })
      if (files == null || files.count == 0) return
      if (row[2]) {
        Transfer.uploadBatch(row[0], files.map {|file| file.toString }.toList, lastCh)
      } else {
        Transfer.upload(row[0], files[0].toString, lastCh)
      }
      if (Transfer.inlineUpload) {
        inline_()
      } else {
        TransferApp.new(row[1] + " Upload").run()
      }
    })
  }
  static download() {
    launch_(Fn.new {
      var rows = downloadRows
      var selected = -1
      Screen.modalRun(Fn.new {
        selected = ListPicker.pick("Download Protocol", downloadHelp, rows.map {|row| row[1] }.toList)
      })
      if (selected < 0) return
      var row = rows[selected]
      var filename = null
      if (row[0] == "xmodem-crc" || row[0] == "xmodem-chksum") {
        Screen.modalRun(Fn.new {
          filename = LinePrompt.runStandalone(App.new(), "Download Filename", "")
        })
        if (filename == null || filename == "") return
      }
      Transfer.download(row[0], filename)
      TransferApp.new(row[1] + " Download").run()
    })
  }
  static automatic() {
    if (busy) return
    var pending = Transfer.autoPending_
    if (pending == "upload") {
      upload(true, 0)
    } else if (pending == "download") {
      launch_(Fn.new { TransferApp.new("ZMODEM Download").run() })
    }
  }
  static inline_() {
    var claim = Input.pushClaim(Fn.new {|event|
      if (event is KeyEvent && (event.code == Key.escape || event.code == Key.ctrlC || event.code == Key.ctrlX)) Transfer.requestAbort()
      return true
    })
    while (!Transfer.done) {
      Transfer.drainLog()
      Timer.trigger(Fiber.current, 50)
      Fiber.yield()
    }
    Transfer.drainLog()
    var ok = Transfer.success
    claim.pop()
    Transfer.endSession()
    if (!ok) Screen.modalRun(Fn.new {
      Alert.runStandalone(App.new(), "Upload", "Upload failed or was cancelled. See the session log for details.")
    })
  }
  static uploadHelp {
    return "# Upload Protocol\n\nZMODEM and YMODEM carry filenames and support batches. " +
      "XMODEM sends 128-byte or 1K blocks; the receiver selects CRC or checksum. " +
      "ASCII normalizes LF to CRLF. Raw sends exact bytes. Esc cancels an upload."
  }
  static downloadHelp {
    return "# Download Protocol\n\nZMODEM supports batches and verified resume. " +
      "YMODEM carries filenames and lengths; YMODEM-g streams without retries. " +
      "XMODEM requires a filename and retains final-block padding. " +
      "CET telesoftware is available in Prestel mode. Esc cancels a transfer."
  }
}
// Historical entry points retained for scripts that invoke the selection flows.
class UploadApp {
  static run(autoZ, lastCh) { TransferFlow.upload(autoZ, lastCh) }
}
class DownloadApp {
  static run() { TransferFlow.download() }
}
