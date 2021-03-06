/*
Copyright (C) 1997-2001 Id Software, Inc.

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
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ipc.h>
#ifndef ANDROID
#include <sys/shm.h>
#endif
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>
#include <dlfcn.h>

#include "../qcommon/qcommon.h"

#include "../linux/rw_linux.h"

#ifdef ANDROID
#include <android/log.h>
#endif

cvar_t *nostdout;

unsigned	sys_frame_time;

uid_t saved_euid;
qboolean stdin_active = true;

// =======================================================================
// General routines
// =======================================================================

void Sys_ConsoleOutput (char *string)
{
	if (nostdout && nostdout->value)
		return;

#ifdef ANDROID
	__android_log_write(ANDROID_LOG_DEBUG, "libquake2.so", string);
#else
	fputs(string, stdout);
#endif
}

void Sys_Printf (char *fmt, ...)
{
#ifdef ANDROID
	va_list		argptr;

	va_start (argptr,fmt);
	__android_log_vprint(ANDROID_LOG_DEBUG, "libquake2.so", fmt, argptr);
	va_end (argptr);
#else
	va_list		argptr;
	char		text[1024];
	unsigned char	*p;

	va_start (argptr,fmt);
	vsnprintf (text,1024,fmt,argptr);
	va_end (argptr);

	if (nostdout && nostdout->value)
		return;

	for (p = (unsigned char *)text; *p; p++) {
		*p &= 0x7f;
		if ((*p > 128 || *p < 32) && *p != 10 && *p != 13 && *p != 9)
			printf("[%02x]", *p);
		else
			putc(*p, stdout);
	}
#endif
}

void Sys_Quit (void)
{
	CL_Shutdown ();
	Qcommon_Shutdown ();
#ifndef ANDROID
	fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);
#endif
	_exit(0);
}

void Sys_Init(void)
{
#if id386
	//	Sys_SetFPCW();
#endif
}

void Sys_Error (char *error, ...)
{ 
	va_list     argptr;
	char        string[1024];

#ifndef ANDROID
	// change stdin to non blocking
	fcntl (0, F_SETFL, fcntl (0, F_GETFL, 0) & ~FNDELAY);
#endif

	CL_Shutdown ();
	Qcommon_Shutdown ();

#ifdef ANDROID
	va_start (argptr,error);
	__android_log_vprint(ANDROID_LOG_ERROR, "libquake2.so", error, argptr);
	va_end (argptr);
#else
	va_start (argptr,error);
	vsnprintf (string,1024,error,argptr);
	va_end (argptr);
	fprintf(stderr, "Error: %s\n", string);
#endif

	_exit (1);

} 

void Sys_Warn (char *warning, ...)
{ 
#ifdef ANDROID
	va_list     argptr;

	va_start (argptr,warning);
	__android_log_vprint(ANDROID_LOG_WARN, "libquake2.so", warning, argptr);
	va_end (argptr);
#else
	va_list     argptr;
	char        string[1024];

	va_start (argptr,warning);
	vsnprintf (string,1024,warning,argptr);
	va_end (argptr);
	fprintf(stderr, "Warning: %s", string);
#endif
} 

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

void floating_point_exception_handler(int whatever)
{
	//	Sys_Warn("floating point exception\n");
	signal(SIGFPE, floating_point_exception_handler);
}

char *Sys_ConsoleInput(void)
{
	static char text[256];
	int     len;
	fd_set	fdset;
	struct timeval timeout;

	if (!dedicated || !dedicated->value)
		return NULL;

	if (!stdin_active)
		return NULL;

	FD_ZERO(&fdset);
	FD_SET(0, &fdset); // stdin
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	if (select (1, &fdset, NULL, NULL, &timeout) == -1 || !FD_ISSET(0, &fdset))
		return NULL;

	len = read (0, text, sizeof(text));
	if (len == 0) { // eof!
		stdin_active = false;
		return NULL;
	}

	if (len < 1)
		return NULL;
	text[len-1] = 0;    // rip off the /n and terminate

	return text;
}

/*****************************************************************************/

static void *game_library;

/*
=================
Sys_UnloadGame
=================
 */
void Sys_UnloadGame (void)
{
#ifndef ANDROID1
	if (game_library) 
		dlclose (game_library);
#endif
	game_library = NULL;
}

#include "quake_game_dll.h"
int quake2_game_dll;

char *savegamePath = "save";

extern char const * library_path;

/*
=================
Sys_GetGameAPI

Loads the game dll
=================
 */
void *Sys_GetGameAPI (void *parms)
{
#ifndef ANDROID1
	void	*(*GetGameAPI) (void *);
	FILE	*fp;
	char	name[MAX_OSPATH];
	char	*path;
	char	*str_p;

#if defined __i386__
	const char *gamename = "gamei386.so";
#elif defined __x86_64__
	const char *gamename = "gamex86_64.so";
#elif defined __alpha__
	const char *gamename = "gameaxp.so";
#elif defined __powerpc__
	const char *gamename = "gameppc.so";
#elif defined __sparc__
	const char *gamename = "gamesparc.so";
#else
	const char *gamename = "libquake2.so";
#endif

	setreuid(getuid(), getuid());
	setegid(getgid());

	if (game_library)
		Com_Error (ERR_FATAL, "Sys_GetGameAPI without Sys_UnloadingGame");

	Com_Printf("------- Loading %s -------\n", gamename);

	// now run through the search paths
	path = NULL;
	const char *lib_name = "libq2game.so";
	if (quake2_game_dll == Q2DLL_GAME)
	{
		LOGI("Loading libq2game.so");
		lib_name = "libq2game.so";
		savegamePath = "save";
	}
	else if (quake2_game_dll == Q2DLL_XATRIX)
	{
		LOGI("Loading libq2gamexatrix.so");
		lib_name = "libq2gamexatrix.so";
		savegamePath = "save_xatrix";
	}
	else if (quake2_game_dll == Q2DLL_ROGUE)
	{
		LOGI("Loading libq2gamerogue.so");
		lib_name = "libq2gamerogue.so";
		savegamePath = "save_rouge";
	}
	else if (quake2_game_dll == Q2DLL_CTF)
	{
		LOGI("Loading libq2gamectf.so");
		lib_name = "libq2gamectf.so";
		savegamePath = "save_ctf";
	}
	else if (quake2_game_dll == Q2DLL_CRBOT)
	{
		LOGI("Loading libq2gamecrbot.so");
		lib_name = "libq2gamecrbot.so";
		savegamePath = "save_crbot";
	}
	else
	{
		savegamePath = "save";
	}

	char lib_full_path[512];
	sprintf(lib_full_path,"%s/%s",library_path,lib_name);

	game_library = dlopen (lib_full_path, RTLD_NOW);

	if (game_library)
	{
		Com_MDPrintf ("LoadLibrary (%s)\n",name);

	}
	else
	{
		Com_Printf ("LoadLibrary (%s):", name);

		path = dlerror();
		str_p = strchr(path, ':'); // skip the path (already shown)
		if (str_p == NULL)
			str_p = path;
		else
			str_p++;

		Com_Printf ("%s\n", str_p);

		return NULL;
	}


	GetGameAPI = (void *)dlsym (game_library, "GetGameAPI");

	if (!GetGameAPI)
	{
		Sys_UnloadGame ();		
		return NULL;
	}
#else
	void	*(GetGameAPI) (void *);

	Com_Printf("------- Using hard-linked game library -------\n");
#endif

	return GetGameAPI (parms);
}

/*****************************************************************************/

void Sys_AppActivate (void)
{
}

void Sys_SendKeyEvents (void)
{
#ifndef DEDICATED_ONLY
	if (KBD_Update_fp)
		KBD_Update_fp();
#endif

	// grab frame time 
	sys_frame_time = Sys_Milliseconds();
}

/*****************************************************************************/

#ifndef ANDROID
int main (int argc, char **argv)
{
	int 	time, oldtime, newtime;

	// go back to real user for config loads
	saved_euid = geteuid();
	seteuid(getuid());

	printf ("Quake 2 -- Version %s\n", LINUX_VERSION);

	Qcommon_Init(argc, argv);

	fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) | FNDELAY);

	nostdout = Cvar_Get("nostdout", "0", 0);
	if (!nostdout->value) {
		fcntl(0, F_SETFL, fcntl (0, F_GETFL, 0) | FNDELAY);	
	}
	oldtime = Sys_Milliseconds ();
	while (1)
	{
		// find time spent rendering last frame
		do {
			newtime = Sys_Milliseconds ();
			time = newtime - oldtime;
		} while (time < 1);
		Qcommon_Frame (time);
		oldtime = newtime;
	}
}
#endif

#if 0
void Sys_CopyProtect(void)
{
	FILE *mnt;
	struct mntent *ent;
	char path[MAX_OSPATH];
	struct stat st;
	qboolean found_cd = false;

	static qboolean checked = false;

	if (checked)
		return;

	if ((mnt = setmntent("/etc/mtab", "r")) == NULL)
		Com_Error(ERR_FATAL, "Can't read mount table to determine mounted cd location.");

	while ((ent = getmntent(mnt)) != NULL) {
		if (strcmp(ent->mnt_type, "iso9660") == 0) {
			// found a cd file system
			found_cd = true;
			sprintf(path, "%s/%s", ent->mnt_dir, "install/data/quake2.exe");
			if (stat(path, &st) == 0) {
				// found it
				checked = true;
				endmntent(mnt);
				return;
			}
			sprintf(path, "%s/%s", ent->mnt_dir, "Install/Data/quake2.exe");
			if (stat(path, &st) == 0) {
				// found it
				checked = true;
				endmntent(mnt);
				return;
			}
			sprintf(path, "%s/%s", ent->mnt_dir, "quake2.exe");
			if (stat(path, &st) == 0) {
				// found it
				checked = true;
				endmntent(mnt);
				return;
			}
		}
	}
	endmntent(mnt);

	if (found_cd)
		Com_Error (ERR_FATAL, "Could not find a Quake2 CD in your CD drive.");
	Com_Error (ERR_FATAL, "Unable to find a mounted iso9660 file system.\n"
			"You must mount the Quake2 CD in a cdrom drive in order to play.");
}
#endif

#if 0
/*
================
Sys_MakeCodeWriteable
================
 */
void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{

	int r;
	unsigned long addr;
	int psize = getpagesize();

	addr = (startaddr & ~(psize-1)) - psize;

	//	fprintf(stderr, "writable code %lx(%lx)-%lx, length=%lx\n", startaddr,
	//			addr, startaddr+length, length);

	r = mprotect((char*)addr, length + startaddr - addr + psize, 7);

	if (r < 0)
		Sys_Error("Protection change failed\n");

}

#endif
