/* sg_rune_install.h -- checked, atomic installation for streamed RUNE v3. */
#ifndef SG_RUNE_INSTALL_H
#define SG_RUNE_INSTALL_H

#include <stddef.h>
#include <stdio.h>

#include "sg_rune_writer.h"

#define SG_RUNE_INSTALL_TEMP_ATTEMPTS 64U

/* Stable transaction outcomes.  The writer result remains separate so a
 * native-record rejection cannot be mistaken for a filesystem failure. */
typedef enum sg_rune_install_status_e
{
	SG_RUNE_INSTALL_OK = 0,
	SG_RUNE_INSTALL_INVALID_ARGUMENT,
	SG_RUNE_INSTALL_BAD_PATH,
	SG_RUNE_INSTALL_TEMP_OPEN_FAILED,
	SG_RUNE_INSTALL_TEMP_EXHAUSTED,
	SG_RUNE_INSTALL_WRITER_FAILED,
	SG_RUNE_INSTALL_FLUSH_FAILED,
	SG_RUNE_INSTALL_SYNC_FAILED,
	SG_RUNE_INSTALL_CLOSE_FAILED,
	SG_RUNE_INSTALL_REVALIDATE_FAILED,
	SG_RUNE_INSTALL_RENAME_FAILED
} sg_rune_install_status_t;

typedef struct sg_rune_install_result_s
{
	sg_rune_install_status_t status;
	int os_error;
	/* A cleanup error is secondary; status retains the operation that made
	 * commit impossible so corpus clustering never loses the primary cause. */
	int cleanup_error;
	unsigned int temp_attempt;
	int writer_called;
	sg_rune_write_result_t writer;
} sg_rune_install_result_t;

/* The stream callback receives the transaction's exact-write sink. */
typedef sg_rune_write_result_t (*sg_rune_install_stream_fn)(void *context,
	sg_rune_write_sink_fn sink, void *sink_context);

/* Called after the temporary is flushed, synced, and closed, immediately
 * before rename.  It must recheck every authority captured before generation. */
typedef int (*sg_rune_install_revalidate_fn)(void *context);

/* Injectable narrow filesystem boundary.  Return conventions match stdio:
 * byte count for write, zero for successful scalar operations.  close_file
 * always consumes the FILE handle, including when it reports a late error. */
typedef struct sg_rune_install_ops_s
{
	void *context;
	FILE *(*open_exclusive)(void *context, const char *path);
	size_t (*write)(void *context, FILE *file, const void *data,
		size_t data_size);
	int (*flush)(void *context, FILE *file);
	int (*sync_file)(void *context, FILE *file);
	int (*close_file)(void *context, FILE *file);
	int (*rename_replace)(void *context, const char *temporary_path,
		const char *destination_path);
	int (*remove_path)(void *context, const char *path);
	unsigned long (*process_id)(void *context);
} sg_rune_install_ops_t;

const sg_rune_install_ops_t *SG_RuneInstallDefaultOps(void);
const char *SG_RuneInstallReason(sg_rune_install_status_t status);

/* Both formatters clear output on every rejection and report truncation. */
int SG_RuneInstallDestinationPath(char *output, size_t output_size,
	const char *game_directory, const char *map_name);
int SG_RuneInstallTemporaryPath(char *output, size_t output_size,
	const char *game_directory, unsigned long process_id,
	unsigned int attempt);

/* The caller owns both path buffers; production passes MAX_OSPATH arrays.
 * Before opening anything, the stream runs once against a counting sink so
 * malformed identity/native graphs fail without filesystem access.  A second
 * complete writer pass streams to an exclusive temporary beside the
 * destination.  Rename is the sole commit point. */
sg_rune_install_result_t SG_RuneInstallV3(
	const char *game_directory, const char *map_name,
	char *destination_path, size_t destination_path_size,
	char *temporary_path, size_t temporary_path_size,
	sg_rune_install_stream_fn stream, void *stream_context,
	sg_rune_install_revalidate_fn revalidate, void *revalidate_context,
	const sg_rune_install_ops_t *ops);

#endif /* SG_RUNE_INSTALL_H */
