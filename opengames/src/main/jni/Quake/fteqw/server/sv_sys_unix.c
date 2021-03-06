/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#include <signal.h>
#include <sys/types.h>
#include <dlfcn.h>
#include "quakedef.h"


#undef malloc

#ifdef NeXT
#include <libc.h>
#endif

#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>

#ifdef MULTITHREAD
#include <pthread.h>
#endif

// callbacks
void Sys_Linebuffer_Callback (struct cvar_s *var, char *oldvalue);

cvar_t sys_nostdout = CVAR("sys_nostdout","0");
cvar_t sys_extrasleep = CVAR("sys_extrasleep","0");
cvar_t sys_colorconsole = CVAR("sys_colorconsole", "0");
cvar_t sys_linebuffer = CVARC("sys_linebuffer", "1", Sys_Linebuffer_Callback);

qboolean	stdin_ready;

struct termios orig, changes;

/*
===============================================================================

				REQUIRED SYS FUNCTIONS

===============================================================================
*/

/*
============
Sys_FileTime

returns -1 if not present
============
*/
int	Sys_FileTime (char *path)
{
	struct	stat	buf;

	if (stat (path,&buf) == -1)
		return -1;

	return buf.st_mtime;
}


/*
============
Sys_mkdir

============
*/
void Sys_mkdir (char *path)
{
	if (mkdir (path, 0777) != -1)
		return;
//	if (errno != EEXIST)
//		Sys_Error ("mkdir %s: %s",path, strerror(errno));
}

qboolean Sys_remove (char *path)
{
	return system(va("rm \"%s\"", path));
}

qboolean Sys_Rename (char *oldfname, char *newfname)
{
	return !rename(oldfname, newfname);
}

int Sys_DebugLog(char *file, char *fmt, ...)
{
	va_list argptr;
	char data[1024];
	int fd;
	size_t result;

	va_start(argptr, fmt);
	vsnprintf (data,sizeof(data)-1, fmt, argptr);
	va_end(argptr);

	if (strlen(data) >= sizeof(data)-1)
		Sys_Error("Sys_DebugLog was stomped\n");

	fd = open(file, O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (fd)
	{
		result = write(fd, data, strlen(data)); // do something with the result

		if (result != strlen(data))
			Con_Printf("Sys_DebugLog() write: Filename: %s, expected %lu, result was %lu (%s)\n",file,(unsigned long)strlen(data),(unsigned long)result,strerror(errno));

		close(fd);
		return 0;
	}
	return 1;
}

/*
================
Sys_Milliseconds
================
*/
unsigned int Sys_Milliseconds (void)
{
	struct timeval tp;
	struct timezone tzp;
	static int secbase;

	gettimeofday(&tp, &tzp);

	if (!secbase)
	{
		secbase = tp.tv_sec;
		return tp.tv_usec/1000;
	}
	return (tp.tv_sec - secbase)*1000 + tp.tv_usec/1000;
}

/*
================
Sys_DoubleTime
================
*/
double Sys_DoubleTime (void)
{
	struct timeval tp;
	struct timezone tzp;
	static int		secbase;

	gettimeofday(&tp, &tzp);

	if (!secbase)
	{
		secbase = tp.tv_sec;
		return tp.tv_usec/1000000.0;
	}

	return (tp.tv_sec - secbase) + tp.tv_usec/1000000.0;
}

/*
================
Sys_Error
================
*/
void Sys_Error (const char *error, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr,error);
	vsnprintf (string,sizeof(string)-1, error,argptr);
	va_end (argptr);
	printf ("Fatal error: %s\n",string);

	tcsetattr(STDIN_FILENO, TCSADRAIN, &orig);

*(int*)-3 = 0;
	exit (1);
}

int ansiremap[8] = {0, 4, 2, 6, 1, 5, 3, 7};
void ApplyColour(unsigned int chr)
{
	static int oldchar = CON_WHITEMASK;
	int bg, fg;
	chr &= CON_FLAGSMASK;

	if (oldchar == chr)
		return;
	oldchar = chr;

	printf("\e[0;"); // reset

	if (chr & CON_BLINKTEXT)
		printf("5;"); // set blink

	bg = (chr & CON_BGMASK) >> CON_BGSHIFT;
	fg = (chr & CON_FGMASK) >> CON_FGSHIFT;

	// don't handle intensive bit for background
	// as terminals differ too much in displaying \e[1;7;3?m
	bg &= 0x7;

	if (chr & CON_NONCLEARBG)
	{
		if (fg & 0x8) // intensive bit set for foreground
		{
			printf("1;"); // set bold/intensity ansi flag
			fg &= 0x7; // strip intensive bit
		}

		// set foreground and background colors
		printf("3%i;4%im", ansiremap[fg], ansiremap[bg]);
	}
	else
	{
		switch(fg)
		{
		//to get around wierd defaults (like a white background) we have these special hacks for colours 0 and 7
		case COLOR_BLACK:
			printf("7m"); // set inverse
			break;
		case COLOR_GREY:
			printf("1;30m"); // treat as dark grey
			break;
		case COLOR_WHITE:
			printf("m"); // set nothing else
			break;
		default:
			if (fg & 0x8) // intensive bit set for foreground
			{
				printf("1;"); // set bold/intensity ansi flag
				fg &= 0x7; // strip intensive bit
			}

			printf("3%im", ansiremap[fg]); // set foreground
			break;
		}
	}
}

#define putch(c) putc(c, stdout);
void Sys_PrintColouredChar(unsigned int chr)
{
	ApplyColour(chr);

	chr = chr & CON_CHARMASK;

	if ((chr > 128 || chr < 32) && chr != 10 && chr != 13 && chr != 9)
		printf("[%02x]", chr);
	else
		chr &= ~0x80;

	putch(chr);
}

/*
================
Sys_Printf
================
*/
#define	MAXPRINTMSG	4096
char	coninput_text[256];
int		coninput_len;
void Sys_Printf (char *fmt, ...)
{
	va_list		argptr;

	if (sys_nostdout.value)
		return;

	if (1)
	{
		char		msg[MAXPRINTMSG];
		unsigned char *t;

		va_start (argptr,fmt);
		vsnprintf (msg,sizeof(msg)-1, fmt,argptr);
		va_end (argptr);

		if (!sys_linebuffer.value)
		{
			int i;

			for (i = 0; i < coninput_len; i++)
				putch('\b');
			putch('\b');
			for (i = 0; i < coninput_len; i++)
				putch(' ');
			putch(' ');
			for (i = 0; i < coninput_len; i++)
				putch('\b');
			putch('\b');
		}


		for (t = (unsigned char*)msg; *t; t++)
		{
			if (*t >= 146 && *t < 156)
				*t = *t - 146 + '0';
			if (*t >= 0x12 && *t <= 0x1b)
				*t = *t - 0x12 + '0';
			if (*t == 143)
				*t = '.';
			if (*t == 157 || *t == 158 || *t == 159)
				*t = '-';
			if (*t >= 128)
				*t -= 128;
			if (*t == 16)
				*t = '[';
			if (*t == 17)
				*t = ']';
			if (*t == 0x1c)
				*t = 249;
		}

		if (sys_colorconsole.value)
		{
			int ext = CON_WHITEMASK;
			int extstack[4];
			int extstackdepth = 0;
			unsigned char *str = (unsigned char*)msg;


			while(*str)
			{
				if (*str == '^')
				{
					str++;
					if (*str >= '0' && *str <= '9')
					{
						ext = q3codemasks[*str++-'0'] | (ext&~CON_Q3MASK);	//change colour only.
						continue;
					}
					else if (*str == '&') // extended code
					{
						if (isextendedcode(str[1]) && isextendedcode(str[2]))
						{
							str++; // foreground char
							if (*str == '-') // default for FG
								ext = (COLOR_WHITE << CON_FGSHIFT) | (ext&~CON_FGMASK);
							else if (*str >= 'A')
								ext = ((*str - ('A' - 10)) << CON_FGSHIFT) | (ext&~CON_FGMASK);
							else
								ext = ((*str - '0') << CON_FGSHIFT) | (ext&~CON_FGMASK);
							str++; // background char
							if (*str == '-') // default (clear) for BG
								ext &= ~CON_BGMASK & ~CON_NONCLEARBG;
							else if (*str >= 'A')
								ext = ((*str - ('A' - 10)) << CON_BGSHIFT) | (ext&~CON_BGMASK) | CON_NONCLEARBG;
							else
								ext = ((*str - '0') << CON_BGSHIFT) | (ext&~CON_BGMASK) | CON_NONCLEARBG;
							str++;
							continue;
						}
						Sys_PrintColouredChar('^' | ext);
						// else invalid code
					}
					else if (*str == 'a')
					{
						str++;
						ext ^= CON_2NDCHARSETTEXT;
						continue;
					}
					else if (*str == 'b')
					{
						str++;
						ext ^= CON_BLINKTEXT;
						continue;
					}
					else if (*str == 'h')
					{
						str++;
						ext ^= CON_HALFALPHA;
						continue;
					}
					else if (*str == 's')	//store on stack (it's great for names)
					{
						str++;
						if (extstackdepth < sizeof(extstack)/sizeof(extstack[0]))
						{
							extstack[extstackdepth] = ext;
							extstackdepth++;
						}
						continue;
					}
					else if (*str == 'r')	//restore from stack (it's great for names)
					{
						str++;
						if (extstackdepth)
						{
							extstackdepth--;
							ext = extstack[extstackdepth];
						}
						continue;
					}
					else if (*str == '^')
					{
						Sys_PrintColouredChar('^' | ext);
						str++;
					}
					else
					{
						Sys_PrintColouredChar('^' | ext);
						Sys_PrintColouredChar ((*str++) | ext);
					}
					continue;
				}
				Sys_PrintColouredChar ((*str++) | ext);
			}

			ApplyColour(CON_WHITEMASK);
		}
		else
		{
			for (t = msg; *t; t++)
			{
				*t &= 0x7f;
				if ((*t > 128 || *t < 32) && *t != 10 && *t != 13 && *t != 9)
					printf("[%02x]", *t);
				else
					putc(*t, stdout);
			}
		}

		if (!sys_linebuffer.value)
		{
			if (coninput_len)
				printf("]%s", coninput_text);
			else
				putch(']');
		}
	}
	else
	{
		va_start (argptr,fmt);
		vprintf (fmt,argptr);
		va_end (argptr);
	}

	fflush(stdout);
}



#if 0
/*
================
Sys_Printf
================
*/
void Sys_Printf (char *fmt, ...)
{
	va_list		argptr;
	static char		text[2048];
	unsigned char		*p;

	va_start (argptr,fmt);
	vsnprintf (text,sizeof(text)-1, fmt,argptr);
	va_end (argptr);

	if (strlen(text) > sizeof(text))
		Sys_Error("memory overwrite in Sys_Printf");

    if (sys_nostdout.value)
        return;

	for (p = (unsigned char *)text; *p; p++) {
		*p &= 0x7f;
		if ((*p > 128 || *p < 32) && *p != 10 && *p != 13 && *p != 9)
			printf("[%02x]", *p);
		else
			putc(*p, stdout);
	}
	fflush(stdout);
}

#endif

/*
================
Sys_Quit
================
*/
void Sys_Quit (void)
{
	tcsetattr(STDIN_FILENO, TCSADRAIN, &orig);
	exit (0);		// appkit isn't running
}

static int do_stdin = 1;

#if 1
char *Sys_LineInputChar(char *line)
{
char c;
	while(*line)
{
c = *line++;
	if (c == '\r' || c == '\n')
	{
		coninput_text[coninput_len] = 0;
		putch ('\n');
		putch (']');
		coninput_len = 0;
		fflush(stdout);
		return coninput_text;
	}
	if (c == 8)
	{
		if (coninput_len)
		{
			putch (c);
			putch (' ');
			putch (c);
			coninput_len--;
			coninput_text[coninput_len] = 0;
		}
		continue;
	}
	if (c == '\t')
	{
		int i;
		char *s = Cmd_CompleteCommand(coninput_text, true, true, 0, NULL);
		if(s)
		{
			for (i = 0; i < coninput_len; i++)
				putch('\b');
			for (i = 0; i < coninput_len; i++)
				putch(' ');
			for (i = 0; i < coninput_len; i++)
				putch('\b');

			strcpy(coninput_text, s);
			coninput_len = strlen(coninput_text);
			printf("%s", coninput_text);
		}
		continue;
	}
	putch (c);
	coninput_text[coninput_len] = c;
	coninput_len++;
	coninput_text[coninput_len] = 0;
	if (coninput_len == sizeof(coninput_text))
		coninput_len = 0;
}
	fflush(stdout);
return NULL;
}
#endif
/*
================
Sys_ConsoleInput

Checks for a complete line of text typed in at the console, then forwards
it to the host command processor
================
*/
void Sys_Linebuffer_Callback (struct cvar_s *var, char *oldvalue)
{
	changes = orig;
	if (var->value)
	{
		changes.c_lflag |= (ICANON|ECHO);
	}
	else
	{
		changes.c_lflag &= ~(ICANON|ECHO);
		changes.c_cc[VTIME] = 0;
		changes.c_cc[VMIN] = 1;
	}
	tcsetattr(STDIN_FILENO, TCSADRAIN, &changes);
}

char *Sys_ConsoleInput (void)
{
	static char	text[256];
	int	len;

	if (!stdin_ready || !do_stdin)
		return NULL;		// the select didn't say it was ready
	stdin_ready = false;

	if (sys_linebuffer.value == 0)
	{
		text[0] = getc(stdin);
		text[1] = 0;
		len = 1;
		return Sys_LineInputChar(text);
	}
	else
	{
		len = read (0, text, sizeof(text)-1);
		if (len == 0) {
			// end of file
			do_stdin = 0;
			return NULL;
		}
		if (len < 1)
			return NULL;
		text[len-1] = 0;	// rip off the /n and terminate

		return text;
	}
}

/*
=============
Sys_Init

Quake calls this so the system can register variables before host_hunklevel
is marked
=============
*/
void Sys_Init (void)
{
	Cvar_Register (&sys_nostdout, "System configuration");
	Cvar_Register (&sys_extrasleep,	"System configuration");

	Cvar_Register (&sys_colorconsole, "System configuration");
	Cvar_Register (&sys_linebuffer, "System configuration");
}

void Sys_Shutdown (void)
{
}

#ifdef __linux__ /*should probably be GNUC but whatever*/
#include <execinfo.h>
static void Friendly_Crash_Handler(int sig)
{
	int fd;
	void *array[10];
	size_t size;

	// get void*'s for all entries on the stack
	size = backtrace(array, 10);

	// print out all the frames to stderr
	fprintf(stderr, "Error: signal %d:\n", sig);
	backtrace_symbols_fd(array, size, 2);

	fd = open("crash.log", O_WRONLY|O_CREAT|O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP);
	if (fd != -1)
	{
		write(fd, "Crash Log:\n", 11);
		size = backtrace(array, 10);
		backtrace_symbols_fd(array, size, fd);
		write(fd, "\n", 1);
		close(fd);
	}
	exit(1);
}
#endif

/*
=============
main
=============
*/
int main(int argc, char *argv[])
{
	int maxsleep;
	quakeparms_t	parms;
//	fd_set	fdset;
//	extern	int		net_socket;
	int j;

	signal(SIGPIPE, SIG_IGN);
	tcgetattr(STDIN_FILENO, &orig);
	changes = orig;

	memset (&parms, 0, sizeof(parms));

	COM_InitArgv (argc, (const char **)argv);
	TL_InitLanguages();
	parms.argc = com_argc;
	parms.argv = com_argv;

#ifdef __linux__
	if (COM_CheckParm("-dumpstack"))
	{
		signal(SIGILL, Friendly_Crash_Handler);
		signal(SIGFPE, Friendly_Crash_Handler);
		signal(SIGSEGV, Friendly_Crash_Handler);
		signal(SIGBUS, Friendly_Crash_Handler);
	}
#endif

	parms.memsize = 64*1024*1024;

	j = COM_CheckParm("-mem");
	if (j)
		parms.memsize = (int) (Q_atof(com_argv[j+1]) * 1024 * 1024);
	if ((parms.membase = malloc (parms.memsize)) == NULL)
		Sys_Error("Can't allocate %u\n", parms.memsize);

	parms.basedir = ".";

	SV_Init (&parms);

// run one frame immediately for first heartbeat
	maxsleep = SV_Frame()*1000;

//
// main loop
//
	while (1)
	{
		if (do_stdin)
			stdin_ready = NET_Sleep(maxsleep, true);
		else
		{
			NET_Sleep(maxsleep, false);
			stdin_ready = false;
		}

		maxsleep = SV_Frame()*1000;

	// extrasleep is just a way to generate a fucked up connection on purpose
		if (sys_extrasleep.value)
			usleep (sys_extrasleep.value);
	}
	return 0;
}






int Sys_EnumerateFiles (const char *gpath, const char *match, int (*func)(const char *, int, void *, void *), void *parm, void *spath)
{
	DIR *dir;
	char apath[MAX_OSPATH];
	char file[MAX_OSPATH];
	char truepath[MAX_OSPATH];
	char *s;
	struct dirent *ent;
	struct stat st;

//printf("path = %s\n", gpath);
//printf("match = %s\n", match);

	if (!gpath)
		gpath = "";
	*apath = '\0';

	Q_strncpyz(apath, match, sizeof(apath));
	for (s = apath+strlen(apath)-1; s >= apath; s--)
	{
		if (*s == '/')
		{
			s[1] = '\0';
			match += s - apath+1;
			break;
		}
	}
	if (s < apath)	//didn't find a '/'
		*apath = '\0';

	Q_snprintfz(truepath, sizeof(truepath), "%s/%s", gpath, apath);


//printf("truepath = %s\n", truepath);
//printf("gamepath = %s\n", gpath);
//printf("apppath = %s\n", apath);
//printf("match = %s\n", match);
	dir = opendir(truepath);
	if (!dir)
	{
		Con_DPrintf("Failed to open dir %s\n", truepath);
		return true;
	}
	do
	{
		ent = readdir(dir);
		if (!ent)
			break;
		if (*ent->d_name != '.')
		{
			if (wildcmp(match, ent->d_name))
			{
				Q_snprintfz(file, sizeof(file), "%s/%s", truepath, ent->d_name);

				if (stat(file, &st) == 0)
				{
					Q_snprintfz(file, sizeof(file), "%s%s%s", apath, ent->d_name, S_ISDIR(st.st_mode)?"/":"");

					if (!func(file, st.st_size, parm, spath))
					{
						closedir(dir);
						return false;
					}
				}
				else
					printf("Stat failed for \"%s\"\n", file);
			}
		}
	} while(1);
	closedir(dir);

	return true;
}



void Sys_CloseLibrary(dllhandle_t *lib)
{
	dlclose((void*)lib);
}
dllhandle_t *Sys_LoadLibrary(const char *name, dllfunction_t *funcs)
{
	int i;
	dllhandle_t lib;

	lib = dlopen (name, RTLD_LAZY);
	if (!lib)
		return NULL;

	if (funcs)
	{
		for (i = 0; funcs[i].name; i++)
		{
			*funcs[i].funcptr = dlsym(lib, funcs[i].name);
			if (!*funcs[i].funcptr)
				break;
		}
		if (funcs[i].name)
		{
			Sys_CloseLibrary((dllhandle_t*)lib);
			lib = NULL;
		}
	}

	return (dllhandle_t*)lib;
}
void *Sys_GetAddressForName(dllhandle_t *module, const char *exportname)
{
	if (!module)
		return NULL;
	return dlsym(module, exportname);
}


static void *game_library;

void Sys_UnloadGame(void)
{
	if (game_library)
	{
		dlclose(game_library);
		game_library = 0;
	}
}

void *Sys_GetGameAPI(void *parms)
{
	void *(*GetGameAPI)(void *);

	char name[MAX_OSPATH];
	char curpath[MAX_OSPATH];
	char *searchpath;
	const char *gamename = "gamei386.so";
	char *result;

	void *ret;

	result = getcwd(curpath, sizeof(curpath)); // do soemthing with the result

	searchpath = 0;
	while((searchpath = COM_NextPath(searchpath)))
	{
		snprintf (name, sizeof(name), "%s/%s/%s", curpath, searchpath, gamename);
		game_library = dlopen (name, RTLD_LAZY );
		if (game_library)
		{
			GetGameAPI = (void *)dlsym (game_library, "GetGameAPI");
			if (GetGameAPI && (ret = GetGameAPI(parms)))
			{
				return ret;
			}

			dlclose(game_library);
			game_library = 0;
		}
	}

	return 0;
}

void Sys_ServerActivity(void)
{
}

#ifdef MULTITHREAD
/* Thread creation calls */
typedef void *(*pfunction_t)(void *);

void *Sys_CreateThread(char *name, int (*func)(void *), void *args, int priority, int stacksize)
{
	pthread_t *thread;
	pthread_attr_t attr;

	thread = (pthread_t *)malloc(sizeof(pthread_t));
	if (!thread)
		return NULL;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	if (stacksize < PTHREAD_STACK_MIN)
		stacksize = PTHREAD_STACK_MIN;
	pthread_attr_setstacksize(&attr, stacksize);
	if (pthread_create(thread, &attr, (pfunction_t)func, args))
	{
		free(thread);
		thread = NULL;
	}
	pthread_attr_destroy(&attr);

	return (void *)thread;
}

void Sys_WaitOnThread(void *thread)
{
	pthread_join((pthread_t *)thread, NULL);
	free(thread);
}

/* Mutex calls */
void *Sys_CreateMutex(void)
{
	pthread_mutex_t *mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));

	if (mutex && !pthread_mutex_init(mutex, NULL))
		return mutex;
	return NULL;
}

qboolean Sys_TryLockMutex(void *mutex)
{
	return !pthread_mutex_trylock(mutex);
}

qboolean Sys_LockMutex(void *mutex)
{
	return !pthread_mutex_lock(mutex);
}

qboolean Sys_UnlockMutex(void *mutex)
{
	return !pthread_mutex_unlock(mutex);
}

void Sys_DestroyMutex(void *mutex)
{
	pthread_mutex_destroy(mutex);
	free(mutex);
}

/* Conditional wait calls */
typedef struct condvar_s
{
	pthread_mutex_t *mutex;
	pthread_cond_t *cond;
} condvar_t;

void *Sys_CreateConditional(void)
{
	condvar_t *condv;
	pthread_mutex_t *mutex;
	pthread_cond_t *cond;

	condv = (condvar_t *)malloc(sizeof(condvar_t));
	if (!condv)
		return NULL;

	mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	if (!mutex)
		return NULL;

	cond = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
	if (!cond)
		return NULL;

	if (!pthread_mutex_init(mutex, NULL))
	{
		if (!pthread_cond_init(cond, NULL))
		{
			condv->cond = cond;
			condv->mutex = mutex;

			return (void *)condv;
		}
		else
			pthread_mutex_destroy(mutex);
	}

	free(cond);
	free(mutex);
	free(condv);
	return NULL;
}

qboolean Sys_LockConditional(void *condv)
{
	return !pthread_mutex_lock(((condvar_t *)condv)->mutex);
}

qboolean Sys_UnlockConditional(void *condv)
{
	return !pthread_mutex_unlock(((condvar_t *)condv)->mutex);
}

qboolean Sys_ConditionWait(void *condv)
{
	return !pthread_cond_wait(((condvar_t *)condv)->cond, ((condvar_t *)condv)->mutex);
}

qboolean Sys_ConditionSignal(void *condv)
{
	return !pthread_cond_signal(((condvar_t *)condv)->cond);
}

qboolean Sys_ConditionBroadcast(void *condv)
{
	return !pthread_cond_broadcast(((condvar_t *)condv)->cond);
}

void Sys_DestroyConditional(void *condv)
{
	condvar_t *cv = (condvar_t *)condv;

	pthread_cond_destroy(cv->cond);
	pthread_mutex_destroy(cv->mutex);
	free(cv->cond);
	free(cv->mutex);
	free(cv);
}
#endif

void Sys_Sleep (double seconds)
{
	struct timespec ts;

	ts.tv_sec = (time_t)seconds;
	seconds -= ts.tv_sec;
	ts.tv_nsec = seconds * 1000000000.0;

	nanosleep(&ts, NULL);
}

qboolean Sys_RandomBytes(qbyte *string, int len)
{
	qboolean res;
	int fd = open("/dev/urandom", 0);
	res = (read(fd, string, len) == len);
	close(fd);

	return res;
}
