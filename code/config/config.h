#ifndef CONFIG_CONFIG_H_
#define CONFIG_CONFIG_H_

#include <stdbool.h>
#include <limits.h>



#define CONFIG_LOG_FILE  "ptime.log"
#define CONFIG_JSON_FILE "ptime.jsonl"
#define CONFIG_METRICS_JSON "metrics.json"
#define CONFIG_LOCK_FILE "ptime.lock"


typedef enum
{
	config_success=0,
	config_error_io=1,
	config_error_parse=2,
	config_error_validation=3
}config_status;

extern config_status config_init(void);
extern void config_print_banner(void);
extern const char* config_get_output_dir(void);
extern off_t config_get_max_file_size_bytes(void);
extern int config_get_max_number_of_files(void);
extern bool config_get_raw_console_enabled(void);
extern bool config_get_raw_log_enabled(void);
extern bool config_get_raw_jsonl_enabled(void);
extern bool config_get_metrics_console_enabled(void);
extern bool config_get_metrics_json_enabled(void);
extern bool config_get_include_self(void);
extern bool config_get_compression_enabled(void);

#endif /* CONFIG_CONFIG_H_ */
