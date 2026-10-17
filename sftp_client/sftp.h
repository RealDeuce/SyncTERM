#ifndef SFTP_SFTP_H
#define SFTP_SFTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SFTP wire permission type bits, independent of host filesystem headers. */
#define ST_SFTP_MODE_TYPE_MASK 0170000
#define ST_SFTP_MODE_DIRECTORY 0040000

// draft-ietf-secsh-filexfer-02

#define SSH_FXP_INIT             UINT8_C(1)
#define SSH_FXP_VERSION          UINT8_C(2)
#define SSH_FXP_OPEN             UINT8_C(3)
#define SSH_FXP_CLOSE            UINT8_C(4)
#define SSH_FXP_READ             UINT8_C(5)
#define SSH_FXP_WRITE            UINT8_C(6)
#define SSH_FXP_LSTAT            UINT8_C(7)
#define SSH_FXP_FSTAT            UINT8_C(8)
#define SSH_FXP_SETSTAT          UINT8_C(9)
#define SSH_FXP_FSETSTAT         UINT8_C(10)
#define SSH_FXP_OPENDIR          UINT8_C(11)
#define SSH_FXP_READDIR          UINT8_C(12)
#define SSH_FXP_REMOVE           UINT8_C(13)
#define SSH_FXP_MKDIR            UINT8_C(14)
#define SSH_FXP_RMDIR            UINT8_C(15)
#define SSH_FXP_REALPATH         UINT8_C(16)
#define SSH_FXP_STAT             UINT8_C(17)
#define SSH_FXP_RENAME           UINT8_C(18)
#define SSH_FXP_READLINK         UINT8_C(19)
#define SSH_FXP_SYMLINK          UINT8_C(20)
#define SSH_FXP_STATUS           UINT8_C(101)
#define SSH_FXP_HANDLE           UINT8_C(102)
#define SSH_FXP_DATA             UINT8_C(103)
#define SSH_FXP_NAME             UINT8_C(104)
#define SSH_FXP_ATTRS            UINT8_C(105)
#define SSH_FXP_EXTENDED         UINT8_C(200)
#define SSH_FXP_EXTENDED_REPLY   UINT8_C(201)

#define SSH_FX_OK                UINT32_C(0)
#define SSH_FX_EOF               UINT32_C(1)
#define SSH_FX_NO_SUCH_FILE      UINT32_C(2)
#define SSH_FX_PERMISSION_DENIED UINT32_C(3)
#define SSH_FX_FAILURE           UINT32_C(4)
#define SSH_FX_BAD_MESSAGE       UINT32_C(5)
#define SSH_FX_NO_CONNECTION     UINT32_C(6)
#define SSH_FX_CONNECTION_LOST   UINT32_C(7)
#define SSH_FX_OP_UNSUPPORTED    UINT32_C(8)

#define SSH_FXF_READ             UINT32_C(0x00000001)
#define SSH_FXF_WRITE            UINT32_C(0x00000002)
#define SSH_FXF_APPEND           UINT32_C(0x00000004)
#define SSH_FXF_CREAT            UINT32_C(0x00000008)
#define SSH_FXF_TRUNC            UINT32_C(0x00000010)
#define SSH_FXF_EXCL             UINT32_C(0x00000020)

#define SSH_FILEXFER_ATTR_SIZE        UINT32_C(0x00000001)
#define SSH_FILEXFER_ATTR_UIDGID      UINT32_C(0x00000002)
#define SSH_FILEXFER_ATTR_PERMISSIONS UINT32_C(0x00000004)
#define SSH_FILEXFER_ATTR_ACMODTIME   UINT32_C(0x00000008)
#define SSH_FILEXFER_ATTR_EXTENDED    UINT32_C(0x80000000)

#define SFTP_MIN_PACKET_ALLOC 4096
#define SFTP_VERSION UINT32_C(3)
#define SFTP_MAX_PACKET_SIZE (256*1024)

/*
 * SFTP extensions negotiated during INIT/VERSION handshake.
 *
 * Both sides advertise the names they support; the intersection is
 * exposed to callers as the `extensions` flag word (sftpc_get_extensions).
 * Presence of the name in both directions
 * enables the extension for the session.
 */
#define SFTP_EXT_NAME_LNAME  "lname@syncterm.net"
#define SFTP_EXT_NAME_DESCS  "descs@syncterm.net"
#define SFTP_EXT_NAME_SHA1S  "sha1s@syncterm.net"
#define SFTP_EXT_NAME_MD5S   "md5s@syncterm.net"
#define SFTP_EXT_NAME_PUBDIR "pubdir@syncterm.net"

#define SFTP_EXT_LNAME      UINT32_C(0x00000001)
#define SFTP_EXT_DESCS      UINT32_C(0x00000002)
#define SFTP_EXT_SHA1S      UINT32_C(0x00000004)
#define SFTP_EXT_MD5S       UINT32_C(0x00000008)
#define SFTP_EXT_PUBDIR     UINT32_C(0x00000010)

/* Client advertisement set. */
#define SFTP_EXT_ALL        (SFTP_EXT_LNAME | SFTP_EXT_DESCS | \
                             SFTP_EXT_SHA1S | SFTP_EXT_MD5S  | \
                             SFTP_EXT_PUBDIR)

/* Packet buffers are private to the protocol translation unit. */
typedef struct sftp_tx_pkt *sftp_tx_pkt_t;
typedef struct sftp_rx_pkt *sftp_rx_pkt_t;

/* sftp_string is the one struct whose fields ARE public: consumers
 * (both syncterm/ssh.c and sbbs3/sftp.cpp) read ->c_str and ->len to
 * get at the wire bytes the library handed them.
 *
 * Three lifetime modes — selected by the helper used to construct the
 * string and conveyed by the release callback:
 *   - heap: library single-malloc of header + bytes; ->release frees
 *     the whole thing in free_sftp_str().
 *   - static: caller provides struct + bytes, both with program/scope
 *     lifetime; ->release is NULL and free_sftp_str() is a no-op.
 *   - borrowed: caller provides the struct, owns the bytes externally
 *     and supplies a ->release callback (refcount, mmap teardown, etc.)
 *     that fires when the library is done with the bytes. */
typedef struct sftp_string {
	uint32_t  len;
	uint8_t  *c_str;
	void    (*release)(struct sftp_string *str);
	void     *release_cbdata;
} *sftp_str_t;

struct sftp_file_attributes;
typedef struct sftp_file_attributes *sftp_file_attr_t;

enum sftp_handle_type {
	SFTP_HANDLE_TYPE_DIR,
	SFTP_HANDLE_TYPE_FILE,
};

typedef sftp_str_t sftp_filehandle_t;
typedef sftp_str_t sftp_dirhandle_t;

typedef struct sftp_client_state *sftpc_state_t;

/* sftp_pkt.c — public diagnostic helpers.  The raw packet codec
 * (get32 / append32 / getstring / rx_pkt_append / …) is internal to the
 * single-TU library and intentionally not declared here. */
const char * sftp_get_type_name(uint8_t type);
const char * sftp_get_errcode_name(uint32_t errcode);

/* sftp_str.c */
sftp_str_t sftp_alloc_str(uint32_t len);
sftp_str_t sftp_strdup(const char *str);
sftp_str_t sftp_memdup(const uint8_t *buf, uint32_t sz);
/* Borrow caller bytes only for synchronous packet construction. */
void sftp_memstatic(struct sftp_string *out, const uint8_t *buf, uint32_t len);
void free_sftp_str(sftp_str_t str);

/* sftp_outcome.c — diagnostic outcome carriers.
 *
 * Server side: the consumer stack-allocates struct sftps_outcome via
 * SFTPS_OUTCOME_DECL and passes a pointer; sftps_outcome_record_()
 * appends file:line-prefixed records into the fixed-size estr.
 *
 * Client side: each op returns a struct sftpc_<op>_pending that
 * carries err/result/estr fields directly; estr is library-allocated
 * and grown via realloc as records accumulate, freed by
 * sftpc_pending_free.  See struct sftpc_pending below. */
typedef enum {
	SFTP_ERR_OK = 0,
	SFTP_ERR_NULL_STATE,
	SFTP_ERR_NULL_PATH,
	SFTP_ERR_NULL_HANDLE_OUT,
	SFTP_ERR_NULL_HANDLE,
	SFTP_ERR_HANDLE_NOT_NULL,
	SFTP_ERR_NULL_DATA,
	SFTP_ERR_NULL_OUT,
	SFTP_ERR_OOM,
	SFTP_ERR_PACKET_BUILD_FAILED,
	SFTP_ERR_SEND_FAILED,
	SFTP_ERR_CHANNEL_CLOSED,
	SFTP_ERR_ABORTED,
	SFTP_ERR_REPLY_NULL,
	SFTP_ERR_REPLY_WRONG_TYPE,
	SFTP_ERR_REPLY_BAD_STRING,
	SFTP_ERR_PARSE_FAILED,
} sftp_err_code_t;

/* True for failures that may succeed on retry (transport, abort).
 * Used by callers that distinguish "permanent" from "try-again-later". */
bool sftp_err_is_transient(sftp_err_code_t err);

/* sftp_client.c */
struct sftpc_dir_entry {
	sftp_str_t filename;
	sftp_str_t longname;
	sftp_file_attr_t attrs;
};

/*
 * Async-first client API.  Each sftpc_<op>() builds + sends the
 * request packet, registers a pending entry with the (cb, cbdata)
 * the caller provides, and returns immediately.  The library's recv
 * thread runs the back half: when a reply arrives, it parses it into
 * the typed pending's fields and invokes pending->base.cb(&pending->base).
 * The callback owns consuming the typed fields (memcpy-out, signal
 * an event, push onto a result queue, …) and then calling
 * sftpc_pending_free().
 *
 * Common base — every typed pending starts with this.  Internal
 * fields (req_id, list links, parse, free_self, reply, delivered,
 * aborted) are populated by the library; callers read `outcome`
 * after the callback fires and ignore the rest.
 */
struct sftpc_pending {
	/* Library-managed bookkeeping; callers don't read or write these. */
	uint32_t                  req_id;
	struct sftpc_pending     *prev;
	struct sftpc_pending     *next;
	void                    (*parse)(struct sftpc_pending *);
	void                    (*free_self)(struct sftpc_pending *);
	sftp_rx_pkt_t             reply;
	bool                      delivered;
	bool                      aborted;
	size_t                    estr_len;   /* current estr length, no NUL */
	size_t                    estr_cap;   /* allocated capacity */

	/* Set by the caller before sftpc_<op>() returns; fired when the
	 * back half completes (or synchronously if the front half
	 * encounters an error before sending). */
	void                    (*cb)(struct sftpc_pending *);
	void                     *cbdata;

	/* Outcome — populated by the back half (or by an early-error
	 * code path); valid once the callback fires.
	 *   err == SFTP_ERR_OK and result is an SSH_FX_* code
	 *     — the RPC happened.  result == SSH_FX_OK means the typed
	 *     pending's other fields are populated; any other SSH_FX_*
	 *     means the server returned a STATUS reply with that code.
	 *   err != SFTP_ERR_OK
	 *     — the RPC didn't complete; estr (if non-NULL) carries
	 *     human-readable diagnostic text.
	 * estr is library-allocated and grown via realloc as records
	 * accumulate; freed by sftpc_pending_free.  May be NULL if no
	 * diagnostic was ever recorded. */
	sftp_err_code_t           err;
	uint32_t                  result;
	char                     *estr;
};

/* Op-specific extensions: each is allocated with sizeof(this struct)
 * and stored at &p->base for the library's pending list.  The cb
 * recovers the typed pointer with `(struct sftpc_<op>_pending *)base`
 * since `base` is the first field. */
struct sftpc_realpath_pending {
	struct sftpc_pending base;
	sftp_str_t           ret;       /* server-resolved path */
};

struct sftpc_open_pending {
	struct sftpc_pending base;
	sftp_str_t           handle;    /* server file/dir handle */
};

struct sftpc_read_pending {
	struct sftpc_pending base;
	sftp_str_t           data;
};

struct sftpc_stat_pending {
	struct sftpc_pending base;
	sftp_file_attr_t     attrs;
};

struct sftpc_readdir_pending {
	struct sftpc_pending      base;
	struct sftpc_dir_entry   *entries;
	uint32_t                  count;
	bool                      eof;
};

struct sftpc_descs_pending {
	struct sftpc_pending base;
	sftp_str_t           desc;
};

/* Lifecycle. */
sftpc_state_t sftpc_begin(bool (*send_cb)(uint8_t *buf, size_t len, void *cb_data), void *cb_data);
void sftpc_finish(sftpc_state_t state);
void sftpc_end(sftpc_state_t state);

/* Drains incoming SFTP-channel bytes into pending replies.  Called
 * from the SSH recv thread; safe alongside any in-flight ops. */
bool sftpc_recv(sftpc_state_t state, uint8_t *buf, uint32_t sz);

/* Free a pending allocated by any sftpc_<op>().  Releases any typed
 * fields the back half populated (sftp_str_t, attrs, dir entries)
 * and the pending struct itself.  Idempotent on NULL. */
void sftpc_pending_free(struct sftpc_pending *p);

/* Async ops.  Each returns the typed pending pointer (or NULL only
 * on alloc failure — extremely rare).  The `cb` fires exactly once
 * unless the call returned NULL, and may fire synchronously before
 * the front half returns if the request couldn't be sent (state
 * terminating, packet build / transport failure, etc.).  `outcome`
 * carries the result either way. */
struct sftpc_pending          *sftpc_init(sftpc_state_t state,
                                          void (*cb)(struct sftpc_pending *),
                                          void *cbdata);
struct sftpc_realpath_pending *sftpc_realpath(sftpc_state_t state,
                                              const char *path,
                                              void (*cb)(struct sftpc_pending *),
                                              void *cbdata);
struct sftpc_open_pending     *sftpc_open(sftpc_state_t state,
                                          const char *path, uint32_t flags,
                                          void (*cb)(struct sftpc_pending *),
                                          void *cbdata);
struct sftpc_open_pending     *sftpc_opendir(sftpc_state_t state,
                                              const char *path,
                                              void (*cb)(struct sftpc_pending *),
                                              void *cbdata);
struct sftpc_pending          *sftpc_close(sftpc_state_t state,
                                            sftp_str_t handle,
                                            void (*cb)(struct sftpc_pending *),
                                            void *cbdata);
struct sftpc_read_pending     *sftpc_read(sftpc_state_t state,
                                          sftp_str_t handle,
                                          uint64_t offset, uint32_t len,
                                          void (*cb)(struct sftpc_pending *),
                                          void *cbdata);
struct sftpc_pending          *sftpc_write(sftpc_state_t state,
                                            sftp_str_t handle,
                                            uint64_t offset, sftp_str_t data,
                                            void (*cb)(struct sftpc_pending *),
                                            void *cbdata);
struct sftpc_stat_pending     *sftpc_stat(sftpc_state_t state,
                                          const char *path,
                                          void (*cb)(struct sftpc_pending *),
                                          void *cbdata);
struct sftpc_pending          *sftpc_setstat(sftpc_state_t state,
                                              const char *path,
                                              sftp_file_attr_t attr,
                                              void (*cb)(struct sftpc_pending *),
                                              void *cbdata);
struct sftpc_readdir_pending  *sftpc_readdir(sftpc_state_t state,
                                              sftp_str_t handle,
                                              void (*cb)(struct sftpc_pending *),
                                              void *cbdata);
struct sftpc_descs_pending    *sftpc_descs(sftpc_state_t state,
                                            const char *path,
                                            void (*cb)(struct sftpc_pending *),
                                            void *cbdata);
struct sftpc_pending          *sftpc_mkdir(sftpc_state_t state,
                                            const char *path,
                                            sftp_file_attr_t attr,
                                            void (*cb)(struct sftpc_pending *),
                                            void *cbdata);
struct sftpc_pending          *sftpc_rmdir(sftpc_state_t state,
                                            const char *path,
                                            void (*cb)(struct sftpc_pending *),
                                            void *cbdata);
struct sftpc_pending          *sftpc_remove(sftpc_state_t state,
                                            const char *path,
                                            void (*cb)(struct sftpc_pending *),
                                            void *cbdata);
struct sftpc_pending          *sftpc_rename(sftpc_state_t state,
                                            const char *oldpath,
                                            const char *newpath,
                                            void (*cb)(struct sftpc_pending *),
                                            void *cbdata);

/* Free the entries array attached to a struct sftpc_readdir_pending.
 * Callers that transfer ownership out (setting pending->entries=NULL
 * and pending->count=0) free it themselves later; otherwise
 * sftpc_pending_free handles the still-attached array. */
void sftpc_free_dir_entries(struct sftpc_dir_entry *entries, uint32_t count);

uint32_t sftpc_get_extensions(sftpc_state_t state);
/* Public-directory path the server advertised via the pubdir@syncterm.net
 * extension during VERSION, or NULL if the extension wasn't negotiated.
 * The library owns the string; valid for the lifetime of `state`. */
const char *sftpc_get_pubdir(sftpc_state_t state);

/* sftp_attr.c */
sftp_file_attr_t sftp_fattr_alloc(void);
void sftp_fattr_free(sftp_file_attr_t fattr);
bool sftp_fattr_get_size(sftp_file_attr_t fattr, uint64_t *sz);
bool sftp_fattr_get_uid(sftp_file_attr_t fattr, uint32_t *uid);
bool sftp_fattr_get_gid(sftp_file_attr_t fattr, uint32_t *gid);
bool sftp_fattr_get_permissions(sftp_file_attr_t fattr, uint32_t *perm);
void sftp_fattr_set_times(sftp_file_attr_t fattr, uint32_t atime, uint32_t mtime);
bool sftp_fattr_get_atime(sftp_file_attr_t fattr, uint32_t *atime);
bool sftp_fattr_get_mtime(sftp_file_attr_t fattr, uint32_t *mtime);
bool sftp_fattr_add_ext(sftp_file_attr_t *fattr, sftp_str_t type, sftp_str_t data);
sftp_str_t sftp_fattr_get_ext_by_type(sftp_file_attr_t fattr, const char *type);


#endif
