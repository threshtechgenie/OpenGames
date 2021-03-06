#include "quakedef.h"
#include "fs.h"
#include <windows.h>

#ifndef INVALID_SET_FILE_POINTER
#define INVALID_SET_FILE_POINTER ~0
#endif

//read-only memory mapped files.
//for write access, we use the stdio module as a fallback.

#define VFSW32_Open VFSOS_Open
#define w32filefuncs osfilefuncs

typedef struct {
	HANDLE changenotification;
	int hashdepth;
	char rootpath[1];
} vfsw32path_t;
typedef struct {
	vfsfile_t funcs;
	HANDLE hand;
	HANDLE mmh;
	void *mmap;
	unsigned int length;
	unsigned int offset;
} vfsw32file_t;
static int VFSW32_ReadBytes (struct vfsfile_s *file, void *buffer, int bytestoread)
{
	DWORD read;
	vfsw32file_t *intfile = (vfsw32file_t*)file;
	if (intfile->mmap)
	{
		if (intfile->offset+bytestoread > intfile->length)
			bytestoread = intfile->length-intfile->offset;

		memcpy(buffer, (char*)intfile->mmap + intfile->offset, bytestoread);
		intfile->offset += bytestoread;
		return bytestoread;
	}
	if (!ReadFile(intfile->hand, buffer, bytestoread, &read, NULL))
		return 0;
	return read;
}
static int VFSW32_WriteBytes (struct vfsfile_s *file, const void *buffer, int bytestoread)
{
	DWORD written;
	vfsw32file_t *intfile = (vfsw32file_t*)file;
	if (intfile->mmap)
	{
		if (intfile->offset+bytestoread > intfile->length)
			bytestoread = intfile->length-intfile->offset;

		memcpy((char*)intfile->mmap + intfile->offset, buffer, bytestoread);
		intfile->offset += bytestoread;
		return bytestoread;
	}

	if (!WriteFile(intfile->hand, buffer, bytestoread, &written, NULL))
		return 0;
	return written;
}
static qboolean VFSW32_Seek (struct vfsfile_s *file, unsigned long pos)
{
	unsigned long upper, lower;
	vfsw32file_t *intfile = (vfsw32file_t*)file;
	if (intfile->mmap)
	{
		intfile->offset = pos;
		return true;
	}

	lower = (pos & 0xffffffff);
	upper = ((pos>>16)>>16);

	return SetFilePointer(intfile->hand, lower, &upper, FILE_BEGIN) != INVALID_SET_FILE_POINTER;
}
static unsigned long VFSW32_Tell (struct vfsfile_s *file)
{
	vfsw32file_t *intfile = (vfsw32file_t*)file;
	if (intfile->mmap)
		return intfile->offset;
	return SetFilePointer(intfile->hand, 0, NULL, FILE_CURRENT);
}
static void VFSW32_Flush(struct vfsfile_s *file)
{
	vfsw32file_t *intfile = (vfsw32file_t*)file;
	if (intfile->mmap)
		FlushViewOfFile(intfile->mmap, intfile->length);
	FlushFileBuffers(intfile->hand);
}
static unsigned long VFSW32_GetSize (struct vfsfile_s *file)
{
	vfsw32file_t *intfile = (vfsw32file_t*)file;

	if (intfile->mmap)
		return intfile->length;
	return GetFileSize(intfile->hand, NULL);
}
static void VFSW32_Close(vfsfile_t *file)
{
	vfsw32file_t *intfile = (vfsw32file_t*)file;
	if (intfile->mmap)
	{
		UnmapViewOfFile(intfile->mmap);
		CloseHandle(intfile->mmh);
	}
	CloseHandle(intfile->hand);
	Z_Free(file);

	COM_FlushFSCache();
}

vfsfile_t *VFSW32_Open(const char *osname, const char *mode)
{
	HANDLE h, mh;
	unsigned int fsize;
	void *mmap;

	vfsw32file_t *file;
	qboolean read = !!strchr(mode, 'r');
	qboolean write = !!strchr(mode, 'w');
	qboolean append = !!strchr(mode, 'a');
	qboolean text = !!strchr(mode, 't');
	write |= append;
	if (strchr(mode, '+'))
		read = write = true;

	if ((write && read) || append)
		h = CreateFileA(osname, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	else if (write)
		h = CreateFileA(osname, GENERIC_READ|GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	else if (read)
		h = CreateFileA(osname, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	else
		h = INVALID_HANDLE_VALUE;
	if (h == INVALID_HANDLE_VALUE)
		return NULL;

	if (write || append || text)
	{
		fsize = 0;
		mh = INVALID_HANDLE_VALUE;
		mmap = NULL;

		/*if appending, set the access position to the end of the file*/
		if (append)
			SetFilePointer(h, 0, NULL, FILE_END);
	}
	else
	{
		fsize = GetFileSize(h, NULL);
		mh = CreateFileMapping(h, NULL, PAGE_READONLY, 0, 0, NULL);
		if (mh == INVALID_HANDLE_VALUE)
			mmap = NULL;
		else
		{
			mmap = MapViewOfFile(mh, FILE_MAP_READ, 0, 0, fsize);
			if (mmap == NULL)
			{
				CloseHandle(mh);
				mh = INVALID_HANDLE_VALUE;
			}
		}
	}

	file = Z_Malloc(sizeof(vfsw32file_t));
#ifdef _DEBUG
	Q_strncpyz(file->funcs.dbgname, osname, sizeof(file->funcs.dbgname));
#endif
	file->funcs.ReadBytes = read?VFSW32_ReadBytes:NULL;
	file->funcs.WriteBytes = (write||append)?VFSW32_WriteBytes:NULL;
	file->funcs.Seek = VFSW32_Seek;
	file->funcs.Tell = VFSW32_Tell;
	file->funcs.GetLen = VFSW32_GetSize;
	file->funcs.Close = VFSW32_Close;
	file->funcs.Flush = VFSW32_Flush;
	file->hand = h;
	file->mmh = mh;
	file->mmap = mmap;
	file->offset = 0;
	file->length = fsize;

	return (vfsfile_t*)file;
}

static vfsfile_t *VFSW32_OpenVFS(void *handle, flocation_t *loc, const char *mode)
{
	//path is already cleaned, as anything that gets a valid loc needs cleaning up first.

	return VFSW32_Open(loc->rawname, mode);
}

static void VFSW32_GetDisplayPath(void *handle, char *out, unsigned int outlen)
{
	vfsw32path_t *wp = handle;
	Q_strncpyz(out, wp->rootpath, outlen);
}
static void VFSW32_ClosePath(void *handle)
{
	vfsw32path_t *wp = handle;
	if (wp->changenotification != INVALID_HANDLE_VALUE)
		FindCloseChangeNotification(wp->changenotification);
	Z_Free(wp);
}
static qboolean VFSW32_PollChanges(void *handle)
{
	qboolean result = false;
	vfsw32path_t *wp = handle;

	if (wp->changenotification == INVALID_HANDLE_VALUE)
		return true;
	for(;;)
	{
		switch(WaitForSingleObject(wp->changenotification, 0))
		{
		case WAIT_OBJECT_0:
			result = true;
			break;
		case WAIT_TIMEOUT:
			return result;
		default:
			FindCloseChangeNotification(wp->changenotification);
			wp->changenotification = INVALID_HANDLE_VALUE;
			return true;
		}
		FindNextChangeNotification(wp->changenotification);
	}
	return result;
}
static void *VFSW32_OpenPath(vfsfile_t *mustbenull, const char *desc)
{
	vfsw32path_t *np;
	int dlen = strlen(desc);
	if (mustbenull)
		return NULL;
	np = Z_Malloc(sizeof(*np) + dlen);
	if (np)
	{
		memcpy(np->rootpath, desc, dlen+1);

		np->changenotification = FindFirstChangeNotification(np->rootpath, true, FILE_NOTIFY_CHANGE_FILE_NAME);
	}
	return np;
}
static int VFSW32_RebuildFSHash(const char *filename, int filesize, void *handle, void *spath)
{
	vfsw32path_t *wp = handle;
	if (filename[strlen(filename)-1] == '/')
	{	//this is actually a directory

		char childpath[256];
		Q_snprintfz(childpath, sizeof(childpath), "%s*", filename);
		Sys_EnumerateFiles(wp->rootpath, childpath, VFSW32_RebuildFSHash, wp, handle);
		return true;
	}

	FS_AddFileHash(wp->hashdepth, filename, NULL, wp);
	return true;
}
static void VFSW32_BuildHash(void *handle, int hashdepth)
{
	vfsw32path_t *wp = handle;
	wp->hashdepth = hashdepth;
	Sys_EnumerateFiles(wp->rootpath, "*", VFSW32_RebuildFSHash, handle, handle);
}
static qboolean VFSW32_FLocate(void *handle, flocation_t *loc, const char *filename, void *hashedresult)
{
	vfsw32path_t *wp = handle;
	FILE *f;
	int len;
	char netpath[MAX_OSPATH];


	if (hashedresult && (void *)hashedresult != wp)
		return false;

/*
	if (!static_registered)
	{	// if not a registered version, don't ever go beyond base
		if ( strchr (filename, '/') || strchr (filename,'\\'))
			continue;
	}
*/

// check a file in the directory tree
	snprintf (netpath, sizeof(netpath)-1, "%s/%s", wp->rootpath, filename);

	f = fopen(netpath, "rb");
	if (!f)
		return false;

	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fclose(f);
	if (loc)
	{
		loc->len = len;
		loc->offset = 0;
		loc->index = 0;
		snprintf(loc->rawname, sizeof(loc->rawname), "%s/%s", wp->rootpath, filename);
	}

	return true;
}
static void VFSW32_ReadFile(void *handle, flocation_t *loc, char *buffer)
{
//	vfsw32path_t *wp = handle;

	FILE *f;
	f = fopen(loc->rawname, "rb");
	if (!f)	//err...
		return;
	fseek(f, loc->offset, SEEK_SET);
	fread(buffer, 1, loc->len, f);
	fclose(f);
}
static int VFSW32_EnumerateFiles (void *handle, const char *match, int (*func)(const char *, int, void *, void *spath), void *parm)
{
	vfsw32path_t *wp = handle;
	return Sys_EnumerateFiles(wp->rootpath, match, func, parm, handle);
}


searchpathfuncs_t w32filefuncs = {
	VFSW32_GetDisplayPath,
	VFSW32_ClosePath,
	VFSW32_BuildHash,
	VFSW32_FLocate,
	VFSW32_ReadFile,
	VFSW32_EnumerateFiles,
	VFSW32_OpenPath,
	NULL,
	VFSW32_OpenVFS,
	VFSW32_PollChanges
};
