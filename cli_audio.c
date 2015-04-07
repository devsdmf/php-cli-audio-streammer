#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "php.h"
#include "php_ini.h"
#include "php_cli_audio.h"
#include "mikmod.h"

ZEND_DECLARE_MODULE_GLOBALS(cli_audio)

static zend_function_entry cli_audio_functions[] = {
	PHP_FE(cli_audio_stream, NULL)
	PHP_FE(cli_audio_getpid, NULL)
	PHP_FE(cli_audio_stop, NULL)
	{NULL, NULL, NULL}
};

zend_module_entry cli_audio_module_entry = {
	#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
	#endif
	PHP_CLI_AUDIO_EXTNAME,
	cli_audio_functions,
	PHP_MINIT(cli_audio),
	PHP_MSHUTDOWN(cli_audio),
	NULL,
	NULL,
	NULL,
	#if ZEND_MODULE_API_NO >= 20010901
	PHP_CLI_AUDIO_VERSION,
	#endif
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_CLI_AUDIO
ZEND_GET_MODULE(cli_audio)
#endif

PHP_INI_BEGIN()
PHP_INI_ENTRY("cli_audio.maxchan","64",PHP_INI_ALL,NULL)
PHP_INI_ENTRY("cli_audio.curious","0",PHP_INI_ALL,NULL)
PHP_INI_END()

static void php_cli_audio_init_globals(zend_cli_audio_globals *cli_audio_globals)
{
	cli_audio_globals->pid = 0;
}

PHP_MINIT_FUNCTION(cli_audio)
{
	ZEND_INIT_MODULE_GLOBALS(cli_audio, php_cli_audio_init_globals, NULL);
	REGISTER_INI_ENTRIES();
	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(cli_audio)
{
	if (CLI_AUDIO_G(pid) > 0) {
		kill(CLI_AUDIO_G(pid), SIGKILL);
	}
	
	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}

PHP_FUNCTION(cli_audio_stream)
{
	FILE *fptr;
	char *filename;
	int filename_length, s_pid;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &filename, &filename_length) == FAILURE) {
        RETURN_NULL();
    }

    if (CLI_AUDIO_G(pid) > 0) {
    	zend_error(E_ERROR, "You've already started the CLI audio stream");
    	RETURN_NULL();
    }

	fptr = fopen(filename, "rb");
	if (fptr == NULL) {
		perror("Error");
        zend_error(E_ERROR, "An error occurred at try to open the audio file specified");
        RETURN_NULL();
    }

	s_pid = stream_audio(fptr, INI_INT("cli_audio.maxchan"), INI_INT("cli_audio.curious"));

	if (s_pid > 0) {
		CLI_AUDIO_G(pid) = s_pid;
	}

	RETURN_LONG(s_pid);
}

PHP_FUNCTION(cli_audio_getpid)
{
	RETURN_LONG(CLI_AUDIO_G(pid));
}

PHP_FUNCTION(cli_audio_stop)
{
	if (CLI_AUDIO_G(pid) > 0) {
		kill(CLI_AUDIO_G(pid), SIGKILL);
		CLI_AUDIO_G(pid) = 0;
	}

	RETURN_NULL();
}

int stream_audio(FILE *fptr, int maxchan, int curious)
{
	pid_t m_pid;
	signed int s_pid;

	m_pid = fork();
	if (m_pid == 0) {
		MODULE *module;

		MikMod_InitThreads();
        MikMod_RegisterAllDrivers();
        MikMod_RegisterAllLoaders();

        md_mode |= DMODE_SOFT_MUSIC | DMODE_NOISEREDUCTION | DMODE_INTERP;
        if (MikMod_Init("")) {
            zend_error(E_ERROR, "Could not initialize the MikMod library");
            return -1;
        }

        module = Player_LoadFP(fptr, maxchan, curious);
        if (module) {
        	module->wrap = 1;
        	module->loop = 0;

        	Player_Start(module);

        	s_pid = getpid();
        	CLI_AUDIO_G(pid) = s_pid;

        	while (Player_Active()) {
        		usleep(10000);
        		MikMod_Update();
        	}
        } else {
        	zend_error(E_ERROR, "Could not load module");
        	return -1;
        }

        fclose(fptr);
        MikMod_Exit();
	} else if (m_pid < 0) {
		zend_error(E_ERROR, "Failed to fork CLI Audio Stream process");
		return -1;
	}

	return m_pid;
}