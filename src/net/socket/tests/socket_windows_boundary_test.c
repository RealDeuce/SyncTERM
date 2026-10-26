/* Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause */

#include "socket.h"
#include "st_threads.h"

#include <windows.h>
#include <ws2tcpip.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Native Windows OOB regression tests, not POSIX mark emulation.
 * In particular, every stream receive asks for MORE than the known prefix:
 * Windows need not preserve a boundary between normal bytes on either side
 * of the OOB byte. Verify both channels without requiring that position.
 * The duplicated control values must remain ordinary data, not be filtered.
 * Results (including successful tests) are retained in CI's JUnit system-out.
 * https://learn.microsoft.com/windows/win32/winsock/protocol-independent-out-of-band-data-2
 */
static const char prefix[] = {'A', 0x02, 'B'};
static const char suffix[] = {0x02, 'C', 0x02, 'D'};
static const char control = 0x02;

#define REQUIRE(e) do { if (!(e)) { \
	fprintf(stderr, "boundary check failed at %s:%d: %s (WSA=%d)\n", \
	    __FILE__, __LINE__, #e, WSAGetLastError()); \
	goto done; \
} } while (0)

static void
snapshot(SOCKET reader, const char *stage)
{
	u_long available = 0, mark = 0;
	WSAPOLLFD item = {.fd = reader, .events = POLLRDNORM | POLLRDBAND};
	int available_status = ioctlsocket(reader, FIONREAD, &available);
	int available_error = available_status == 0 ? 0 : WSAGetLastError();
	int mark_status = ioctlsocket(reader, SIOCATMARK, &mark);
	int mark_error = mark_status == 0 ? 0 : WSAGetLastError();
	int poll_status = WSAPoll(&item, 1, 0);
	int poll_error = poll_status == SOCKET_ERROR ? WSAGetLastError() : 0;

	fprintf(stderr, "%s available=%lu status=%d error=%d pending-query=%lu"
	    " status=%d error=%d poll=%d events=0x%x error=%d\n", stage,
	    available, available_status, available_error, mark, mark_status,
	    mark_error, poll_status, (unsigned)item.revents, poll_error);
}

static int
wait_queued(SOCKET reader, u_long minimum, bool urgent)
{
	ULONGLONG start = GetTickCount64();
	const struct timespec pause = {0, 1000000L};

	do {
		u_long available = 0;
		WSAPOLLFD item = {.fd = reader, .events = POLLRDBAND};

		if (ioctlsocket(reader, FIONREAD, &available) != 0
		    || WSAPoll(&item, 1, 0) == SOCKET_ERROR)
			return -1;
		if (available >= minimum
		    && (!urgent || (item.revents & POLLRDBAND) != 0))
			return 0;
		(void)st_thread_sleep(&pause, NULL);
	} while (GetTickCount64() - start < 3000);
	snapshot(reader, "queued-data deadline");
	return -1;
}

static int
urgent_receive(SOCKET reader, bool peek, const char *stage)
{
	char byte = 0;
	int count = recv(reader, &byte, 1, MSG_OOB | (peek ? MSG_PEEK : 0));
	int error = count == SOCKET_ERROR ? WSAGetLastError() : 0;

	fprintf(stderr, "%s urgent peek=%d count=%d error=%d byte=%02x\n",
	    stage, peek, count, error, (unsigned char)byte);
	return count == 1 && byte == control ? 0 : -1;
}

static void
print_receive(const char *stage, DWORD count, DWORD flags, const char *bytes)
{
	fprintf(stderr, "%s count=%lu flags=0x%lx bytes=", stage, count, flags);
	for (DWORD i = 0; i < count; ++i)
		fprintf(stderr, "%02x ", (unsigned char)bytes[i]);
	fputc('\n', stderr);
}

static int
receive_stream(SOCKET reader, bool *consumed)
{
	const char expected[] = {'A', 0x02, 'B', 0x02, 'C', 0x02, 'D'};
	size_t total = 0;
	bool crossed = false;

	while (total < sizeof(expected)) {
		char bytes[32];
		WSABUF buffer = {.len = sizeof(bytes), .buf = bytes};
		DWORD count = 0, flags = 0;
		int status, error;

		snapshot(reader, "before oversized receive");
		status = WSARecv(reader, &buffer, 1, &count, &flags, NULL, NULL);
		error = status == 0 ? 0 : WSAGetLastError();
		fprintf(stderr, "oversized receive request=%lu status=%d error=%d\n",
		    buffer.len, status, error);
		if (status != 0 && error == WSAEWOULDBLOCK
		    && !*consumed) {
			/* A provider may hold ordinary reads at an unread OOB block.
			 * Release it without assuming that its position is observable,
			 * then verify all remaining normal data is unmodified. */
			fprintf(stderr, "ordinary read blocked with queued suffix at offset=%zu\n",
			    total);
			if (urgent_receive(reader, true, "ordinary read blocked") != 0
			    || urgent_receive(reader, false, "release ordinary read barrier") != 0)
				return -1;
			*consumed = true;
			continue;
		}
		if (status != 0)
			return -1;
		print_receive("oversized receive", count, flags, bytes);
		snapshot(reader, "after oversized receive");
		if (count == 0 || count > sizeof(expected) - total
		    || memcmp(bytes, expected + total, count) != 0)
			return -1;
		if (total < sizeof(prefix) && count > sizeof(prefix) - total)
			crossed = true;
		total += count;
	}
	fprintf(stderr, "normal bytes=%zu receive-spanned-mark=%d OOB-consumed=%d\n",
	    total, crossed, *consumed);
	return 0;
}

static int
pending_case(SOCKET reader, SOCKET writer, const char *mode)
{
	bool peek = strcmp(mode, "peek") == 0;
	bool consumed = strcmp(mode, "taken") == 0;
	int receive_status;
	int result = 1;

	REQUIRE(send(writer, prefix, (int)sizeof(prefix), 0) == (int)sizeof(prefix));
	REQUIRE(send(writer, &control, 1, MSG_OOB) == 1);
	REQUIRE(send(writer, suffix, (int)sizeof(suffix), 0) == (int)sizeof(suffix));
	/* Ensure a short receive cannot be explained by the suffix not having
	 * arrived yet. No helper here performs an implicit normal-data peek. */
	REQUIRE(wait_queued(reader, sizeof(prefix) + sizeof(suffix), true) == 0);
	snapshot(reader, "all data queued");
	if (peek || consumed) {
		REQUIRE(urgent_receive(reader, peek, "before normal reads") == 0);
		snapshot(reader, "after initial urgent operation");
	}
	receive_status = receive_stream(reader, &consumed);
	if (!consumed) {
		REQUIRE(urgent_receive(reader, true, "after all normal bytes") == 0);
		snapshot(reader, "after final urgent peek");
		REQUIRE(urgent_receive(reader, false, "after all normal bytes") == 0);
	}
	REQUIRE(receive_status == 0);
	result = 0;
done:
	return result;
}

static int
overlapped_case(SOCKET reader, SOCKET writer)
{
	WSAOVERLAPPED operation = {0};
	char bytes[32];
	WSABUF buffer = {.len = sizeof(bytes), .buf = bytes};
	DWORD count = 0, flags = 0;
	int enabled = 1;
	int status, error;
	bool submitted = false;
	int result = 1;

	REQUIRE(setsockopt(reader, SOL_SOCKET, SO_OOBINLINE,
	    (const char *)&enabled, sizeof(enabled)) == 0);
	REQUIRE(send(writer, prefix, (int)sizeof(prefix), 0) == (int)sizeof(prefix));
	REQUIRE(wait_queued(reader, sizeof(prefix), false) == 0);
	REQUIRE(WSARecv(reader, &buffer, 1, &count, &flags, NULL, NULL) == 0);
	REQUIRE(count == sizeof(prefix) && memcmp(bytes, prefix, count) == 0);
	operation.hEvent = WSACreateEvent();
	REQUIRE(operation.hEvent != WSA_INVALID_EVENT);
	/* Post BEFORE sending urgent data, to exercise pending completion rather
	 * than just repeating the synchronous, already-buffered WSARecv probe. */
	status = WSARecv(reader, &buffer, 1, NULL, &flags, &operation, NULL);
	error = status == 0 ? 0 : WSAGetLastError();
	fprintf(stderr, "overlapped submit status=%d error=%d\n", status, error);
	REQUIRE(status == 0 || error == WSA_IO_PENDING);
	submitted = true;
	REQUIRE(send(writer, &control, 1, MSG_OOB) == 1);
	REQUIRE(send(writer, suffix, (int)sizeof(suffix), 0) == (int)sizeof(suffix));
	REQUIRE(WSAWaitForMultipleEvents(1, &operation.hEvent, TRUE, 3000,
	    FALSE) == WSA_WAIT_EVENT_0);
	REQUIRE(WSAGetOverlappedResult(reader, &operation, &count, FALSE,
	    &flags));
	submitted = false;
	/* Internal is trace evidence only; production must not depend on this
	 * reserved field. The supported completion flags are recorded separately. */
	fprintf(stderr, "overlapped internal-status=0x%llx\n",
	    (unsigned long long)operation.Internal);
	print_receive("overlapped completion", count, flags, bytes);
	snapshot(reader, "after overlapped completion");
	REQUIRE(count == 1 && bytes[0] == control);
	REQUIRE(wait_queued(reader, sizeof(suffix), false) == 0);
	flags = 0;
	REQUIRE(WSARecv(reader, &buffer, 1, &count, &flags, NULL, NULL) == 0);
	print_receive("inline suffix", count, flags, bytes);
	REQUIRE(count == sizeof(suffix) && memcmp(bytes, suffix, count) == 0);
	result = 0;
done:
	if (submitted) {
		/* Reap cancellation before releasing either the stack operation or
		 * its buffer. CTest's process timeout bounds a broken native driver. */
		(void)CancelIoEx((HANDLE)reader, &operation);
		(void)WSAGetOverlappedResult(reader, &operation, &count, TRUE, &flags);
	}
	if (operation.hEvent != WSA_INVALID_EVENT)
		(void)WSACloseEvent(operation.hEvent);
	return result;
}

int
main(int argc, char **argv)
{
	SOCKET listener = INVALID_SOCKET, reader = INVALID_SOCKET;
	SOCKET writer = INVALID_SOCKET;
	struct sockaddr_in address = {.sin_family = AF_INET};
	int address_size = sizeof(address);
	int result = 1;
	bool initialized = false;

	REQUIRE(argc == 2);
	REQUIRE(strcmp(argv[1], "pending") == 0 || strcmp(argv[1], "peek") == 0
	    || strcmp(argv[1], "taken") == 0 || strcmp(argv[1], "overlapped") == 0);
	fprintf(stderr, "native Windows urgent boundary mode=%s\n", argv[1]);
	REQUIRE(socket_runtime_init() == 0);
	initialized = true;
	listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	REQUIRE(listener != INVALID_SOCKET);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	REQUIRE(bind(listener, (const struct sockaddr *)&address, sizeof(address)) == 0);
	REQUIRE(listen(listener, 1) == 0);
	REQUIRE(getsockname(listener, (struct sockaddr *)&address, &address_size) == 0);
	reader = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
	    WSA_FLAG_OVERLAPPED);
	REQUIRE(reader != INVALID_SOCKET);
	REQUIRE(connect(reader, (const struct sockaddr *)&address, sizeof(address)) == 0);
	writer = accept(listener, NULL, NULL);
	REQUIRE(writer != INVALID_SOCKET);
	REQUIRE(socket_set_no_delay(writer, true) == 0);
	REQUIRE(socket_set_blocking(reader, false) == 0);
	result = strcmp(argv[1], "overlapped") == 0
	    ? overlapped_case(reader, writer) : pending_case(reader, writer, argv[1]);
done:
	if (writer != INVALID_SOCKET)
		(void)closesocket(writer);
	if (reader != INVALID_SOCKET)
		(void)closesocket(reader);
	if (listener != INVALID_SOCKET)
		(void)closesocket(listener);
	if (initialized)
		(void)socket_runtime_cleanup();
	return result;
}
