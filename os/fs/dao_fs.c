/*
// Dao Standard Modules
// http://www.daovm.net
//
// Copyright (c) 2011-2014, Limin Fu
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
// 
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
// SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
// OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* Contribution logs: */
/* 2011-02-13, Aleksey Danilov: added initial implementation. */

#include<string.h>
#include<errno.h>
#include<sys/stat.h>
#include<limits.h>

#include"dao.h"
#include"daoValue.h"

#ifdef WIN32

#include<io.h>
#include<fcntl.h>
#include<lmcons.h>
#include<aclapi.h>
#ifdef _MSC_VER
#define chdir _chdir
#define rmdir _rmdir
#define getcwd _getcwd
#define mkdir _mkdir
#define stat64 _stat64
#define chmod _chmod
#define mktemp _mktemp
#endif

DMutex fs_mtx;
#define FS_TRANS( st ) DMutex_Lock( &fs_mtx ); st; DMutex_Unlock( &fs_mtx )
#define FS_INIT() DMutex_Init( &fs_mtx )

#else
#include<dirent.h>

#define FS_TRANS( st ) st
#define FS_INIT()
#endif

#ifdef UNIX
#include<unistd.h>
#include<sys/time.h>
#include<pwd.h>
#endif

#ifndef MAX_PATH
#ifndef PATH_MAX
#define MAX_PATH 512
#else
#define MAX_PATH PATH_MAX
#endif
#endif

#define MAX_ERRMSG 100

struct DInode
{
	char *path;
	short type;
	time_t ctime;
	time_t atime;
	time_t mtime;
	mode_t mode;
	daoint size;
#ifndef WIN32
	uid_t uid;
#endif
};

typedef struct DInode DInode;

DaoType *daox_type_fsnode = NULL;

DInode* DInode_New()
{
	DInode *res = (DInode*)dao_malloc( sizeof(DInode) );
	res->path = NULL;
	res->type = -1;
	return res;
}

void DInode_Close( DInode *self )
{
	if( self->path ){
		dao_free( self->path );
		self->path = NULL;
		self->type = -1;
	}
}

void DInode_Delete( DInode *self )
{
	DInode_Close( self );
	dao_free( self );
}

#ifdef WIN32
#define IS_PATH_SEP( c ) ( ( c ) == '\\' || ( c ) == '/' || ( c ) == ':' )
#else
#define IS_PATH_SEP( c ) ( ( c ) == '/' )
#endif

int NormalizePath( const char *path, char *dest )
{
#ifdef WIN32
	int res;
	FS_TRANS( res = GetFullPathName( path, MAX_PATH + 1, dest, NULL ) != 0 );
	if ( res ){
		int i;
		for ( i = 0; dest[i] != '\0'; i++ )
			if ( dest[i] == '\\' )
				dest[i] = '/';
	}
	return res;
#else
	return realpath( path, dest ) != NULL;
#endif
}

int DInode_Open( DInode *self, const char *fpath )
{
	char buf[MAX_PATH + 1] = {0};
	char path[MAX_PATH + 1];
	struct stat64 info;
	int len;
	if ( !NormalizePath( fpath, path ) )
		return -2;
	DInode_Close( self );
	len = strlen( path );
	if( stat64( path, &info ) != 0 )
		return errno;
#ifdef WIN32
	if( !( info.st_mode & _S_IFDIR ) && !( info.st_mode & _S_IFREG ) )
		return -1;
	self->type = ( info.st_mode & _S_IFDIR )? 0 : 1;
	self->size = ( info.st_mode & _S_IFDIR )? 0 : info.st_size;
	if( len < 2 || ( path[1] != ':' && path[0] != '/' ) ){
		if( !getcwd( buf, sizeof(buf) ) )
			return errno;
		strcat( buf, "/" );
	}
#else
	if( !S_ISDIR( info.st_mode ) && !S_ISREG( info.st_mode ) )
		return -1;
	self->type = ( S_ISDIR( info.st_mode ) )? 0 : 1;
	self->size = ( S_ISDIR( info.st_mode ) )? 0 : info.st_size;
	self->uid = info.st_uid;
	if( path[0] != '/' ){
		if( !getcwd( buf, sizeof(buf) ) )
			return errno;
		strcat( buf, "/" );
	}
#endif
	len += strlen( buf );
	if( len > MAX_PATH )
		return ENAMETOOLONG;
	self->path = (char*)dao_malloc( len + 1 );
	strcpy( self->path, buf );
	strcat( self->path, path );
#ifndef WIN32
	if( self->path[len - 1] == '/' && len > 1 )
		self->path[len - 1] = '\0';
#endif
	self->ctime = info.st_ctime;
	self->mtime = info.st_mtime;
	self->atime = info.st_atime;
	self->mode = info.st_mode;
	return 0;
}

int DInode_Reopen( DInode *self )
{
	struct stat64 info;
	if( stat64( self->path, &info ) != 0 )
		return errno;
#ifdef WIN32
	if( !( info.st_mode & _S_IFDIR ) && !( info.st_mode & _S_IFREG ) )
		return -1;
	self->type = ( info.st_mode & _S_IFDIR )? 0 : 1;
	self->size = ( info.st_mode & _S_IFDIR )? 0 : info.st_size;
#else
	if( !S_ISDIR( info.st_mode ) && !S_ISREG( info.st_mode ) )
		return -1;
	self->type = ( S_ISDIR( info.st_mode ) )? 0 : 1;
	self->size = ( S_ISDIR( info.st_mode ) )? 0 : info.st_size;
	self->uid = info.st_uid;
#endif
	self->ctime = info.st_ctime;
	self->mtime = info.st_mtime;
	self->atime = info.st_atime;
	self->mode = info.st_mode;
	return 0;
}

char* DInode_Parent( DInode *self, char *buffer )
{
	int i;
	if( !self->path )
		return NULL;
	for (i = strlen( self->path ) - 1; i >= 0; i--)
		if( IS_PATH_SEP( self->path[i] ) )
			break;
	if( self->path[i + 1] == '\0' )
		return NULL;
#ifdef WIN32
	if( self->path[i] == ':' ){
		strncpy( buffer, self->path, i + 1 );
		buffer[i + 1] = '/';
		buffer[i + 2] = '\0';
	}
	else{
		if( i == 2 )
			i++;
		strncpy( buffer, self->path, i );
		if( self->path[0] == '/' ){
			int j;
			int k = 0;
			for (j = 2; j < i && k < 2; j++)
				if( IS_PATH_SEP( self->path[j] ) )
					k++;
			if( i == j ){
				buffer[i] = '/';
				i++;
			}
		}
		buffer[i] = '\0';
	}
#else
	if( i == 0 )
		strcpy( buffer, "/" );
	else{
		strncpy( buffer, self->path, i );
		buffer[i] = '\0';
	}
#endif
	return buffer;
}

int DInode_Rename( DInode *self, const char *fpath )
{
	char buf[MAX_PATH + 1] = {0};
	char path[MAX_PATH + 1];
	int len, noname = 0;
	if( !self->path )
		return -1;
	if ( !NormalizePath( fpath, path ) )
		return -2;
	len = strlen( path );
#ifdef WIN32
	if ( len > 1 && IS_PATH_SEP( path[len - 1] ) && path[len - 2] != ':' ){
#else
	if ( len > 1 && IS_PATH_SEP( path[len - 1] ) ){
#endif
		path[len - 1] = '\0';
		noname = 1;
	}
	if( !DInode_Parent( self, buf ) )
		return 1;
#ifdef WIN32
	if( len < 2 || ( path[1] != ':' && path[0] != '/' ) ){
#else
	if( path[0] != '/' ){
#endif
		strcat( buf, "/" );
		len += strlen( buf );
		if( len > MAX_PATH )
			return ENAMETOOLONG;
		strcat( buf, path );
	}else{
		if( len > MAX_PATH )
			return ENAMETOOLONG;
		strcpy( buf, path );
	}
	if ( noname ){
		int i;
		for (i = strlen( self->path ) - 1; i > 0; i--)
			if( IS_PATH_SEP( self->path[i] ) )
				break;
		strcat( buf, "/" );
		strcat( buf, self->path + i );
	}
	if( rename( self->path, buf ) != 0 )
		return errno;
	self->path = (char*)dao_realloc( self->path, len + 1 );
	strcpy( self->path, buf );
	return 0;
}

int DInode_Remove( DInode *self )
{
	if( !self->path )
		return -1;
	if( self->type == 0 ){
		if( rmdir( self->path ) != 0 )
			return errno;
	}else{
		if( unlink( self->path ) != 0 )
			return errno;
	}
	return 0;
}

int DInode_SubInode( DInode *self, const char *fpath, int dir, DInode *dest, int test )
{
	int len;
	char buf[MAX_PATH + 1], path[MAX_PATH + 1];
	FILE *handle;
	struct stat64 info;
	if( !self->path || self->type != 0 )
		return -1;
	if( DInode_Parent( self, path ) ){
		strcpy( path, self->path );
		strcat( path, "/" );
	}
	else
		strcpy( path, self->path );
	len = strlen( path ) + strlen( fpath );
	if( len > MAX_PATH )
		return ENAMETOOLONG;
	strcat( path, fpath );
#ifdef WIN32
	if ( len > 1 && IS_PATH_SEP( path[len - 1] ) && path[len - 2] != ':' )
#else
	if ( len > 1 && IS_PATH_SEP( path[len - 1] ) )
#endif
		path[len - 1] = '\0';
	if ( !NormalizePath( path, buf ) )
		return -2;
#ifdef WIN32
	if( stat64( buf, &info ) == 0 && ( test || ( ( dir && ( info.st_mode & _S_IFDIR ) )
		|| ( !dir && ( info.st_mode & _S_IFREG ) ) ) ) )
		return DInode_Open( dest, buf );
#else
	if( stat64( buf, &info ) == 0 && ( test || ( ( dir && S_ISDIR( info.st_mode ) )
		|| ( !dir && S_ISREG( info.st_mode ) ) ) ) )
		return DInode_Open( dest, buf );
#endif
	if ( test )
		return ENOENT;
	if( !dir ){
		if( !( handle = fopen( buf, "w" ) ) )
			return ( errno == EINVAL ) ? -1 : errno;
		fclose( handle );
	}else{
#ifdef WIN32
	if( mkdir( buf ) != 0 )
		return ( errno == EINVAL ) ? -1 : errno;
#else
	if( mkdir( buf, S_IRWXU|S_IRGRP|S_IXGRP|S_IXOTH ) != 0 )
		return ( errno == EINVAL ) ? -1 : errno;
#endif
	}
	return DInode_Open( dest, buf );
}

extern DaoTypeBase fsnodeTyper;

int DInode_ChildrenRegex( DInode *self, int type, DaoProcess *proc, DaoList *dest, DaoRegex *pattern )
{
	char buffer[MAX_PATH + 1];
	int len, res;
#ifdef WIN32
	intptr_t handle;
	struct _finddata_t finfo;
#else
	DIR *handle;
	struct dirent *finfo;
#endif
	if( !self->path || self->type != 0 )
		return -1;
    strcpy( buffer, self->path );
	len = strlen( buffer );
#ifdef WIN32
	/* Using _findfirst/_findnext for Windows */
	if( IS_PATH_SEP( buffer[len - 1] ) )
    	strcpy( buffer + len, "*" );
    else
		strcpy( buffer + len++, "/*" );
	handle = _findfirst( buffer, &finfo );
	if (handle != -1){
		DString *str = DString_New( 1 );
		DaoValue *value;
		DInode *fsnode;
		do
			if( strcmp( finfo.name, "." ) && strcmp( finfo.name, ".." ) ){
				DString_SetDataMBS( str, finfo.name, strlen(finfo.name) );
				strcpy( buffer + len, finfo.name );
				fsnode = DInode_New();
				if( ( res = DInode_Open( fsnode, buffer ) ) != 0 ){
					DInode_Delete( fsnode );
					return res;
				}
				if( ( fsnode->type == type || type == 2 ) && DaoRegex_Match( pattern, str, NULL, NULL ) ){
					value = (DaoValue*) DaoProcess_NewCdata( proc, daox_type_fsnode, fsnode, 1 );
					DaoList_PushBack( dest, value );
				}
				else
					DInode_Delete( fsnode );
			}
		while( !_findnext( handle, &finfo ) );
		DString_Delete( str );
		_findclose( handle );
	}
#else
	/* Using POSIX opendir/readdir otherwise */
	handle = opendir( buffer );
	if( !IS_PATH_SEP( buffer[len - 1] ) )
		strcpy( buffer + len++,  "/");
	if( handle ){
		DString *str = DString_New( 1 );
		DaoValue *value;
		DInode *fsnode;
		while( ( finfo = readdir( handle ) ) )
			if( strcmp( finfo->d_name, "." ) && strcmp( finfo->d_name, ".." ) ){
				DString_SetDataMBS( str, finfo->d_name, strlen(finfo->d_name) );
				strcpy( buffer + len, finfo->d_name );
				fsnode = DInode_New();
				if( ( res = DInode_Open( fsnode, buffer ) ) != 0 ){
					DInode_Delete( fsnode );
					return res;
				}
				if( ( fsnode->type == type || type == 2 ) && DaoRegex_Match( pattern, str, NULL, NULL ) ){
					value = (DaoValue*) DaoProcess_NewCdata( proc, daox_type_fsnode, fsnode, 1 );
					DaoList_PushBack( dest, value );
				}
				else
					DInode_Delete( fsnode );
			}
		DString_Delete( str );
		closedir( handle );
	}
#endif
	else
		return errno;
	return 0;
}

int DInode_SetAccess( DInode *self, int ur, int uw, int ux, int gr, int gw, int gx, int otr, int otw, int otx )
{
#ifdef WIN32
	if ( chmod( self->path, (ur? _S_IREAD : 0) | (uw? _S_IWRITE : 0) ) )
#else
	if ( chmod( self->path, (ur? S_IRUSR : 0) | (uw? S_IWUSR : 0) | (ux? S_IXUSR : 0) |
				(gr? S_IRGRP : 0) | (gw? S_IWGRP : 0) | (gx? S_IXGRP : 0) |
				(otr? S_IROTH : 0) | (otw? S_IWOTH : 0) | (otx? S_IXOTH : 0) ) )
#endif
		return errno;
	return 0;
}

void DInode_GetMode( DInode *self, DaoTuple *res )
{
#ifdef WIN32
	if ( self->mode & _S_IREAD )
		DString_AppendMBS( res->items[0]->xString.data, "r" );
	if ( self->mode & _S_IWRITE )
		DString_AppendMBS( res->items[0]->xString.data, "w" );
	if ( self->mode & _S_IEXEC )
		DString_AppendMBS( res->items[0]->xString.data, "x" );
#else
	if ( self->mode & S_IRUSR )
		DString_AppendMBS( res->items[0]->xString.data, "r" );
	if ( self->mode & S_IWUSR )
		DString_AppendMBS( res->items[0]->xString.data, "w" );
	if ( self->mode & S_IXUSR )
		DString_AppendMBS( res->items[0]->xString.data, "x" );

	if ( self->mode & S_IRGRP )
		DString_AppendMBS( res->items[1]->xString.data, "r" );
	if ( self->mode & S_IWGRP )
		DString_AppendMBS( res->items[1]->xString.data, "w" );
	if ( self->mode & S_IXGRP )
		DString_AppendMBS( res->items[1]->xString.data, "x" );

	if ( self->mode & S_IROTH )
		DString_AppendMBS( res->items[2]->xString.data, "r" );
	if ( self->mode & S_IWOTH )
		DString_AppendMBS( res->items[2]->xString.data, "w" );
	if ( self->mode & S_IXOTH )
		DString_AppendMBS( res->items[2]->xString.data, "x" );
#endif
}

int DInode_GetOwner( DInode *self, DString *name )
{
#ifdef WIN32
	PSID sid = NULL;
	SID_NAME_USE sidname;
	PSECURITY_DESCRIPTOR sd = NULL;
	char buf[UNLEN], dbuf[UNLEN];
	DWORD bufsize = sizeof(buf), sdsize = sizeof(dbuf);
	if ( GetNamedSecurityInfo( self->path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &sid, NULL, NULL, NULL, &sd ) != ERROR_SUCCESS )
		return 1;
	if ( !LookupAccountSid( NULL, sid, buf, &bufsize, dbuf, &sdsize, &sidname ) ){
		LocalFree( sd );
		return 1;
	}
	DString_SetMBS( name, buf );
	LocalFree( sd );
	return 0;
#else
	struct passwd pwd, *res;
	char buf[4096];
	if ( getpwuid_r( self->uid, &pwd, buf, sizeof(buf), &res ) )
		return 1;
	DString_SetMBS( name, pwd.pw_name );
	return 0;
#endif
}

int DInode_Resize( DInode *self, daoint size )
{
	int res;
#ifdef WIN32
	int fd = _open( self->path, _O_RDWR );
	if ( fd == -1 )
		return errno;
	res = _chsize_s( fd, size );
	_close( fd );
#else
	res = truncate64( self->path, size )? errno : 0;
#endif
	if ( !res )
		self->size = size;
	return res? errno : 0;
}

int GetRootDirs( DaoList *dest )
{
#ifdef WIN32
	char buf[MAX_PATH + 1];
	char *pos, *start;
	if ( !GetLogicalDriveStrings( sizeof(buf), buf ) )
		return 0;
	start = buf;
	while ( *start != '\0' ){
		pos = strchr( start, '\0' );
		DaoList_PushBack( dest, (DaoValue*)DaoString_NewMBS( start ) );
		start = pos + 1;
	}
#else
	DaoList_PushBack( dest, (DaoValue*)DaoString_NewMBS( "/" ) );
#endif
	return 1;
}

static void GetErrorMessage( char *buffer, int code, int special )
{
	switch ( code ){
	case -2:
		strcpy( buffer, "Invalid path" );
		break;
	case EACCES:
	case EBADF:
		strcpy( buffer, "Access not permitted (EACCES/EBADF)");
		break;
	case EBUSY:
		strcpy (buffer, "Path is being used (EBUSY)" );
		break;
	case ENOTEMPTY:
	case EEXIST:
		strcpy( buffer, special? "Directory is not empty (ENOTEMPTY/EEXIST)" : "File object already exists (EEXIST/ENOTEMPTY)" );
		break;
	case EPERM:
	case ENOTDIR:
	case EISDIR:
		strcat( buffer, "Inconsistent type of file object (EPERM/ENOTDIR/EISDIR)" );
		break;
	case EINVAL:
		strcpy( buffer, special? "Path does not exist (EINVAL)" : "Making a directory its own subdirectory (EINVAL)" );
		break;
	case EMLINK:
		strcat( buffer, "Too many entries in parent directory (EMLINK)" );
		break;
	case ENOENT:
		strcpy( buffer, "Path does not exist (ENOENT)" );
		break;
	case ENOSPC:
		strcpy( buffer, "Not enough free space in the file system (ENOSPC)" );
		break;
	case EROFS:
		strcpy( buffer, "Writing to a read-only file system (EROFS)" );
		break;
	case EXDEV:
		strcpy( buffer, "Relocating file object to a different file system (EXDEV)" );
		break;
	case ENAMETOOLONG:
		strcpy( buffer, "Path is too long (ENAMETOOLONG)" );
		break;
	case EMFILE:
	case ENFILE:
		strcpy( buffer, "Too many files open (EMFILE/ENFILE)" );
		break;
	case ENOMEM:
		strcpy( buffer, "Not enough memory (ENOMEM)" );
		break;
	case EFBIG:
		strcpy( buffer, "File size is too large (EFBIG)" );
		break;
	case EIO:
		strcpy( buffer, "Hardware I/O error (EIO)" );
		break;
	case EINTR:
		strcpy( buffer, "Operation interrupted by a signal (EINTR)" );
		break;
	default:
		sprintf( buffer, "Unknown system error (%x)", code );
	}
}

static void FSNode_Update( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	char errbuf[MAX_ERRMSG + MAX_PATH + 3];
	int res;
	if( ( res = DInode_Reopen( self ) ) != 0 ){
		if( res == -1 )
			strcpy( errbuf, "File object is not a file or directory" );
		else
			GetErrorMessage( errbuf, res, 0 );
		if( res == -1 || res == ENOENT )
			snprintf( errbuf + strlen( errbuf ), MAX_PATH + 3, ": %s", self->path );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
	}
}

static void FSNode_Path( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	DaoProcess_PutMBString( proc, self->path );
}

static void FSNode_Name( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	int i;
	for (i = strlen( self->path ) - 1; i >= 0; i--)
		if( IS_PATH_SEP( self->path[i] ) )
			break;
	if( self->path[i + 1] == '\0' )
		DaoProcess_PutMBString( proc, self->path );
	else
		DaoProcess_PutMBString( proc, self->path + i + 1 );
}

static void FSNode_BaseName( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	int i;
	char *name, *pos;
	for (i = strlen( self->path ) - 1; i >= 0; i--)
		if( IS_PATH_SEP( self->path[i] ) )
			break;
	name = self->path;
	if( self->path[i + 1] != '\0' )
		name += i + 1;
	pos = strchr( name, '.' );
	if ( pos )
		DaoProcess_PutBytes( proc, name, pos - name );
	else
		DaoProcess_PutMBString( proc, name );
}

static void FSNode_Parent( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	DInode *par;
	char path[MAX_PATH + 1];
	int res = 0;
	par = DInode_New();
	if( !DInode_Parent( self, path ) || ( res = DInode_Open( par, path ) ) != 0 ){
		DInode_Delete( par );
		if( res == 0 )
			strcpy( path, "Obtaining parent directory of root directory" );
		else
			GetErrorMessage( path, res, 0 );
		DaoProcess_RaiseException( proc, DAO_ERROR, path );
		return;
	}
	DaoProcess_PutCdata( proc, (void*)par, daox_type_fsnode );
}

static void FSNode_Type( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	DaoProcess_PutEnum( proc, self->type == 1 ? "file" : "dir" );
}

static void FSNode_Size( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	DaoProcess_PutInteger( proc, self->size );
}

static void FSNode_Resize( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	daoint size = p[1]->xInteger.value;
	int res;
	if ( self->type == 0 ){
		DaoProcess_RaiseException( proc, DAO_ERROR, "Resizing a directory" );
	}
	if ( ( res = DInode_Resize( self, size ) ) != 0 ){
		char errbuf[MAX_ERRMSG];
		if( res == -1 )
			strcpy( errbuf, "Failed to resize file" );
		else
			GetErrorMessage( errbuf, res, 0 );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
	}
}

static void FSNode_Rename( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	int res;
	char errbuf[MAX_ERRMSG];
	DString *path = DString_Copy( p[1]->xString.data );
	DString_ToMBS( path );
	if ( (res = DInode_Rename( self, path->mbs ) ) != 0 ){
		if( res == -1 )
			strcpy( errbuf, "Renaming root directory" );
		else
			GetErrorMessage( errbuf, res, 0 );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
	}
	DString_Delete( path );
}

static void FSNode_Remove( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	int res;
	char errbuf[MAX_ERRMSG];
	if ( (res = DInode_Remove( self ) ) != 0 ){
		GetErrorMessage( errbuf, res, self->type == 0 );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
	}
}

static void FSNode_Time( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	DaoTuple *res = DaoProcess_PutTuple( proc, 3 );
	res->items[0]->xInteger.value = self->ctime;
	res->items[1]->xInteger.value = self->mtime;
	res->items[2]->xInteger.value = self->atime;
}

static void FSNode_Access( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	DaoTuple *res = DaoProcess_PutTuple( proc, 3 );
	DInode_GetMode( self, res );
}

int InitPermissions( DString *mode, int *r, int *w, int *x )
{
	daoint i;
	DString_ToMBS( mode );
	*r = *w = *x = 0;
	for ( i = 0; i < mode->size; i++ )
		switch ( mode->mbs[i] ){
		case 'r': if ( *r ) return 0; *r = 1; break;
		case 'w': if ( *w ) return 0; *w = 1; break;
		case 'x': if ( *x ) return 0; *x = 1; break;
		default: return 0;
		}
	return 1;
}

static void FSNode_SetAccess(DaoProcess *proc, DaoValue *p[], int N)
{
	DInode *self = (DInode*)DaoValue_TryGetCdata(p[0]);
	char errbuf[MAX_ERRMSG];
	int ur, uw, ux, gr, gw, gx, otr, otw, otx;
	DString *mode = DString_Copy( p[1]->xString.data );
	if ( !InitPermissions( mode, &ur, &uw, &ux ) )
		goto Error;
	DString_Assign( mode, p[2]->xString.data );
	if ( !InitPermissions( mode, &gr, &gw, &gx ) )
		goto Error;
	DString_Assign( mode, p[3]->xString.data );
	if ( !InitPermissions( mode, &otr, &otw, &otx ) )
		goto Error;
	int res = DInode_SetAccess(self, ur, uw, ux, gr, gw, gx, otr, otw, otx);
	if (res){
		GetErrorMessage(errbuf, res, 0);
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
	}
	else
		FSNode_Update(proc, p, N);
	DString_Delete( mode );
	return;
Error:
	DaoProcess_RaiseException( proc, DAO_ERROR, "Invalid access mode format" );
	DString_Delete( mode );
}

static void FSNode_Makefile( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	DInode *child;
	DString *path = DString_Copy( p[1]->xString.data );
	DString_ToMBS( path );
	int res;
	if( self->type != 0 ){
		DaoProcess_RaiseException( proc, DAO_ERROR, "File object is not a directory" );
		DString_Delete( path );
		return;
	}
	child = DInode_New();
	if( ( res = DInode_SubInode( self, path->mbs, 0, child, 0 ) ) != 0 ){
		char errbuf[MAX_ERRMSG];
		DInode_Delete( child );
		if( res == -1 )
			strcpy( errbuf, "Invalid file name (EINVAL)" );
		else
			GetErrorMessage( errbuf, res, 0 );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
	}
	else
		DaoProcess_PutCdata( proc, (void*)child, daox_type_fsnode );
	DString_Delete( path );
}

static void FSNode_Makedir( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	DInode *child;
	DString *path = DString_Copy( p[1]->xString.data );
	int res;
	DString_ToMBS( path );
	if( self->type != 0 ){
		DaoProcess_RaiseException( proc, DAO_ERROR, "File object is not a directory" );
		DString_Delete( path );
		return;
	}
	child = DInode_New();
	if( ( res = DInode_SubInode( self, path->mbs, 1, child, 0 ) ) != 0 ){
		char errbuf[MAX_ERRMSG];
		DInode_Delete( child );
		if( res == -1 )
			strcpy( errbuf, "Invalid directory name (EINVAL)" );
		else
			GetErrorMessage( errbuf, res, 0 );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
	}
	else
		DaoProcess_PutCdata( proc, (void*)child, daox_type_fsnode );
	DString_Delete( path );
}

static void FSNode_Contains( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	DInode *child;
	DString *path = DString_Copy( p[1]->xString.data );
	DString_ToMBS( path );
	if( self->type != 0 ){
		DaoProcess_RaiseException( proc, DAO_ERROR, "File object is not a directory" );
		DString_Delete( path );
		return;
	}
	child = DInode_New();
	DaoProcess_PutInteger( proc, DInode_SubInode( self, path->mbs, 0, child, 1 ) == 0 );
	DString_Delete( path );
	DInode_Delete( child );
}

static void FSNode_Child( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	DInode *child;
	DString *path = DString_Copy( p[1]->xString.data );
	char buf[MAX_PATH + 1];
	int res;
	DString_ToMBS( path );
	if( self->type != 0 ){
		DaoProcess_RaiseException( proc, DAO_ERROR, "File object is not a directory" );
		goto Exit;
	}
	child = DInode_New();
	strcpy( buf, self->path );
	strcat( buf, "/" );
	if( strlen( buf ) + path->size > MAX_PATH ){
		char errbuf[MAX_ERRMSG];
		GetErrorMessage( errbuf, ENAMETOOLONG, 0 );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
		DInode_Delete( child );
		goto Exit;
	}
	if( ( res = DInode_Open( child, path->mbs ) ) != 0 ){
		char errbuf[MAX_ERRMSG];
		DInode_Delete( child );
		if( res == -1 )
			strcpy( errbuf, "File object is not a directory" );
		else
			GetErrorMessage( errbuf, res, 0 );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
		return;                                  
	}
	DaoProcess_PutCdata( proc, (void*)child, daox_type_fsnode );
Exit:
	DString_Delete( path );
}

static void DInode_Children( DInode *self, DaoProcess *proc, int type, DString *pat, int ft )
{
	DaoList *list = DaoProcess_PutList( proc );
	char buffer[MAX_PATH + 1], *filter;
	int res, i, j, len;
	DString *strbuf;
	DaoRegex *pattern;
	if( self->type != 0 ){
		DaoProcess_RaiseException( proc, DAO_ERROR, "File object is not a directory" );
		return;
	}
	filter = DString_GetMBS( pat );
	len = strlen( filter );
	if( len > MAX_PATH ){
		DaoProcess_RaiseException( proc, DAO_ERROR, "Filter is too large" );
		return;
	}
	if( ft == 0 ){
		buffer[0] = '^';
		buffer[1] = '(';
		for( i = 0, j = 2; i < len && j < MAX_PATH - 1; i++, j++ )
			switch( filter[i] )
			{
			case '?':
				buffer[j] = '.';
				break;
			case '*':
				buffer[j++] = '.';
				buffer[j] = '*';
				break;
			case ';':
				buffer[j] = '|';
				break;
			case '{':
			case '}':
			case '(':
			case ')':
			case '%':
			case '.':
			case '|':
			case '$':
			case '^':
			case '[':
			case ']':
			case '+':
			case '-':
			case '<':
			case '>':
				buffer[j++] = '%';
				buffer[j] = filter[i];
				break;
			default:
				buffer[j] = filter[i];
			}
		if( j >= MAX_PATH - 1 ){
			DaoProcess_RaiseException( proc, DAO_ERROR, "Filter is too large" );
			return;
		}
		buffer[j] = ')';
		buffer[j + 1] = '$';
		buffer[j + 2] = '\0';
	}
	else
		strcpy( buffer, filter );
	strbuf = DString_New( 1 );
	DString_SetMBS( strbuf, buffer );
	pattern = DaoProcess_MakeRegex( proc, strbuf, 1 );
    DString_Delete( strbuf );
    if( !pattern )
    	return;
	type = ( type == 3 ) ? 2 : ( ( type == 1 ) ? 1 : 0 );
	if( ( res = DInode_ChildrenRegex( self, type, proc, list, pattern ) ) != 0 ){
		char errbuf[MAX_ERRMSG];
		GetErrorMessage( errbuf, res, 1 );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
		return;
	}
}

static void FSNode_Children( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	DString *pt = DString_Copy( p[2]->xString.data );
	DInode_Children( self, proc, DaoValue_TryGetEnum( p[1] ), pt, DaoValue_TryGetEnum( p[3] ) );
	DString_Delete( pt );
}

static void FSNode_Files( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	DString *pt = DString_Copy( p[1]->xString.data );
	DInode_Children( self, proc, 1, pt, DaoValue_TryGetEnum( p[2] ) );
}

static void FSNode_Dirs( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	DString *pt = DString_Copy( p[1]->xString.data );
	DInode_Children( self, proc, 2, pt, DaoValue_TryGetEnum( p[2] ) );
	DString_Delete( pt );
}

static void FSNode_Suffix( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	char *pos = strrchr( self->path, '.' );
	DaoProcess_PutMBString( proc, pos && !strchr( pos + 1, '/' )? pos + 1 : "" );
}

static void FSNode_Copy( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	DString *path = DString_Copy( p[1]->xString.data );
	FILE *src = NULL, *dest = NULL;
	DInode *copy;
	int res;
	char buf[4096];
	DString_ToMBS( path );
	if ( self->type == 0 ){
		DaoProcess_RaiseException( proc, DAO_ERROR, "Copying of directories is not supported" );
		goto Exit;
	}
	src = fopen( self->path, "r" );
	if ( !src ){
		DaoProcess_RaiseException( proc, DAO_ERROR, "Unable to read file" );
		goto Exit;
	}
	if ( IS_PATH_SEP( path->mbs[path->size - 1] ) ){
		int i;
		for (i = strlen( self->path ) - 1; i > 0; i--)
			if( IS_PATH_SEP( self->path[i] ) )
				break;
		DString_AppendMBS( path, self->path + i );
	}
	dest = fopen( path->mbs, "w" );
	if ( !dest ){
		char errbuf[MAX_ERRMSG + MAX_PATH + 3];
		snprintf( errbuf, sizeof(errbuf), "Unable to write file: %s", path->mbs );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
		goto Exit;
	}
	while ( !feof( src ) ){
		size_t count = fread( buf, sizeof(char), sizeof(buf), src );
		fwrite( buf, sizeof(char), count, dest );
	}
	copy = DInode_New();
	if ( ( res = DInode_Open( copy, path->mbs ) ) != 0 ){
		char errbuf[MAX_ERRMSG];
		DInode_Delete( copy );
		GetErrorMessage( errbuf, res, 0 );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
		goto Exit;
	}
	DaoProcess_PutCdata( proc, copy, daox_type_fsnode );
Exit:
	DString_Delete( path );
	if ( src ) fclose( src );
	if ( dest ) fclose( dest );
}

static void FSNode_Owner( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *self = (DInode*)DaoValue_TryGetCdata( p[0] );
	DString *name = DString_New( 1 );
	int res = DInode_GetOwner( self, name );
	if ( res )
		DaoProcess_RaiseException( proc, DAO_ERROR, "Unable to get information on file owner" );
	else
		DaoProcess_PutString( proc, name );
	DString_Delete( name );
}

static void FSNode_New( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *fsnode = DInode_New();
	int res;
	DString *path = DString_Copy( p[0]->xString.data );
	DString_ToMBS( path );
	if( ( res = DInode_Open( fsnode, path->mbs ) ) != 0 ){
		char errbuf[MAX_ERRMSG + MAX_PATH + 3];
		DInode_Delete( fsnode );
		if( res == -1 )
			strcpy( errbuf, "File object is not a file or directory" );
		else
			GetErrorMessage( errbuf, res, 0 );
		if( res == -1 || res == ENOENT )
			snprintf( errbuf + strlen( errbuf ), MAX_PATH + 3, ": %s", path->mbs );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
	}
	else
		DaoProcess_PutCdata( proc, (void*)fsnode, daox_type_fsnode );
	DString_Delete( path );
}

static void FS_CWD( DaoProcess *proc, DaoValue *p[], int N )
{
	char buf[MAX_PATH + 1];
	int res = 0;
	DInode *fsnode = DInode_New();
	FS_TRANS( res = (int)getcwd( buf, sizeof(buf) ) );
	if( !res || ( res = DInode_Open( fsnode, buf ) ) != 0 ){
		char errbuf[MAX_ERRMSG];
		DInode_Delete( fsnode );
		GetErrorMessage( errbuf, ( res == 0 )? errno : res, 0 );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
	}
	else
		DaoProcess_PutCdata( proc, (void*)fsnode, daox_type_fsnode );
}

static void FS_PWD( DaoProcess *proc, DaoValue *p[], int N )
{
	char path[MAX_PATH + 1];
	int res = 0;
	FS_TRANS( res = (int)getcwd( path, sizeof(path) ) );
	if( !res ){
		char errbuf[MAX_ERRMSG];
		GetErrorMessage( errbuf, ( res == 0 )? errno : res, 0 );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
	}
	else {
		char buf[MAX_PATH + 1];
		if ( NormalizePath( path, buf ) )
			DaoProcess_PutMBString( proc, buf );
		else
			DaoProcess_PutMBString( proc, path );
	}
}

static void FS_SetCWD( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *fsnode = (DInode*)DaoValue_TryGetCdata( p[0] );
	int res;
	if( fsnode->type != 0 ){
		DaoProcess_RaiseException( proc, DAO_ERROR, "File object is not a directory" );
		return;
	}
	FS_TRANS( res = chdir( fsnode->path ) );
	if( res ){
		char errbuf[MAX_PATH + 1];
		GetErrorMessage( errbuf, errno, 0 );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
	}
}

static void FS_SetCWD2( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *path = DString_Copy( p[0]->xString.data );
	int res;
	DInode *fsnode = DInode_New();
	DString_ToMBS( path );
	if( ( res = DInode_Open( fsnode, path->mbs ) ) == 0 && fsnode->type == 0 )
		FS_TRANS( res = chdir( fsnode->path ) );
	if ( res ) {
		char errbuf[MAX_ERRMSG + MAX_PATH + 3];
		GetErrorMessage( errbuf, errno, 0 );
		if( res == -1 || res == ENOENT )
			snprintf( errbuf + strlen( errbuf ), MAX_PATH + 3, ": %s", path->mbs );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
	}
	DString_Delete( path );
	DInode_Delete( fsnode );
}

static void FS_NormPath( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *path = DString_Copy( p[0]->xString.data );
	char buf[MAX_PATH + 1];
	DString_ToMBS( path );
	if ( NormalizePath( path->mbs, buf ) )
		DaoProcess_PutMBString( proc, buf );
	else
		DaoProcess_RaiseException( proc, DAO_ERROR, "Invalid path" );
	DString_Delete( path );
}

static void FS_Exists( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *path = DString_Copy( p[0]->xString.data );
	DInode *fsnode = DInode_New();
	DString_ToMBS( path );
	DaoProcess_PutInteger( proc, DInode_Open( fsnode, path->mbs ) == 0 );
	DString_Delete( path );
	DInode_Delete( fsnode );
}

static void FS_TmpName( DaoProcess *proc, DaoValue *p[], int N )
{
	DString *tml = DString_Copy( p[0]->xString.data );
	char buf[MAX_PATH + 1], nbuf[MAX_PATH + 1], *name;
	DString_ToMBS( tml );
	if ( !tml->size )
		name = tmpnam( buf );
	else {
		strcpy( buf, tml->mbs );
		if ( tml->size < 6 || strcmp( tml->mbs + tml->size - 6, "XXXXXX" ) != 0 )
			strcat( buf, "XXXXXX" );
		name = mktemp( buf );
	}
	if ( name )
		NormalizePath( buf, nbuf );
	DaoProcess_PutMBString( proc, name? nbuf: "" );
	DString_Delete( tml );
}

static void FS_Roots( DaoProcess *proc, DaoValue *p[], int N )
{
	DaoList *lst = DaoProcess_PutList( proc );
	if ( !GetRootDirs( lst ) )
		DaoProcess_RaiseException( proc, DAO_ERROR, "Failed to obtain the list of root directories" );
}

static void FS_NewFile( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *fsnode = DInode_New();
	int res;
	DString *path = DString_Copy( p[0]->xString.data );
	DString_ToMBS( path );
	if( ( res = DInode_Open( fsnode, path->mbs ) ) != 0 ){
		char errbuf[MAX_ERRMSG + MAX_PATH + 3];
		DInode_Delete( fsnode );
		if( res == -1 )
			strcpy( errbuf, "File object is not a file or directory" );
		else
			GetErrorMessage( errbuf, res, 0 );
		if( res == -1 || res == ENOENT )
			snprintf( errbuf + strlen( errbuf ), MAX_PATH + 3, ": %s", path->mbs );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
	}
	else if ( fsnode->type == 0 ){
		DInode_Delete( fsnode );
		DaoProcess_RaiseException( proc, DAO_ERROR, "File object is not a file" );
	}
	else
		DaoProcess_PutCdata( proc, (void*)fsnode, daox_type_fsnode );
	DString_Delete( path );
}

static void FS_NewDir( DaoProcess *proc, DaoValue *p[], int N )
{
	DInode *fsnode = DInode_New();
	int res;
	DString *path = DString_Copy( p[0]->xString.data );
	DString_ToMBS( path );
	if( ( res = DInode_Open( fsnode, path->mbs ) ) != 0 ){
		char errbuf[MAX_ERRMSG + MAX_PATH + 3];
		DInode_Delete( fsnode );
		if( res == -1 )
			strcpy( errbuf, "File object is not a file or directory" );
		else
			GetErrorMessage( errbuf, res, 0 );
		if( res == -1 || res == ENOENT )
			snprintf( errbuf + strlen( errbuf ), MAX_PATH + 3, ": %s", path->mbs );
		DaoProcess_RaiseException( proc, DAO_ERROR, errbuf );
	}
	else if ( fsnode->type == 1 ){
		DInode_Delete( fsnode );
		DaoProcess_RaiseException( proc, DAO_ERROR, "File object is not a directory" );
	}
	else
		DaoProcess_PutCdata( proc, (void*)fsnode, daox_type_fsnode );
	DString_Delete( path );
}

static DaoFuncItem fsnodeMeths[] =
{
	/*! Returns new fsnode given @path of file or directory */
	{ FSNode_New,      "fsnode( path : string )=>fsnode" },

	/*! Returns full path */
	{ FSNode_Path,     "path( self : fsnode )=>string" },

	/*! Returns file object name */
	{ FSNode_Name,     "name( self : fsnode )=>string" },

	/*! Returns base name of file object (up to, but not including, the first '.') */
	{ FSNode_BaseName, "basename( self : fsnode )=>string" },

	/*! Returns name part after the last '.' */
	{ FSNode_Suffix,   "suffix( self: fsnode )=>string" },

	/*! Returns type of file object: file or directory */
	{ FSNode_Type,     "type( self : fsnode )=>enum<file, dir>" },

	/*! Returns size of file (0 for directory) */
	{ FSNode_Size,     "size( self : fsnode )=>int" },

	/*! Resizes file */
	{ FSNode_Resize,     "resize( self : fsnode, size : int )" },

	/*! Returns parent directory, raises exception if none */
	{ FSNode_Parent,   "parent( self : fsnode )=>fsnode" },

	/*! Returns creation, last modification and last access time (use time module to operate them) */
	{ FSNode_Time,    "time( self : fsnode )=>tuple<created: int, modified: int, accessed: int>" },

	/*! Returns owner name */
	{ FSNode_Owner,    "owner( self : fsnode )=>string" },

	/*! Returns access mode as a combination of 'r', 'w' and 'x'. On Windows, only permissions for the current user are returned */
	{ FSNode_Access,   "access( self : fsnode )=>tuple<user: string, group: string, other: string>" },

	/*! Sets access mode to @mode, where @mode is a combination of 'r', 'w' and 'x'. On Windows, only permissions for the current user are changed */
	{ FSNode_SetAccess,"access( self : fsnode, user : string, group='', other='' )" },

	/*! Moves (renames) file object within the file system so that its full path becomes @path. @path may end with directory separator,
	 * omitting the file object name, in which case the current name is assumed */
	{ FSNode_Rename,   "move( self : fsnode, path : string )" },

	/*! Deletes file or empty directory
	 * \note Doing this does not invalidate the fsnode */
	{ FSNode_Remove,   "delete( self : fsnode )" },

	/*! For directory creates new file given its relative @path and returns its fsnode */
	{ FSNode_Makefile, "mkfile( self : fsnode, path : string )=>fsnode" },

	/*! For directory creates new directory given its relative @path and returns its fsnode */
	{ FSNode_Makedir,  "mkdir( self : fsnode, path : string )=>fsnode" },

	/*! For directory returns list of inner file objects of the given @type with names matching @filter,
	 * where @filter type is defined by @filtering and can be either a wildcard pattern or normal string pattern */
	{ FSNode_Children, "children( self : fsnode, type : enum<files; dirs>, filter='*', filtering : enum<wildcard, regex> = $wildcard )=>list<fsnode>" },

	/*! For directory returns list of inner files with names matching @filter,
	 * where @filter type is defined by @filtering and can be either a wildcard pattern or normal string pattern */
	{ FSNode_Files,    "files( self : fsnode, filter='*', filtering : enum<wildcard, regex> = $wildcard )=>list<fsnode>" },

	/*! For directory returns list of inner directories with names matching @filter,
	 * where @filter type is defined by @filtering and can be either a wildcard pattern or normal string pattern */
	{ FSNode_Dirs,     "dirs( self : fsnode, filter='*', filtering : enum<wildcard, regex> = $wildcard )=>list<fsnode>" },

	/*! For directory returns inner fsnode given its relative @path */
	{ FSNode_Child,    "[]( self : fsnode, path : string )=>fsnode" },

	/*! Copies file and returns fsnode of the copy */
	{ FSNode_Copy,  "copy( self : fsnode, path : string )=>fsnode" },

	/*! For directory returns non-zero it it contains file object specified by relative @path */
	{ FSNode_Contains,  "contains( self : fsnode, path : string )=>int" },

	/*! Re-reads all attributes of file object */
	{ FSNode_Update,   "refresh( self : fsnode )" },

	{ NULL, NULL }
};

/*! Provides platform-independent interface for manipulating files and directories */
DaoTypeBase fsnodeTyper = {
	"fsnode", NULL, NULL, fsnodeMeths, {NULL}, {0}, (FuncPtrDel)DInode_Delete, NULL
};

static DaoFuncItem fsMeths[] =
{
	/*! Returns fsnode of the current working directory */
	{ FS_CWD,	"cwd()=>fsnode" },

	/*! Returns path of the current working directory */
	{ FS_PWD,	"pwd()=>string" },

	/*! Makes @dir the current working directory */
	{ FS_SetCWD,	"cd( dir : fsnode )" },
	{ FS_SetCWD2,	"cd( dir : string )" },

	/*! Returns absolute form of @path. On Windows, replaces all '\' in path with '/' */
	{ FS_NormPath,	"realpath( path : string )=>string" },

	/*! Returns non-zero if @path exists and is a file or directory */
	{ FS_Exists,   "exists( path : string )=>int" },

	/*! Generates unique file name based on @template if it is not empty. An example of template is '/tmp/prefixXXXXXX'.
	 * If @template does not end with 'XXXXXX', it is automatically appended. Returns empty string in case of failure */
	{ FS_TmpName,   "tmpname( template='' )=>string" },

	/*! On Windows, returns list of root directories (drives). On other systems returns {'/'} */
	{ FS_Roots,		"roots()=>list<string>" },

	/*! Returns fsnode for @path if it points to a file, otherwise raises exception */
	{ FS_NewFile,	"file( path : string )=>fsnode" },

	/*! Returns fsnode for @path if it points to a directory, otherwise raises exception */
	{ FS_NewDir,	"dir( path : string )=>fsnode" },

	{ NULL, NULL }
};

DAO_DLL int DaoFS_OnLoad( DaoVmSpace *vmSpace, DaoNamespace *ns )
{
	DaoNamespace *fsns;
	FS_INIT();
	fsns = DaoVmSpace_GetNamespace( vmSpace, "fs" );
	DaoNamespace_AddConstValue( ns, "fs", (DaoValue*)fsns );
	DaoNamespace_AddParent( fsns, ns );
	daox_type_fsnode = DaoNamespace_WrapType( ns, & fsnodeTyper, 1 );
	DaoNamespace_WrapFunctions( fsns, fsMeths );
	return 0;
}
