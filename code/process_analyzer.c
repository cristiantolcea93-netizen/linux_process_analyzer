#include "process_snapshot.h"
#include "args_parser.h"


ap_arguments gArguments;



int main(int argc, char **argv)
{
	parse_args_status l_parse_status  = ap_parse_args(argc, argv, &gArguments);

	if(parse_args_ok == l_parse_status)
	{
		process_snapshot_status l_process_status = process_snapshot_error;
		l_process_status = process_snapshot_initialize();
		if(process_snapshot_success == l_process_status)
		{
			collect_snapshot();
			process_snapshot_deinit();
		}
	}
	return 0;
}
