#ifndef COMPRESSION_WORKER_H
#define COMPRESSION_WORKER_H

extern void compression_worker_init(void);
extern void compression_worker_shutdown(void);

extern void compression_enqueue_file(const char *path);

#endif
