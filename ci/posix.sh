#!/bin/sh
# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
set -eu

ci_root=$(CDPATH= cd -P "$(dirname "$0")/.." && pwd)
CI_BUILD_DIR=${CI_BUILD_DIR:-"$ci_root/build-ci"}
export CI_BUILD_DIR
mkdir -p "$CI_BUILD_DIR/reports"
ci_xvfb_pid=
ci_weston_pid=
ci_runtime=
cleanup() {
	# Only these child processes belong to this job. Never use a shared desktop
	# display or stop another pipeline's X server/compositor.
	if [ -n "$ci_weston_pid" ]; then
		kill "$ci_weston_pid" 2>/dev/null || :
		wait "$ci_weston_pid" 2>/dev/null || :
	fi
	if [ -n "$ci_xvfb_pid" ]; then
		kill "$ci_xvfb_pid" 2>/dev/null || :
		wait "$ci_xvfb_pid" 2>/dev/null || :
	fi
	# A private, empty runtime directory can be removed without recursion.
	if [ -n "$ci_runtime" ]; then rmdir "$ci_runtime" 2>/dev/null || :; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
unset DISPLAY WAYLAND_DISPLAY XAUTHORITY XDG_RUNTIME_DIR

case ${CI_PROFILE:-native} in
mingw32-cross|mingw64-cross|minimal) ;;
*)
	if command -v Xvfb >/dev/null 2>&1; then
		# -displayfd atomically chooses a free display; a fixed :N races with
		# other jobs on shell runners. No inherited Xauthority is used.
		# Clear an earlier run's readiness before the background child starts.
		: >"$CI_BUILD_DIR/reports/xvfb-display"
		Xvfb -displayfd 3 -screen 0 1280x1024x24 -nolisten tcp \
			3>"$CI_BUILD_DIR/reports/xvfb-display" \
			>"$CI_BUILD_DIR/reports/xvfb.log" 2>&1 &
		ci_xvfb_pid=$!
		printf '%s\n' "$ci_xvfb_pid" >"$CI_BUILD_DIR/reports/xvfb.pid"
		ci_wait=0
		while [ ! -s "$CI_BUILD_DIR/reports/xvfb-display" ]; do
			if ! kill -0 "$ci_xvfb_pid" 2>/dev/null || [ "$ci_wait" -ge 30 ]; then
				cat "$CI_BUILD_DIR/reports/xvfb.log"
				echo 'Job-owned Xvfb failed to start' >&2
				exit 1
			fi
			sleep 1
			ci_wait=$((ci_wait + 1))
		done
		DISPLAY=":$(cat "$CI_BUILD_DIR/reports/xvfb-display")"
		export DISPLAY
	fi
	if command -v weston >/dev/null 2>&1; then
		ci_runtime=$(mktemp -d "${TMPDIR:-/tmp}/syncterm-ci.XXXXXXXX")
		XDG_RUNTIME_DIR=$ci_runtime
		WAYLAND_DISPLAY=syncterm-ci
		export XDG_RUNTIME_DIR WAYLAND_DISPLAY
		weston --backend=headless-backend.so --socket="$WAYLAND_DISPLAY" \
			--idle-time=0 >"$CI_BUILD_DIR/reports/weston.log" 2>&1 &
		ci_weston_pid=$!
		printf '%s\n' "$ci_weston_pid" >"$CI_BUILD_DIR/reports/weston.pid"
		ci_wait=0
		while [ ! -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]; do
			if ! kill -0 "$ci_weston_pid" 2>/dev/null || [ "$ci_wait" -ge 30 ]; then
				cat "$CI_BUILD_DIR/reports/weston.log"
				echo 'Job-owned headless Weston failed to start' >&2
				exit 1
			fi
			sleep 1
			ci_wait=$((ci_wait + 1))
		done
	fi
	;;
esac

if command -v ccache >/dev/null 2>&1; then
	# Launchers preserve CC/CXX identity for capability checks and C++ linking.
	CMAKE_C_COMPILER_LAUNCHER=$(command -v ccache)
	CMAKE_CXX_COMPILER_LAUNCHER=$CMAKE_C_COMPILER_LAUNCHER
	CCACHE_DIR="$ci_root/.ci-cache/ccache"
	export CMAKE_C_COMPILER_LAUNCHER CMAKE_CXX_COMPILER_LAUNCHER CCACHE_DIR
fi
cmake -P "$ci_root/ci/run.cmake"
