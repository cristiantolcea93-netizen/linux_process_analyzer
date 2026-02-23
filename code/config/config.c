#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include "config.h"
#include "unistd.h"

#define CONFIG_MIN_FILE_SIZE_BYTES 1024
#define CONFIG_MIN_NO_OF_FILES 1
#define CONFIG_MAX_NO_OF_FILES 1000

typedef struct
{
    char output_dir[PATH_MAX];

    bool raw_log_enabled;
    bool raw_jsonl_enabled;
    bool raw_console_enabled;

    bool compression_enabled;

    bool metrics_console_enabled;
    bool metrics_json_enabled;

    off_t max_file_size_bytes;
    int max_number_of_files;

    bool include_self;

}config_t;



static const config_t config_default =
{

    /* output */
    .output_dir = "/tmp/ptime",

    /* raw */
    .raw_log_enabled     = true,
    .raw_jsonl_enabled   = true,
    .raw_console_enabled = false,

	/*compression*/
	.compression_enabled = true,

    /* metrics */
    .metrics_console_enabled = true,
    .metrics_json_enabled    = true,

    /* rotation */
    .max_file_size_bytes = 5 * 1024 * 1024, /* 5MB */
    .max_number_of_files = 3,

	/*additional options*/
	.include_self = false
};


static config_t configuration_t;

static char* trim(char* s);
static config_status parse_config_file(const char* path);
static int parse_bool(const char* v, bool* out);
static int parse_size(const char* v, off_t* out);
static int config_set_option(const char* key,const char* value);
static config_status validate_config(void);
static const char* truefalse (bool v);
static void print_size(size_t bytes);
static int ensure_output_dir(const char* path);
static int mkdir_p(const char *path, mode_t mode);
static int check_file_access(const char* dir);


static char* trim(char* s)
{
    while (isspace(*s)) s++;

    if (*s == 0) return s;

    char* end = s + strlen(s) - 1;

    while (end > s && isspace(*end))
        *end-- = 0;

    return s;
}

static config_status parse_config_file(const char* path)
{
	struct stat st;

	if (stat(path, &st) != 0)
	{
	    fprintf(stderr,"Config error: cannot access config file %s: %s\n",path, strerror(errno));
	    return config_error_io;
	}

	if (!S_ISREG(st.st_mode))
	{
	    fprintf(stderr,"Config error: %s is not a regular file\n",path);
	    return config_error_io;
	}


    FILE* f = fopen(path, "r");
    if (!f)
    {
    	fprintf(stderr,"parse_config_file: failed to open %s\n",path);
        return config_error_io;
    }

    char line[512];
    int lineno = 0;

    while (fgets(line, sizeof(line), f))
    {

        lineno++;

        char* s = trim(line);

        /* empty / comment */
        if (*s == 0 || *s == '#')
            continue;

        char* eq = strchr(s, '=');
        if (!eq)
        {
            fprintf(stderr,"parse_config_file: Config error line %d: missing '='\n", lineno);
            fclose(f);
            return config_error_parse;
        }

        *eq = 0;

        char* key   = trim(s);
        char* value = trim(eq + 1);

        if (config_set_option(key, value) != 0)
        {
            fprintf(stderr,"parse_config_file: Config error line %d: invalid option '%s'\n",lineno, key);
            fclose(f);
            return config_error_parse;
        }
    }

    fclose(f);
    return config_success;
}

static int parse_bool(const char* v, bool* out)
{
    if (!strcasecmp(v, "true") ||
        !strcasecmp(v, "yes") ||
        !strcmp(v, "1"))
    {
        *out = true;
        return 0;
    }

    if (!strcasecmp(v, "false") ||
        !strcasecmp(v, "no") ||
        !strcmp(v, "0"))
    {
        *out = false;
        return 0;
    }

    return -1;
}

static int parse_size(const char* v, off_t* out)
{
    char* end;
    unsigned long long n = strtoull(v, &end, 10);

    if (end == v)
        return -1;

    switch (*end)
    {
        case 0:
            break;

        case 'k': case 'K':
            n *= 1024;
            break;

        case 'm': case 'M':
            n *= 1024 * 1024;
            break;

        case 'g': case 'G':
        	n*= 1024 * 1024 * 1024;
        	break;

        default:
            return -1;
    }

    *out = (off_t)n;
    return 0;
}


static int config_set_option(const char* key,const char* value)
{
    if (!strcmp(key, "output_dir"))
    {
        if (strlen(value) >= PATH_MAX)
            return -1;

        strcpy(configuration_t.output_dir, value);
        return 0;
    }

    if (!strcmp(key, "raw_log_enabled"))
        return parse_bool(value, &configuration_t.raw_log_enabled);

    if (!strcmp(key, "raw_jsonl_enabled"))
        return parse_bool(value, &configuration_t.raw_jsonl_enabled);

    if (!strcmp(key, "raw_console_enabled"))
        return parse_bool(value, &configuration_t.raw_console_enabled);

    if (!strcmp(key, "compression_enabled"))
        return parse_bool(value, &configuration_t.compression_enabled);

    if (!strcmp(key, "metrics_on_console"))
        return parse_bool(value, &configuration_t.metrics_console_enabled);

    if (!strcmp(key, "metrics_on_json"))
        return parse_bool(value, &configuration_t.metrics_json_enabled);

    if (!strcmp(key, "max_file_size"))
        return parse_size(value,&configuration_t.max_file_size_bytes);

    if (!strcmp(key, "max_number_of_files"))
    {
        int n = atoi(value);
        configuration_t.max_number_of_files = n;
        return 0;
    }

    if(!strcmp(key, "include_self"))
    	return  parse_bool(value, &configuration_t.include_self);


    return -1; /* unknown key */
}



static const char* truefalse (bool v)
{
	return v ? "yes" : "no";
}

static void print_size(size_t bytes)
{
    if (bytes >= 1024 * 1024)
        printf("%zu MB", bytes / (1024 * 1024));
    else if (bytes >= 1024)
        printf("%zu KB", bytes / 1024);
    else
        printf("%zu B", bytes);
}

static int mkdir_p(const char *path, mode_t mode)
{
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;

    if (!path)
        return -1;

    len = strlen(path);

    if (len >= sizeof(tmp))
        return -1;

    strcpy(tmp, path);

    /* Remove trailing slash */
    if (tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    for (p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';

            if (mkdir(tmp, mode) != 0)
            {
                if (errno != EEXIST)
                    return -1;
            }

            *p = '/';
        }
    }

    if (mkdir(tmp, mode) != 0)
    {
        if (errno != EEXIST)
            return -1;
    }

    return 0;
}


static int ensure_output_dir(const char* path)
{
    struct stat st;

    /* Check if path exists */
    if (stat(path, &st) == 0)
    {
        /* Exists: must be directory */
        if (!S_ISDIR(st.st_mode))
        {
            fprintf(stderr,"Config error: %s exists but is not a directory\n",path);
            return -1;
        }
    }
    else
    {
        /* Does not exist -> try to create */
        if (mkdir_p(path, 0755) != 0)
        {
            fprintf(stderr,"Config error: cannot create directory %s: %s\n",path,strerror(errno));
            return -1;
        }

        printf("config: created output directory %s\n", path);
    }

    /* Now check write access */
    if (check_file_access(path) != 0)
    {
        fprintf(stderr,"Config error: no write permission for %s\n",path);
        return -1;
    }

    return 0;
}

static int check_file_access(const char* dir)
{
    char path[PATH_MAX];

    int ret = snprintf(path, sizeof(path), "%s/%s", dir, "dummyfile");
    if ((ret >= (int)sizeof(path)) || (ret < 0))
        return -1;

    int fd = open(path,O_CREAT | O_RDWR,0644);

    if (fd < 0)
        return -1;

    close(fd);
    unlink(path);
    return 0;
}



static config_status validate_config(void)
{
    /* output dir */

    if (ensure_output_dir(configuration_t.output_dir) != 0)
    {
        return config_error_validation;
    }


    if (configuration_t.max_file_size_bytes < CONFIG_MIN_FILE_SIZE_BYTES)
    {
        fprintf(stderr,"Config error: max_file_size too small,minimum size: %d, config value %jd\n", CONFIG_MIN_FILE_SIZE_BYTES, configuration_t.max_file_size_bytes);
        return config_error_validation;
    }

    if (configuration_t.max_number_of_files < CONFIG_MIN_NO_OF_FILES || configuration_t.max_number_of_files >= CONFIG_MAX_NO_OF_FILES)
    {
        fprintf(stderr,"Config error: invalid max_number_of_files expected a value between %d and %d, config value: %d\n",CONFIG_MIN_NO_OF_FILES, CONFIG_MAX_NO_OF_FILES, configuration_t.max_number_of_files);
        return config_error_validation;
    }

    if (!configuration_t.raw_log_enabled &&
        !configuration_t.raw_jsonl_enabled &&
        !configuration_t.raw_console_enabled)
    {

        fprintf(stderr,"Config error: all raw outputs disabled\n");
        return config_error_validation;
    }
    return config_success;
}

void config_print_banner(void)
{
    printf("\n");
    printf("=================================================\n");
    printf("   Process Analyzer - Active Configuration\n");
    printf("=================================================\n");

    printf(" Output directory      : %s\n", configuration_t.output_dir);
    printf("\n");

    printf(" Raw snapshots:\n");
    printf("   .log file            : %s\n", truefalse(configuration_t.raw_log_enabled));
    printf("   .jsonl file          : %s\n", truefalse(configuration_t.raw_jsonl_enabled));
    printf("   console              : %s\n", truefalse(configuration_t.raw_console_enabled));
    printf("\n");

    printf(" Compression:\n");
    printf("   compression enabled  : %s\n", truefalse(configuration_t.compression_enabled));
    printf("\n");

    printf(" Metrics output:\n");
    printf("   console              : %s\n", truefalse(configuration_t.metrics_console_enabled));
    printf("   metrics.json         : %s\n", truefalse(configuration_t.metrics_json_enabled));
    printf("\n");

    printf(" Log rotation:\n");
    printf("   max file size         : ");
    print_size(configuration_t.max_file_size_bytes);
    printf("\n");

    printf("   max files             : %d\n",configuration_t.max_number_of_files);
    printf("\n");

    printf(" General options\n");
    printf("   include self          : %s\n",truefalse(configuration_t.include_self));

    printf("=================================================\n\n");
}

config_status config_init(void)
{
	char* config_file_path = getenv("PROCESS_ANALYZER_CONFIG");

	if(config_file_path!=NULL)
	{
		printf("config_init: parsing configuration from %s\n",config_file_path );
		config_status st = parse_config_file(config_file_path);
		if(config_success != st)
		{
			//parsing failed
			return st;
		}
	}
	else
	{
		printf("config_init: using default configuration\n");
		configuration_t = config_default;
	}
	return validate_config();
}

const char* config_get_output_dir(void)
{
	return (const char*)configuration_t.output_dir;
}

off_t config_get_max_file_size_bytes(void)
{
	return configuration_t.max_file_size_bytes;
}

int config_get_max_number_of_files(void)
{
	return configuration_t.max_number_of_files;
}

bool config_get_raw_console_enabled(void)
{
	return configuration_t.raw_console_enabled;
}

bool config_get_raw_log_enabled(void)
{
	return configuration_t.raw_log_enabled;
}

bool config_get_raw_jsonl_enabled(void)
{
	return configuration_t.raw_jsonl_enabled;
}

bool config_get_metrics_console_enabled(void)
{
	return configuration_t.metrics_console_enabled;
}

bool config_get_metrics_json_enabled(void)
{
	return configuration_t.metrics_console_enabled;
}

bool config_get_include_self(void)
{
	return configuration_t.include_self;
}

bool config_get_compression_enabled(void)
{
	return configuration_t.compression_enabled;
}
