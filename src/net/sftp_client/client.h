/* Connection-owned asynchronous SFTP client. No callbacks enter Wren. */
#ifndef ST_SFTP_CLIENT_H_
#define ST_SFTP_CLIENT_H_
#include "st_sftp.h"
#include "syncterm_types.h"

struct st_sftp_client;
struct st_sftp_io {
	void *context;
	int (*open)(void *context);
	ssize_t (*read)(void *context, void *data, size_t size, int timeout_ms);
	/* Nonblocking short writes; WOULD_BLOCK must not prevent receive work. */
	ssize_t (*write)(void *context, const void *data, size_t size);
	void (*close)(void *context);
	void (*log)(void *context, const char *message);
};
/* Creation does no I/O and starts no thread. The caller keeps io.context
 * alive through destroy. Before destroy, interrupt the containing transport
 * so an in-progress channel open/send cannot block the worker join. */
int st_sftp_client_create(struct st_sftp_client **result,
	const struct st_sftp_io *io);
const struct st_sftp_provider *st_sftp_client_provider(struct st_sftp_client *client);
void st_sftp_client_destroy(struct st_sftp_client *client);
#endif
