#ifndef CONFIG_CONFIG_H_
#define CONFIG_CONFIG_H_


#define CONFIG_MAX_LOG_SIZE   (5 * 1024 * 1024)  // 5 MB
#define CONFIG_MAX_ROTATIONS  3

/*todo: replace this with configuration option*/
#define CONFIG_LOG_DIR   "/tmp/ptime"
#define CONFIG_LOG_FILE  "ptime.log"
#define CONFIG_JSON_FILE "ptime.jsonl"
#define CONFIG_METRICS_JSON "metrics.json"


#define CONFIG_LOCK_FILE "/tmp/ptime/ptime.lock"


#endif /* CONFIG_CONFIG_H_ */
