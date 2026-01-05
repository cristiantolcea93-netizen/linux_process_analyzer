#ifndef PROCESS_SNAPSHOT_PROCESS_SNAPSHOT_H_
#define PROCESS_SNAPSHOT_PROCESS_SNAPSHOT_H_

typedef enum
{
	process_snapshot_error=-1,
	process_snapshot_success=0
}process_snapshot_status;


extern process_snapshot_status collect_snapshot(void);
extern process_snapshot_status process_snapshot_initialize(void);
extern void process_snapshot_deinit(void);



#endif /* PROCESS_SNAPSHOT_PROCESS_SNAPSHOT_H_ */
