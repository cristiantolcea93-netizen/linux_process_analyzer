#include "stdio.h"
#include "process_snapshot.h"

int main(void)
{
	process_snapshot_status l_process_status = process_snapshot_error;
	l_process_status = process_snapshot_initialize();

	if(process_snapshot_success == l_process_status)
	{
		collect_snapshot();
		process_snapshot_deinit();
	}

	return 0;
}
