// SFTP transfer queue — Wren replacement for the C-side
// sftp_queue.c.  Holds the list of jobs, persists them to the
// per-BBS Cache directory as `sftp_queue.won`, and exposes
// snapshot / cancel / status APIs to consumers (the queue screen
// App, the SFTP browser's status chip, the degraded modal, etc.).
//
// Two worker fibers (one per direction) drive the actual transfers:
// they claim a QUEUED job, open the local + remote endpoints, copy
// in CHUNK-sized blocks while watching the cancel flag, and post
// the terminal status.  Concurrency is intentionally 1-up + 1-down
// (matches the C-side queue).  Persistence runs on every state
// transition; the in-flight `done` byte counter is best-effort
// (caught up by the disconnect-time stop()).
//
// All state is class-static (mirroring the C side's file-static
// state).  Single-session singleton — `start()` is called once on
// connect by sftp_queue_init.wren; `stop()` runs on disconnect.

import "syncterm" for Cache, CTerm, Download, File, FileError, IOErr, FileFlag,
                       Format, Host, SFTP, SFTPError, SFTPStat, Timer, Session,
					   Wake, WON, WONError

// One in-flight or terminal-state job.  Mutable in place; serialized
// from `persistNow_()`.  `cancel` is the volatile flag the workers
// poll between chunks (matches the C `volatile bool cancel`).
//
// `localToken` is the picker consent token (Wren `File.token`)
// minted by Host.pickFiles — required for uploads, since the worker
// re-opens the source file via Host.openLocalFile on resume.  Null
// for download jobs.
class Job {
  // Sliding-window throughput estimation parameters.  WIN_SECS is
  // the look-back horizon: rate is (newest.bytes - oldest.bytes) /
  // (newest.t - oldest.t) over samples within this window.  Long
  // enough to smooth chunk-to-chunk jitter, short enough that the
  // displayed rate tracks the *current* speed rather than the
  // lifetime average.  SAMPLE_GAP is the minimum spacing between
  // recorded samples — caps the buffer at WIN_SECS / SAMPLE_GAP
  // entries on fast links (no growth on slow links either).
  static WIN_SECS   { 3.0 }
  static SAMPLE_GAP { 0.1 }

	construct new(id, dir, local, remote, total, localToken) {
	_id          = id
    _dir         = dir
    _local       = local
    _remote      = remote
    _total       = total
    _done        = 0
	_resume      = 0
	_sourceSize  = 0
	_sourceMtime = 0
    _status      = "queued"
    _errMsg      = ""
    _cancel      = false
    _localToken  = localToken
    // Sliding window of [time, bytes] samples for throughput /
    // ETA display.  Empty until the job goes ACTIVE; samples
    // outside WIN_SECS evict on insert.  Not persisted — purely
    // transient display state.
    _samples     = []
  }
  dir            { _dir          }
	id             { _id           }
  local          { _local        }
  remote         { _remote       }
  total          { _total        }
  total=(v)      { _total = v    }
  done           { _done         }
  done=(v)       { _done = v     }
	resume         { _resume       }
	resume=(v)     { _resume = v   }
	sourceSize     { _sourceSize   }
	sourceSize=(v) { _sourceSize = v }
	sourceMtime    { _sourceMtime  }
	sourceMtime=(v) { _sourceMtime = v }
  status         { _status       }
  status=(s)     { _status = s   }
  errMsg         { _errMsg       }
  errMsg=(m)     { _errMsg = m   }
  cancel         { _cancel       }
  cancel=(b)     { _cancel = b   }
  localToken     { _localToken   }
  samples        { _samples      }

  // Drop every sample.  Called on QUEUED → ACTIVE so a resumed job
  // doesn't inherit a stale window from a previous session.
  resetSamples() { _samples = [] }

  // Record the current (Timer.now, _done) as a window sample.
  // Insert is rate-limited to SAMPLE_GAP so a 100 Mbps link with
  // 30 KiB chunks doesn't bloat the list — at most WIN_SECS /
  // SAMPLE_GAP = 30 entries are ever live.  Older entries (outside
  // WIN_SECS) drop from the front.
  recordSample() {
    var now = Timer.now
    if (_samples.count > 0 && now - _samples[-1][0] < Job.SAMPLE_GAP) return
    _samples.add([now, _done])
    var cutoff = now - Job.WIN_SECS
    while (_samples.count > 1 && _samples[0][0] < cutoff) {
      _samples.removeAt(0)
    }
  }
}

// Immutable point-in-time copy of a Job — what `snapshot` returns
// so consumers can render off a stable view without holding a
// reference into the live list.
//
// `bpsStr` and `etaStr` are pre-formatted display strings; they are
// non-empty only when the sliding window holds enough recent samples
// to compute a stable rate.  Callers should treat empty strings as
// "no estimate available" rather than zero.
class JobSnap {
  construct new(j) {
	_id     = j.id
    _dir    = j.dir
    _local  = j.local
    _remote = j.remote
    _total  = j.total
    _done   = j.done
    _status = j.status
    _errMsg = j.errMsg
    _bpsStr = ""
    _etaStr = ""
    var s = j.samples
    if (s.count >= 2) {
      var oldest  = s[0]
      var newest  = s[-1]
      var elapsed = newest[0] - oldest[0]
      var moved   = newest[1] - oldest[1]
      // Sub-100ms windows or zero-byte moves produce noisy or
      // undefined rates; leave the display blank instead.
      if (elapsed >= 0.1 && moved > 0) {
        var bps = moved / elapsed
        _bpsStr = Format.bytes(bps) + "/s"
        var remain = j.total - j.done
        if (remain > 0) {
          // Floor the ETA seconds before formatting — duration_
          // estimate_to_str's seconds branch uses %g, which would
          // render sub-second values as scientific notation
          // ("1.2e-06s") and we don't want that resolution anyway.
          _etaStr = Format.duration((remain / bps).floor)
        }
      }
    }
  }
  dir    { _dir    }
	id     { _id     }
  local  { _local  }
  remote { _remote }
  total  { _total  }
  done   { _done   }
  status { _status }
  errMsg { _errMsg }
  bpsStr { _bpsStr }
  etaStr { _etaStr }
}

// Internal — coordinator for one in-flight job's pipelined transfer.
// Shared between the dispatcher fiber (runDownload_ / runUpload_) and
// the N chunk-worker fibers it spawns; the dispatcher parks on
// `await()` while the workers grind through `claimChunk()` slots and
// the last one out posts a Wake to wake it up.
//
// All mutation runs on Wren's single fiber thread, so the read-modify-
// write of `_nextOff` inside `claimChunk()` is atomic without any
// extra locking — no two fibers can be mid-method simultaneously.
class JobCtx {
  construct new(job, totalSize, lf, hRemote, depth) {
    _job       = job
    _totalSize = totalSize
    _lf        = lf
    _hRemote   = hRemote
    _depth     = depth
	_nextOff   = job.resume
	_frontier  = job.resume
	_completed = {}
    _alive     = 0
    _waiter    = null
    _err       = null
  }

  job        { _job        }
  hRemote    { _hRemote    }
  lf         { _lf         }
  err        { _err        }
  depth      { _depth      }

	// Publish only a completely filled claimed interval.  Out-of-order
	// completions remain bounded by pipeline depth; persisted resume moves
	// only across a contiguous prefix.
	complete(off, size) {
	  _completed[off] = size
	  while (_completed.containsKey(_frontier)) {
		var count = _completed[_frontier]
		_completed.remove(_frontier)
		_frontier = _frontier + count
	  }
	  _job.resume = _frontier
	  SftpQueue.persistProgress_()
	}

  // Reserve the next chunk's [offset, size] for the calling worker.
  // Returns null at end-of-file, on prior error, or when cancel /
  // suspend has been requested — the worker exits its loop in any
  // of those cases.  Cancel and suspend latch onto _err here so the
  // dispatcher can branch on the cause.
  claimChunk(want) {
    if (_err != null) return null
    if (_job.cancel) {
      _err = "cancelled"
      return null
    }
    if (SftpQueue.suspended) {
      _err = "suspended"
      return null
    }
    if (_nextOff >= _totalSize) return null
    var off = _nextOff
    var sz  = want
    if (off + sz > _totalSize) sz = _totalSize - off
    _nextOff = _nextOff + sz
    return [off, sz]
  }

  // First error wins; later setError calls are ignored so the
  // dispatcher's failure message reflects the original cause.
  setError(e) {
    if (_err == null) _err = e
  }

  // Increment alive count BEFORE spawning a worker; the worker is
  // responsible for calling workerExit() exactly once.
  bumpAlive() { _alive = _alive + 1 }

  // Last worker out posts a wake to the dispatcher (if it actually
  // parked in await(); _waiter stays null when no worker ever
  // yielded, in which case await() short-circuits).
  workerExit() {
    _alive = _alive - 1
    if (_alive == 0 && _waiter != null) {
      Wake.post(_waiter, null)
    }
  }

  // Park the dispatcher until every worker has exited.  Idempotent
  // for the all-already-done case (workers that completed entirely
  // during their first .call() never yielded, so _waiter was null
  // and no wake was queued — there's nothing to drain).
  await() {
    if (_alive == 0) return
    _waiter = Fiber.current
    Fiber.yield()
    _waiter = null
  }
}

class SftpQueue {
  // ----- direction constants -----------------------------------------
  static UPLOAD   { "up"   }
  static DOWNLOAD { "down" }

  // ----- status constants --------------------------------------------
  static QUEUED    { "queued"    }
  static ACTIVE    { "active"    }
  static DONE      { "done"      }
  static FAILED    { "failed"    }
  static CANCELLED { "cancelled" }

  // Persistence file name in the per-BBS Cache directory.  Matches
  // the sandbox filename policy (1..64 chars, [A-Za-z0-9._-]).
  static FILENAME { "sftp_queue.won" }

  // Chunk size bounds.  CHUNK is the maximum (30 KiB — the largest
  // payload that fits in DeuceSSH's 32 KB channel max packet after
  // SFTP framing) and the bootstrap default before the EWMA
  // estimators have any data.  MIN_CHUNK is the floor on extreme
  // sub-broadband links — below 4 KiB the SFTP per-request overhead
  // starts to dominate.  See `adaptiveChunkSize` for the live policy.
  static CHUNK     { 30720 }
  static MIN_CHUNK { 4096  }

  // Pipeline depth bounds.  PIPELINE_DEPTH is the bootstrap default
  // (used until we've seen at least one chunk land); MIN/MAX clamp
  // the live `adaptiveDepth` value.  Working-set ceiling is
  // MAX_DEPTH * CHUNK = ~960 KiB.
  static PIPELINE_DEPTH { 8  }
  static MIN_DEPTH      { 2  }
  static MAX_DEPTH      { 32 }

  // Asymmetric EWMA constants.  α_bad applies on the "bad" direction
  // (RTT increasing or BW dropping); α_good applies on the "good"
  // direction (RTT dropping or BW rising).  4× faster reaction on
  // bad than good — congestion gets recognised quickly, recovery is
  // cautious so we don't punch a recovering session in the face.
  // 90% convergence: ~8 samples (bad) vs ~36 samples (good).
  static EWMA_ALPHA_BAD  { 0.25   }
  static EWMA_ALPHA_GOOD { 0.0625 }

  // Per-chunk transmission budget for keystroke responsiveness.
  // The adaptive chunk size targets `bw * 75 ms` so a single chunk's
  // wire time stays under our interactivity budget.
  static CHUNK_BUDGET_S { 0.075 }

  // Idle poll cadence for worker fibers when no jobs are claimable.
  static IDLE_POLL_MS { 250 }

  // ----- lifecycle ---------------------------------------------------

  // Idempotent.  Initializes class-static state, loads any jobs
  // persisted from a previous session, and spawns the two worker
  // fibers (one per direction).  After this returns, the workers
  // have entered their idle-poll loop on the result-queue side and
  // are ready to claim work.
  static start() {
    if (__running) return
    __jobs        = []
    __gen         = 0
	__progressGen = 0
	__nextId      = 1
	__fatal       = false
    __running     = true
    __workersStarted = false
    __suspended   = false
    __shellClosed = false
    // Adaptive sizing state.  Null until the first chunk completes
    // and seeds them; bootstrap defaults (PIPELINE_DEPTH / CHUNK) are
    // used in the interim.  Reset at session start since each remote
    // (and link path) has its own characteristics.
    __ewmaRtt     = null
    __ewmaBps     = null
	if (!load_()) {
	  __fatal = true
	  signalActive_()
	  return
	}
    signalActive_()
  }

  // Save and clear.  Called from the disconnect path; subsequent
  // queries return safe defaults.  The worker fibers detect
  // __running == false on their next iteration and exit cleanly.
  static stop() {
    if (!__running) return
    persistNow_()
    __running     = false
    __suspended   = false
    __shellClosed = false
    __ewmaRtt     = null
    __ewmaBps     = null
    __jobs        = []
    Host.uploadArrow   = false
    Host.downloadArrow = false
    holdSession_(false)
  }

  // Halt workers without touching job state.  Workers see __suspended
  // at their loop top and the chunk loop's per-iteration check, exit
  // without flipping ACTIVE → terminal, so jobs stay ACTIVE in the
  // queue and persist that way at the next stop().  Used by:
  //   - SftpApp's Esc handler in the shell-closed state (user
  //     "I'm done for now, finish on next connect" signal)
  //   - the worker chunk loops when an SFTP error occurs while
  //     shellClosed is set (server tore down the SFTP channel
  //     when the shell exited; preserve the in-flight ACTIVE job
  //     for the next session rather than marking it FAILED)
  // Idempotent.
  static suspend() {
    if (!__running) return
    __suspended = true
    signalActive_()
  }

  // True after suspend() has been called; consumed by SftpApp's
  // tick to auto-quit once the workers have wound down.
  static suspended { __suspended ? true : false }

  // Set by Hook.onShellClose (sftp_queue_init.wren).  Workers
  // consult this on SFTP errors: when set, treat the error as a
  // shell-induced session end (suspend, leave ACTIVE for resume)
  // rather than a hard FAILED.
  static shellClosed { __shellClosed ? true : false }
  static shellClosed=(b) { __shellClosed = b }

  // Keep this queue's own session lease while work can still progress.
  static holdSession_(active) {
    if (active && __sessionLease == null) __sessionLease = Session.current.hold()
    if (!active && __sessionLease != null) {
      __sessionLease.release()
      __sessionLease = null
    }
  }

  static signalActive_() {
    if (__suspended) {
      holdSession_(false)
      return
    }
    holdSession_(hasWork)
    if (hasWork && !__workersStarted && !__fatal) {
      __workersStarted = true
      Fiber.new {
        var ready = SFTP.start().await()
        if (ready is SFTPError) {
          suspend()
          return
        }
        if (__running && !__suspended) spawnWorkers_()
      }.call()
    }
  }

  // ----- public state queries ----------------------------------------

  // Returns a fresh List<JobSnap>; never returns the live list.
  static snapshot {
    var out = []
    if (!__running) return out
    for (j in __jobs) {
      out.add(JobSnap.new(j))
    }
    return out
  }

  // Generation counter — bumps on enqueue and every state
  // transition.  Consumers cache it and rebuild only on change.
  static gen { __running ? __gen : 0 }
	static progressGen { __running ? __progressGen : 0 }
	static fatal { __fatal ? true : false }

  // ----- adaptive sizing --------------------------------------------

  // Smoothed estimates exposed for diagnostics / future UI.  Null
  // until at least one chunk has completed and seeded the EWMAs.
  static ewmaRtt { __ewmaRtt }
  static ewmaBps { __ewmaBps }

  // Push a per-chunk RTT sample into the asymmetric EWMA.  RTT
  // increasing is "bad" → fast α; decreasing is "good" → slow α.
  // First sample seeds the estimator without smoothing.
  static recordRtt_(seconds) {
    if (seconds <= 0) return
    if (__ewmaRtt == null) {
      __ewmaRtt = seconds
      return
    }
    var alpha = (seconds > __ewmaRtt) ? SftpQueue.EWMA_ALPHA_BAD : SftpQueue.EWMA_ALPHA_GOOD
    __ewmaRtt = alpha * seconds + (1 - alpha) * __ewmaRtt
  }

  // Push a per-chunk BW sample into the asymmetric EWMA.  BW dropping
  // is "bad" → fast α; rising is "good" → slow α (cautious recovery
  // so we don't punch a recovering link in the face).  First sample
  // seeds the estimator without smoothing.
  static recordBps_(bps) {
    if (bps <= 0) return
    if (__ewmaBps == null) {
      __ewmaBps = bps
      return
    }
    var alpha = (bps < __ewmaBps) ? SftpQueue.EWMA_ALPHA_BAD : SftpQueue.EWMA_ALPHA_GOOD
    __ewmaBps = alpha * bps + (1 - alpha) * __ewmaBps
  }

  // Adaptive chunk size — targets `bw * CHUNK_BUDGET_S` so per-chunk
  // wire time stays under the keystroke budget.  Falls back to CHUNK
  // before the BW estimator has any data; clamped to [MIN_CHUNK,
  // CHUNK].
  static adaptiveChunkSize {
    if (__ewmaBps == null) return SftpQueue.CHUNK
    var c = (__ewmaBps * SftpQueue.CHUNK_BUDGET_S).floor
    if (c < SftpQueue.MIN_CHUNK) return SftpQueue.MIN_CHUNK
    if (c > SftpQueue.CHUNK)     return SftpQueue.CHUNK
    return c
  }

  // Adaptive pipeline depth — sized to keep the bandwidth-delay
  // product in flight, plus one slot of headroom.  Falls back to the
  // bootstrap PIPELINE_DEPTH while estimators are uninitialized;
  // clamped to [MIN_DEPTH, MAX_DEPTH].
  static adaptiveDepth {
    if (__ewmaBps == null || __ewmaRtt == null) return SftpQueue.PIPELINE_DEPTH
    var bdp = __ewmaBps * __ewmaRtt
    var d   = (bdp / SftpQueue.adaptiveChunkSize).ceil + 1
    if (d < SftpQueue.MIN_DEPTH) return SftpQueue.MIN_DEPTH
    if (d > SftpQueue.MAX_DEPTH) return SftpQueue.MAX_DEPTH
    return d
  }

  // True if any QUEUED or ACTIVE job exists.  Used by the C-side
  // bridge to decide whether to block teardown.
  static hasWork {
    if (!__running) return false
    for (j in __jobs) {
      if (j.status == SftpQueue.QUEUED || j.status == SftpQueue.ACTIVE) return true
    }
    return false
  }

  // Status of a specific (dir, remote) job, or null if not in queue.
  static status(direction, remotePath) {
    if (!__running) return null
    for (j in __jobs) {
      if (j.dir == direction && j.remote == remotePath) return j.status
    }
    return null
  }

  // ----- mutation API ------------------------------------------------

  // Append a new job.  `total` is the expected byte count for
  // display purposes only — workers re-stat at open time.
  // `localToken` is the picker consent token from `Host.pickFiles`
  // for upload jobs (re-opened via `Host.openLocalFile(token)` at
  // upload time); pass null for downloads.  Returns the Job (so
  // the caller can keep a reference) or null if the queue isn't
  // running.
  static enqueue(direction, localPath, remotePath, total, localToken) {
    if (!exactInteger_(total, 0)) Fiber.abort("SftpQueue: invalid exact byte count")
	if (!__running || __fatal) return null
	for (existing in __jobs) {
	  if (existing.dir == direction && existing.remote == remotePath &&
		  (existing.status == SftpQueue.QUEUED ||
		   existing.status == SftpQueue.ACTIVE)) return existing
	}
    if (__nextId >= Num.maxSafeInteger) Fiber.abort("SftpQueue: job IDs exhausted")
	var j = Job.new(__nextId, direction, localPath, remotePath, total,
		localToken)
	__nextId = __nextId + 1
    __jobs.add(j)
    bump_()
    signalActive_()
	if (!persistNow_()) return null
    return j
  }
  // 4-arg overload for download jobs (no token needed).
  static enqueue(direction, localPath, remotePath, total) {
    return enqueue(direction, localPath, remotePath, total, null)
  }

  // Flag a single job for cancellation.  QUEUED and ACTIVE jobs are
  // claimed by their worker so it can remove any partial destination
  // before publishing CANCELLED.  Terminal-state jobs are ignored.
	static cancel(id) {
    if (!__running) return false
    var hit = false
    for (j in __jobs) {
	  if (j.id == id) {
        if (j.status == SftpQueue.QUEUED) {
		  j.cancel = true
          hit = true
        } else if (j.status == SftpQueue.ACTIVE && !j.cancel) {
          j.cancel = true
          hit = true
        }
        break
      }
    }
    if (hit) {
      bump_()
      signalActive_()
      persistNow_()
    }
    return hit
  }

  // Flag every QUEUED / ACTIVE job for cancellation.  Used by the
  // degraded modal's "Hang up now" branch.
  static cancelAll() {
    if (!__running) return
    var changed = false
    for (j in __jobs) {
      if (j.status == SftpQueue.QUEUED) {
		j.cancel = true
        changed = true
      } else if (j.status == SftpQueue.ACTIVE && !j.cancel) {
        j.cancel = true
        changed = true
      }
    }
    if (changed) {
      bump_()
      signalActive_()
	  persistNow_()
    }
  }

  // ----- worker fibers -----------------------------------------------

  // Spawn one worker per direction.  Each fiber's first .call() runs
  // it up to its first yield (the idle-poll Timer.after), so by
  // the time spawnWorkers_ returns the workers are parked on the
  // result queue, ready for the dispatcher to resume them.
  static spawnWorkers_() {
    Fiber.new { workerLoop_(SftpQueue.UPLOAD)   }.call()
    Fiber.new { workerLoop_(SftpQueue.DOWNLOAD) }.call()
  }

  // Per-direction loop.  Claims one job at a time, runs it, then
  // looks for another.  Idle parks for IDLE_POLL_MS; an enqueue
  // doesn't currently wake the worker explicitly (the cadence is
  // tight enough that the latency is invisible).  Exits when the
  // queue is stopped or the SFTP session has gone away.
  static workerLoop_(direction) {
    while (true) {
      if (!__running) return
      if (__suspended) return
      if (!SFTP.available) {
        handleSftpDown_(direction)
        return
      }
      var job = claimNext_(direction)
      if (job == null) {
        Timer.after(SftpQueue.IDLE_POLL_MS).await()
      } else {
        runJob_(job)
      }
    }
  }

  // Walk the list, find the first QUEUED job for `direction`, and flip
  // it to ACTIVE.  The persisted contiguous frontier is the working
  // baseline; out-of-order completed intervals beyond it are replayed.
  // Throughput-window samples are reset and re-seeded by the dispatcher.
  static claimNext_(direction) {
    for (j in __jobs) {
	  if (j.dir == direction && j.status == SftpQueue.QUEUED) {
        j.status = SftpQueue.ACTIVE
		j.done   = j.resume
        j.resetSamples()
        bump_()
		if (!persistNow_()) return null
        return j
      }
    }
    return null
  }

  // SFTP went away mid-session. Preserve active frontiers and queued
  // work for the next connected VM rather than converting transport
  // loss into a per-file failure.
  static handleSftpDown_(direction) {
	// Loss of the provider is a session interruption, not a per-file
	// failure.  Preserve every active frontier for the next connection.
	suspend()
	persistNow_()
  }

  // Dispatch a claimed job to the per-direction transfer loop.
  // The status-bar arrow is on for the duration of the transfer
  // and off the rest of the time — true at start, false at end,
  // no counting.  One worker per direction means the per-direction
  // bool maps cleanly to "this worker is moving bytes right now."
  // Missing partials are already removed. Preserve every other native reason.
  static removeLocal_(name) {
    if (Download == null) return null
    var result = Download.delete(name)
    if (result is FileError && result.code == IOErr.notFound) return null
    return result
  }

  static runJob_(job) {
	if (job.cancel) {
	  if (job.dir == SftpQueue.DOWNLOAD) {
        var removed = removeLocal_(job.local)
		if (removed == null) {
		  job.status = SftpQueue.CANCELLED
		} else {
		  job.status = SftpQueue.FAILED
		  job.errMsg = "cancelled, but the local partial could not be removed: " + removed.toString
		}
	  } else {
		var removed = SFTP.remove(job.remote).await()
		if (removed is SFTPError && removed.isTransient) {
		  SftpQueue.suspend()
		  return
		} else if (removed is SFTPError && !(removed.code == 0 &&
			removed.serverStatus == 2)) {
		  job.status = SftpQueue.FAILED
		  job.errMsg = "cancelled, but the remote partial could not be removed: " + removed.toString
		} else {
		  job.status = SftpQueue.CANCELLED
		}
	  }
	  bump_()
	  signalActive_()
	  persistNow_()
	  return
	}
    if (job.dir == SftpQueue.UPLOAD) {
      Host.uploadArrow = true
      runUpload_(job)
      Host.uploadArrow = false
    } else {
      Host.downloadArrow = true
      runDownload_(job)
      Host.downloadArrow = false
    }
    bump_()
    signalActive_()
    persistNow_()
  }

  // Open the remote read-only, create a fresh local destination,
  // and dispatch PIPELINE_DEPTH chunk fibers that race through the
  // file in parallel.  job.local is the basename inside DownloadPath
  // — the relaxed-name predicate is what makes typical SFTP names
  // like "file with spaces.zip" allowable here.  Errors land the job
  // in FAILED with a descriptive errMsg; cancel between chunks
  // lands it in CANCELLED. Mid-transfer interruption preserves the
  // partial and the last contiguous completed frontier for the next VM.
  static runDownload_(job) {
    if (Download == null) {
      job.status = SftpQueue.FAILED
      job.errMsg = "DownloadPath is not configured, is set to $HOME, or " +
                   "doesn't exist on disk.  Set DownloadPath in your BBS list " +
                   "(and create the directory if necessary) before downloading."
      return
    }
	// Every download begins with a source identity check.  A changed source
	// invalidates the checkpoint and the partial destination.
	var source = SFTP.stat(job.remote).await()
	if (!(source is SFTPStat)) {
	  if (source is SFTPError && source.isTransient) {
		SftpQueue.suspend()
		return
	  }
	  job.status = SftpQueue.FAILED
	  job.errMsg = "stat remote: " + source.toString
	  return
	}
	if (job.resume > 0 && (job.sourceSize != source.size ||
		job.sourceMtime != source.mtime)) {
	  job.resume = 0
	  job.done = 0
      var removed = removeLocal_(job.local)
	  if (removed is FileError) {
		job.status = SftpQueue.FAILED
		job.errMsg = "cannot discard local partial after remote source changed: " + removed.toString
		return
	  }
	}
	job.sourceSize = source.size
	job.sourceMtime = source.mtime
	job.total = source.size
    var entries = Download.list
    if (entries is FileError) {
      job.status = SftpQueue.FAILED
      job.errMsg = entries.toString
      return
    }
    var lf = entries[job.local]
    if (lf != null) {
      var localSize = lf is File ? lf.size : null
      if (localSize is FileError) {
        job.status = SftpQueue.FAILED
        job.errMsg = localSize.toString
        return
      }
      if (!(lf is File) || localSize < job.resume || localSize > job.total) {
        var removed = removeLocal_(job.local)
        if (removed is FileError) {
          job.status = SftpQueue.FAILED
          job.errMsg = "cannot replace incompatible local partial: " + removed.toString
          return
        }
        lf = null
        job.resume = 0
        job.done = 0
      }
    }
    if (lf == null) lf = Download.create(job.local)
    if (lf is FileError) {
      job.status = SftpQueue.FAILED
      job.errMsg = lf.toString
      return
    }
	var localOpen = lf.open()
	if (localOpen is FileError) {
	  job.status = SftpQueue.FAILED
	  job.errMsg = localOpen.toString
	  return
	}
	if (job.resume == 0) {
	  var truncated = lf.write("")
	  if (truncated is FileError) {
		lf.close()
		job.status = SftpQueue.FAILED
		job.errMsg = truncated.toString
		return
	  }
	}
    var hSrc = SFTP.open(job.remote, FileFlag.read).await()
    if (hSrc is SFTPError) {
	  lf.close()
	  if (hSrc.isTransient) {
		SftpQueue.suspend()
		return
	  }
      job.status = SftpQueue.FAILED
      job.errMsg = "open remote: " + hSrc.toString
      return
    }
	var totalSize = source.size
    // Seed the throughput window now that done is at its working
    // baseline (zero — see claimNext_).
    job.recordSample()
    var depth = SftpQueue.adaptiveDepth
    var ctx   = JobCtx.new(job, totalSize, lf, hSrc, depth)
    for (i in 0...depth) {
      ctx.bumpAlive()
      Fiber.new { downloadChunk_(ctx) }.call()
    }
    ctx.await()
	var localClose = lf.close()
	var remoteClose = hSrc.close().await()
    if (ctx.err == "suspended") {
      // Worker is bailing per user request; leave the job ACTIVE so
      // it persists in that state and resumes on the next session.
      return
    }
    if (ctx.err == "cancelled") {
      var removed = removeLocal_(job.local)
	  if (removed == null) {
		job.status = SftpQueue.CANCELLED
	  } else {
		job.status = SftpQueue.FAILED
		job.errMsg = "cancelled, but the local partial could not be removed: " + removed.toString
	  }
    } else if (ctx.err != null) {
      job.status = SftpQueue.FAILED
      job.errMsg = ctx.err
	} else if (localClose is FileError) {
	  job.status = SftpQueue.FAILED
	  job.errMsg = localClose.toString
	} else if (remoteClose is SFTPError && remoteClose.isTransient) {
	  SftpQueue.suspend()
	  return
	} else if (remoteClose is SFTPError) {
	  job.status = SftpQueue.FAILED
	  job.errMsg = "close remote: " + remoteClose.toString
	} else {
	  var finalSource = SFTP.stat(job.remote).await()
	  if (finalSource is SFTPError && finalSource.isTransient) {
		SftpQueue.suspend()
		return
	  }
	  if (!(finalSource is SFTPStat) || finalSource.size != job.sourceSize ||
		  finalSource.mtime != job.sourceMtime || job.resume != job.total) {
		job.status = SftpQueue.FAILED
		job.errMsg = "remote source changed during download"
		return
	  }
	  job.done = job.total
	  job.status = SftpQueue.DONE
      // Stamp local mtime to match the remote.  Stat after the data
      // is on disk so the local mtime matches the server's "as of
      // completion" timestamp.  Errors are non-fatal — chip falls
      // back to "[<>]".
	  lf.mtime = job.sourceMtime
    }
  }

  // One chunk worker for a download job.  Loops claiming offsets
  // from the shared JobCtx, issues an SFTP read for the slot, and
  // pwrites the result into the local file.  N of these run in
  // parallel; out-of-order completion is fine because writeBytes is
  // offset-keyed.  job.done aggregates total bytes written across
  // workers and is used purely for the progress display.
  static downloadChunk_(ctx) {
    while (true) {
      var slot = ctx.claimChunk(SftpQueue.adaptiveChunkSize)
      if (slot == null) break
	  var off  = slot[0]
	  var want = slot[1]
	  var cursor = off
	  var remaining = want
	  while (remaining > 0) {
		var t0 = Timer.now
		var bytes = ctx.hRemote.readBytes(remaining,
			cursor).await()
		var rtt = Timer.now - t0
		if (bytes is SFTPError) {
		  if (bytes.isTransient || SftpQueue.shellClosed) {
			SftpQueue.suspend()
			ctx.setError("suspended")
		  } else {
			ctx.setError("read remote: " + bytes.toString)
		  }
		  break
		}
		if (bytes == null) {
		  ctx.setError("remote ended before the advertised size")
		  break
		}
		var got = bytes.bytes.count
		if (got > remaining) {
		  ctx.setError("remote returned more bytes than requested")
		  break
		}
		var written = ctx.lf.writeBytes(bytes, cursor)
		if (written is FileError) {
		  ctx.setError(written.toString)
		  break
		}
		cursor = cursor + got
		remaining = remaining - got
		var job = ctx.job
		job.done = job.done + got
		job.recordSample()
      // Feed adaptive sizing.  RTT sample is the issue-to-response
      // wall time; BW sample assumes the pipeline is saturated at
      // ctx.depth concurrent requests, so the link's per-RTT capacity
      // is depth * got / rtt.  Both estimators clamp non-positive
      // values internally.
		SftpQueue.recordRtt_(rtt)
		if (rtt > 0) SftpQueue.recordBps_((got * ctx.depth) / rtt)
		bumpProgress_()
	  }
	  if (remaining != 0) break
	  ctx.complete(off, want)
    }
    ctx.workerExit()
  }

  // Mirror — re-open the picker-consented source via the token,
  // open the remote with WRITE|CREAT|TRUNC, dispatch PIPELINE_DEPTH
  // chunk fibers that race through the file in parallel.
  //
  // The token is verified by Host.openLocalFile: HMAC must match
  // the installed signing key, AND the file's current SHA-256 must
  // match the SHA-256 captured at consent time.  If either fails
  // (key rotated, file edited, file deleted), the upload is
  // rejected — the user must re-pick to re-consent to the new
  // content.  Uploads without a token fail immediately.
  static runUpload_(job) {
    if (job.localToken == null) {
      job.status = SftpQueue.FAILED
      job.errMsg = "upload missing consent token"
      return
    }
    var lf = Host.openLocalFile(job.localToken)
    if (lf == null) {
      job.status = SftpQueue.FAILED
      job.errMsg = "consent token rejected (file changed, deleted, or key rotated)"
      return
    }
	var opened = lf.open()
	if (opened is FileError) {
	  job.status = SftpQueue.FAILED
	  job.errMsg = opened.toString
	  return
	}
    var totalSize = lf.size
	var localMtime = lf.mtime
    if (totalSize is FileError || localMtime is FileError) {
      lf.close()
      job.status = SftpQueue.FAILED
      job.errMsg = (totalSize is FileError ? totalSize : localMtime).toString
      return
    }
    job.total = totalSize
	if (job.resume > 0 && (job.sourceSize != totalSize ||
		job.sourceMtime != localMtime)) {
	  lf.close()
	  job.status = SftpQueue.FAILED
	  job.errMsg = "local upload source changed; pick it again"
	  return
	}
	job.sourceSize = totalSize
	job.sourceMtime = localMtime
	var remote = SFTP.stat(job.remote).await()
	var restart = job.resume == 0
	if (remote is SFTPStat) {
	  if (remote.size < job.resume || remote.size > totalSize) restart = true
	} else if (remote is SFTPError) {
	  // SSH_FX_NO_SUCH_FILE is the only stat failure that authorizes create.
	  if (remote.code == 0 && remote.serverStatus == 2) {
		restart = true
	  } else if (remote.isTransient) {
		lf.close()
		SftpQueue.suspend()
		return
	  } else {
		lf.close()
		job.status = SftpQueue.FAILED
		job.errMsg = "stat remote: " + remote.toString
		return
	  }
	} else {
	  lf.close()
	  job.status = SftpQueue.FAILED
	  job.errMsg = "stat remote returned an invalid result"
	  return
	}
	if (restart) {
	  job.resume = 0
	  job.done = 0
	}
	var flags = FileFlag.write | FileFlag.creat
	if (restart) flags = flags | FileFlag.trunc
    var hDst  = SFTP.open(job.remote, flags).await()
    if (hDst is SFTPError) {
      lf.close()
	  if (hDst.isTransient) {
		SftpQueue.suspend()
		return
	  }
      job.status = SftpQueue.FAILED
      job.errMsg = "open remote: " + hDst.toString
      return
    }
    job.recordSample()
    var depth = SftpQueue.adaptiveDepth
    var ctx   = JobCtx.new(job, totalSize, lf, hDst, depth)
    for (i in 0...depth) {
      ctx.bumpAlive()
      Fiber.new { uploadChunk_(ctx) }.call()
    }
    ctx.await()
	var localClose = lf.close()
	var remoteClose = hDst.close().await()
    if (ctx.err == "suspended") {
      // Worker is bailing per user request; leave the job ACTIVE so
      // it persists in that state and resumes on the next session.
      return
    }
    if (ctx.err == "cancelled") {
	  var removed = SFTP.remove(job.remote).await()
	  if (removed is SFTPError && removed.isTransient) {
		SftpQueue.suspend()
		return
	  } else if (removed is SFTPError) {
		job.status = SftpQueue.FAILED
		job.errMsg = "cancelled, but the remote partial could not be removed: " + removed.toString
	  } else {
		job.status = SftpQueue.CANCELLED
	  }
    } else if (ctx.err != null) {
      job.status = SftpQueue.FAILED
      job.errMsg = ctx.err
	} else if (localClose is FileError) {
	  job.status = SftpQueue.FAILED
	  job.errMsg = localClose.toString
	} else if (remoteClose is SFTPError && remoteClose.isTransient) {
	  SftpQueue.suspend()
	  return
	} else if (remoteClose is SFTPError) {
	  job.status = SftpQueue.FAILED
	  job.errMsg = "close remote: " + remoteClose.toString
	} else if (Host.openLocalFile(job.localToken) == null ||
		job.resume != job.total) {
	  job.status = SftpQueue.FAILED
	  job.errMsg = "local upload source changed during transfer"
    } else {
	  job.done = job.total
      job.status = SftpQueue.DONE
      // Stamp the remote with the local mtime so a subsequent browse
      // shows [==] without depending on hash extensions.  Symmetric
      // with the download path's local-mtime stamp.  Errors are
      // non-fatal — if the server rejects setstat (e.g. read-only
      // mount), the chip falls back to "[<>]".
      // Local POSIX times may be signed; SFTP v3 only carries uint32 seconds.
      if (localMtime >= 0 && localMtime <= 4294967295) {
        SFTP.setMtime(job.remote, localMtime).await()
      }
    }
  }

  // One chunk worker for an upload job.  Symmetric with
  // downloadChunk_: claim an offset, pread the local slice, push it
  // out via RemoteFile.writeBytes at that offset.  N parallel; out-of-order
  // remote writes are fine for SFTP write-by-offset.
  static uploadChunk_(ctx) {
    while (true) {
      var slot = ctx.claimChunk(SftpQueue.adaptiveChunkSize)
      if (slot == null) break
	  var off  = slot[0]
	  var want = slot[1]
	  var cursor = off
	  var remaining = want
	  while (remaining > 0) {
		var chunk = ctx.lf.readBytes(remaining, cursor)
		if (chunk is FileError || chunk == null) {
		  ctx.setError(chunk is FileError ? chunk.toString :
			  "local source ended before its advertised size")
		  break
		}
		var got = chunk.bytes.count
		if (got > remaining) {
		  ctx.setError("local read returned more bytes than requested")
		  break
		}
		var t0 = Timer.now
		var r = ctx.hRemote.writeBytes(chunk, cursor).await()
		var rtt = Timer.now - t0
		if (r is SFTPError) {
		  if (r.isTransient || SftpQueue.shellClosed) {
			SftpQueue.suspend()
			ctx.setError("suspended")
		  } else {
			ctx.setError("write remote: " + r.toString)
		  }
		  break
		}
		cursor = cursor + got
		remaining = remaining - got
		var job = ctx.job
		job.done = job.done + got
		job.recordSample()
      // Feed adaptive sizing — see downloadChunk_ for the rationale.
		SftpQueue.recordRtt_(rtt)
		if (rtt > 0) SftpQueue.recordBps_((got * ctx.depth) / rtt)
		bumpProgress_()
	  }
	  if (remaining != 0) break
	  ctx.complete(off, want)
    }
    ctx.workerExit()
  }

  // ----- private helpers ---------------------------------------------

  static bump_() {
    __gen = __gen + 1
  }
	static bumpProgress_() { __progressGen = __progressGen + 1 }
	static persistProgress_() {
	  bumpProgress_()
	  return persistNow_()
	}

	static persistenceFailed_(reason) {
	  __fatal = true
	  __suspended = true
	  for (j in __jobs) {
		if (j.status == SftpQueue.ACTIVE || j.status == SftpQueue.QUEUED) {
		  j.status = SftpQueue.FAILED
		  j.errMsg = "queue persistence failed: %(reason)"
		}
	  }
	  bump_()
	  signalActive_()
	  return false
	}

  // Serialize the current list to Cache/sftp_queue.won.  Called on
  // every state transition + enqueue + cancel; matches the C side's
  // "flush on every change" cadence.  At queue scale (<<100 jobs)
  // the cost is negligible.
  static persistNow_() {
	if (!__running || __fatal) return !__fatal
    var jobs = []
    for (j in __jobs) {
	  if (j.status != SftpQueue.QUEUED && j.status != SftpQueue.ACTIVE) continue
      var m = {
		"id":     j.id,
        "dir":    j.dir,
        "local":  j.local,
        "remote": j.remote,
        "total":  j.total,
		"resume": j.resume,
		"sourceSize": j.sourceSize,
		"sourceMtime": j.sourceMtime,
		"cancel": j.cancel
      }
      // Picker consent token — present only for arbitrary-path
      // uploads.  Worker re-resolves via Host.openLocalFile on
      // resume; verify failure (key rotated, file changed) lands
      // the resumed job in FAILED.
      if (j.localToken != null) m["localToken"] = j.localToken
      jobs.add(m)
    }
	var doc = { "version": 2, "nextId": __nextId, "jobs": jobs }
    var text = WON.serialize(doc, "  ")
	var replaced = Cache.replace(SftpQueue.FILENAME, text)
	if (replaced is FileError) return persistenceFailed_(replaced.toString)
	return true
  }

  static exactInteger_(value, minimum) {
    return value is Num && value >= minimum && value <= Num.maxSafeInteger && value == value.floor
  }

  // Load persisted jobs. ACTIVE work returns to QUEUED at its contiguous
  // resume frontier. Version-1 imports restart at byte zero. Terminal jobs
  // are not persisted.
  static load_() {
	if (Cache == null) return false
    var entries = Cache.list
    if (entries is FileError) return false
    var f = entries[SftpQueue.FILENAME]
    if (f == null) return true
    if (!(f is File)) return false
    var opened = f.open()
    if (opened is FileError) return false
    var text = f.read()
    var closed = f.close()
    if (!(text is String) || closed is FileError || text.bytes.count == 0) return false
    // WON.deserialize aborts on bad input; guard so a corrupt
    // persisted file doesn't brick the queue.  Check fiber.error
    // rather than try()'s return — on success try() returns the
    // fiber's final value (which is the deserialized doc and could
    // be any non-null type); only fiber.error is null vs non-null
    // on success vs abort.
    var doc = null
    var fb = Fiber.new { doc = WON.deserialize(text) }
    fb.try()
	if (fb.error != null || doc is WONError || !(doc is Map)) return false
    var ver = doc["version"]
	if (ver != 1 && ver != 2) return false
    var loaded = doc["jobs"]
	if (!(loaded is List)) return false
	if (ver == 2) {
	  var next = doc["nextId"]
	  if (!exactInteger_(next, 1)) return false
	  __nextId = next
	}
    for (m in loaded) {
	  if (!(m is Map)) return false
	  if (ver == 1) {
		var oldStatus = m["status"]
		if (oldStatus == SftpQueue.DONE || oldStatus == SftpQueue.FAILED ||
			oldStatus == SftpQueue.CANCELLED) continue
		if (oldStatus != SftpQueue.QUEUED && oldStatus != SftpQueue.ACTIVE) {
		  return false
		}
	  }
      var dir    = m["dir"]
      var local  = m["local"]
      var remote = m["remote"]
	  if (!(dir is String) || !(local is String) || !(remote is String)) return false
	  if (dir != SftpQueue.UPLOAD && dir != SftpQueue.DOWNLOAD) return false
      var total = m["total"]
	  if (!exactInteger_(total, 0)) return false
      var token = m["localToken"]
      if (!(token is String) || token.count == 0) token = null
	  if (dir == SftpQueue.UPLOAD && token == null) return false
	  var id = ver == 2 ? m["id"] : __nextId
	  if (!exactInteger_(id, 1) || id >= Num.maxSafeInteger) return false
	  for (existing in __jobs) {
		if (existing.id == id || (existing.dir == dir &&
			existing.remote == remote)) return false
	  }
	  var j = Job.new(id, dir, local, remote, total, token)
	  if (ver == 2) {
		var resume = m["resume"]
		var sourceSize = m["sourceSize"]
		var sourceMtime = m["sourceMtime"]
		var cancel = m["cancel"]
		if (!exactInteger_(resume, 0) || resume > total ||
			!exactInteger_(sourceSize, 0) ||
			!exactInteger_(sourceMtime, Num.minSafeInteger) || !(cancel is Bool)) return false
		j.resume = resume
		j.done = resume
		j.sourceSize = sourceSize
		j.sourceMtime = sourceMtime
		j.cancel = cancel
	  }
	  if (id >= __nextId) __nextId = id + 1
	  j.status = SftpQueue.QUEUED
      __jobs.add(j)
    }
	return true
  }
}
