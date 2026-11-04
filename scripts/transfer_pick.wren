// Copyright (c) 2026 Stephen Hurd
// SPDX-License-Identifier: BSD-2-Clause
import "syncterm" for Transfer, Host, Session, CTerm, Emulation, Screen, Input, Mod,
                       Key, KeyEvent, Timer, FilePickerOptions, Error, Cleanup_
import "ui_app" for App
import "ui_picker" for ListPicker
import "ui_popup" for Alert, LinePrompt, Confirm
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
      ["ymodem", "YMODEM"], ["xmodem-g", "XMODEM-G"], ["xmodem-crc", "XMODEM-CRC"],
      ["xmodem-chksum", "XMODEM-CHKSUM"]]
    if (CTerm.emulation == Emulation.prestel) rows.add(["cet", "CET Telesoftware"])
    return rows
  }
  static launch_(work) {
    if (busy || Host.safeMode) return
    __busy = true
    Fiber.new {
      var task = Fiber.new { work.call() }
      // Picker and App callbacks can yield before startup fails. Route each
      // registered resume through ordinary try, retaining the original error.
      Cleanup_.try_(task)
      var result = task.error
      if (Transfer.active) Transfer.endSession()
      Transfer.cancelAuto_()
      __busy = false
      if (result is String || result is Error) Screen.checked_(Screen.modalRun(Fn.new {
        Alert.runStandalone(App.new(), "File Transfer", result is Error ? (result.message || result.toString) : result)
      }))
    }.call()
  }
  static upload() { upload(false, 0) }
  static upload(autoZ, lastCh) {
    launch_(Fn.new {
      var rows = uploadRows
      var selected = 0
      if (!autoZ) Screen.checked_(Screen.modalRun(Fn.new {
        selected = ListPicker.pick("Upload Protocol", uploadHelp, rows.map {|row| row[1] }.toList)
      }))
      if (selected < 0) return
      var row = rows[selected]
      var files = null
      Screen.checked_(Screen.modalRun(Fn.new {
        if (row[2]) {
          files = Host.pickFiles(Session.current.profile.ulDir, null, 0)
        } else {
          var file = Host.pickFile(Session.current.profile.ulDir, null, FilePickerOptions.allowEntry)
          if (file != null) files = [file]
        }
      }))
      if (files == null || files.count == 0) return
      var error = null
      if (row[2]) {
        error = Transfer.uploadBatch(row[0], files, lastCh)
      } else {
        error = Transfer.upload(row[0], files[0], lastCh)
      }
      if (error != null) Fiber.abort(error)
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
      Screen.checked_(Screen.modalRun(Fn.new {
        selected = ListPicker.pick("Download Protocol", downloadHelp, rows.map {|row| row[1] }.toList)
      }))
      if (selected < 0) return
      var row = rows[selected]
      var filename = null
      if (row[0] == "xmodem-crc" || row[0] == "xmodem-chksum" || row[0] == "xmodem-g") {
        Screen.checked_(Screen.modalRun(Fn.new {
          filename = LinePrompt.runStandalone(App.new(), "Download Filename", "")
        }))
        if (filename == null || filename == "") return
      }
      var error = Transfer.download(row[0], filename)
      if (error != null) Fiber.abort(error)
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
    } else if (pending == "rip-download") {
      launch_(Fn.new {
        if (Transfer.inlineProgress) {
          inline_()
        } else {
          var app = TransferApp.new("RIP Asset Download")
          app.waitTimeoutMs = 0
          app.run()
        }
      })
    } else if (pending == "rip-upload") {
      launch_(Fn.new {
        var allow = false
        Screen.checked_(Screen.modalRun(Fn.new {
          allow = Confirm.runStandalone(App.new(), "The host requests an upload of cached RIP file %(Transfer.ripUploadName_). Allow?")
        }))
        if (!allow) return
        var error = Transfer.acceptRipUpload_()
        if (error != null) Fiber.abort(error)
        var app = TransferApp.new("RIP Asset Upload")
        app.waitTimeoutMs = 0
        app.run()
      })
    }
  }
  static inline_() {
    var claim = Input.pushClaim(Fn.new {|event|
      if (event is KeyEvent && (event.shortcut_ == Key.escape || event.shortcut_ == Key.chord_(99, Mod.control) || event.shortcut_ == Key.chord_(120, Mod.control))) Transfer.requestAbort()
      return true
    })
    while (!Transfer.done) {
      Transfer.drainLog()
      Timer.after(50).await()
    }
    Transfer.drainLog()
    var result = Transfer.result
    claim.pop()
    Transfer.endSession()
    if (result != null) Screen.checked_(Screen.modalRun(Fn.new {
      Alert.runStandalone(App.new(), "File Transfer", result.message)
    }))
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
      "XMODEM-G streams without retries. " +
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
