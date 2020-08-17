//
//  HPI File Packer
//

#include <windows.h>
#include <shlobj.h>
//#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "tree.h"

#include "resource.h"

#include "zlib.h"

#define HPI_V1 0x00010000
#define HPI_V2 0x00020000

#define HEX_HAPI 0x49504148
// 'BANK'
#define HEX_BANK 0x4B4E4142
// 'SQSH'
#define HEX_SQSH 0x48535153

#define OUTBLOCKSIZE 16384

#define INBLOCKSIZE (65536+17)

#define NO_COMPRESSION 0
#define LZ77_COMPRESSION 1
#define ZLIB_COMPRESSION 2

#pragma pack(1)

typedef struct _HPIVERSION {
  long HPIMarker;              /* 'HAPI' */
  long Version;                /* 'BANK' if savegame, 0x00010000, or 0x00020000 */
} HPIVERSION;

typedef struct _HPIHEADER1 {
  long DirectorySize;                /* Directory size */
  long Key;                    /* decode key */
  long Start;                  /* offset of directory */
} HPIHEADER1;

typedef struct _HPIHEADER2 {
  long DirBlock;
  long DirSize;
  long NameBlock;
  long NameSize;
  long Data;
  long Last78;
} HPIHEADER2;

typedef struct _HPIENTRY1 {
  int NameOffset;
  int CountOffset;
  char Flag;
} HPIENTRY1;

typedef struct _HPICHUNK {
  long Marker;            /* always 0x48535153 (SQSH) */
  char Unknown1;          /* I have no idea what these mean */
	char CompMethod;				/* 1 = lz77, 2 = zlib */
	char Encrypt;           /* Is the chunk encrypted? */
  long CompressedSize;    /* the length of the compressed data */
  long DecompressedSize;  /* the length of the decompressed data */
  long Checksum;          /* check sum */
} HPICHUNK;

typedef struct _HPIDIR2 {
  long NamePtr;
  long FirstSubDir;
  long SubCount;
  long FirstFile;
  long FileCount;
} HPIDIR2;

typedef struct _HPIENTRY2 {
  long NamePtr;
  long Start;
  long DecompressedSize;
  long CompressedSize; /* 0 = no compression */
  long Date;  /* date in time_t format */
  long Checksum;
} HPIENTRY2;

#pragma pack()

typedef struct _DIRENTRY {
	struct _DIRENTRY *Next;
	struct _DIRENTRY *Prev;
	struct _DIRENTRY *FirstSub;
	struct _DIRENTRY *LastSub;
	int dirflag;
	int count;
  int CompSize;
	int *DirOffset;
	char *Name;
	char *FileName;
  time_t Date;
  int fpos;
  int NameLen;
  long Checksum;
} DIRENTRY;

#define TADIR_REG_ENTRY "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Total Annihilation"
#define TAKDIR_REG_ENTRY "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\Kingdoms.exe"

HANDLE hThisInstance = NULL;
HWND hwndMain = NULL;
HIMAGELIST FileIcons = NULL;
HANDLE ProgHeap = NULL;

const char HPIFilter[] = "HPI Files (*.hpi,*.ufo,*.gp?,*.ccx,*.kmp)\0*.ufo;*.hpi;*.gp?;*.ccx;*.kmp\0All Files (*.*)\0*.*\0";
const char szAppTitle[] = "HPI File Packer";
const char szAppName[] = "HPIPack";
const char szTrailer[] = "HPIPack by Joe D (joed@cws.org) FNORD Total Annihilation Copyright 1997 Cavedog Entertainment";
const char szTrailer2[] = "HPIPack by Joe D (joed@cws.org) FNORD Total Annihilation: Kingdoms Copyright 1999 Cavedog Entertainment";
//const char szTrailer2[] = "Copyright 1999 Cavedog Entertainment";

char CurrentDirectory[MAX_PATH];
char CurrentSaveDirectory[MAX_PATH];
char PackDirectory[MAX_PATH];
char SaveName[MAX_PATH];
char ININame[MAX_PATH];
int CProgram = HPI_V1;
int CMethod = ZLIB_COMPRESSION;
int CLevel = Z_DEFAULT_COMPRESSION;  //Z_BEST_COMPRESSION; 

FILE *HPIFile;
FILE *OldFile;
FILETIME OldDate;

int Key;

//char *Directory;
//char *Names;
LPSTR CommandLine = NULL;
int Packing = FALSE;
int Closing = FALSE;
HANDLE PackHandle = NULL;
DWORD PackThreadID = 0;
int FileTotal;
int FileCount;
int CompTotal;
int UncompTotal;
int CompleteCount;
int TotalCount;
int AutoMode = FALSE;

char *Window = NULL;
int WinOfs = 0;
int WinLen = 0;
unsigned int WIn;
unsigned int WOut;
unsigned int ChunkPtr = 0;
int BlockSize = 0;

//FILE *DumpFile = NULL;

TREENODE *SearchTree = NULL;

static LPVOID GetMem(int size, int zero)
{
  if (!ProgHeap) {
    ProgHeap = HeapCreate(0, 250000, 0);
    if (!ProgHeap)
      return NULL;
  }

  return HeapAlloc(ProgHeap, (zero ? HEAP_ZERO_MEMORY : 0), size); 
  //return GlobalAlloc(GPTR, size);
	//ZeroMemory(This, sizeof(DIRENTRY));
}

static void FreeMem(LPVOID x)
{
  HeapFree(ProgHeap, 0, x);
  //GlobalFree(x);
}

static LPSTR DupString(LPSTR x)
{
  LPSTR s = GetMem(strlen(x)+1, FALSE);
  strcpy(s, x);
  return s;
}

int FileExists(LPSTR FName)
{
  FILE *f;

  f = fopen(FName, "r");
  if (!f)
    return FALSE;
  fclose(f);
  return TRUE;
}

void StatusMessage(LPSTR fmt, ...)
{
  va_list argptr;
  char tstr[1024];
  int index;
  HWND hwndlb = GetDlgItem(hwndMain, IDC_STATLIST);
  
  va_start(argptr, fmt);
  wvsprintf(tstr, fmt, argptr);
  va_end(argptr);
  index = SendMessage(hwndlb, LB_ADDSTRING, 0, (LPARAM) tstr);
  SendMessage(hwndlb, LB_SETTOPINDEX, index, 0);
  UpdateWindow(hwndlb);
}

time_t FileTimeToTimeT(FILETIME *ft)
{
  SYSTEMTIME st;
  struct tm t;

  FileTimeToSystemTime(ft, &st);

  t.tm_year = st.wYear - 1900;
  t.tm_mon = st.wMonth - 1;     
  t.tm_mday = st.wDay; 
  t.tm_hour = st.wHour;     
  t.tm_min = st.wMinute;     
  t.tm_sec = st.wSecond;     
  return mktime(&t);
}

void SetMethodCombo(int method)
{
  int index;

  SendDlgItemMessage(hwndMain, IDC_CMETHOD, CB_RESETCONTENT, 0, 0);
  switch (method) {
    case HPI_V1 :
      ShowWindow(GetDlgItem(hwndMain, IDC_STATBAR), SW_SHOW);
      index = SendDlgItemMessage(hwndMain, IDC_CMETHOD, CB_ADDSTRING, 0, (LPARAM) "None");
	    SendDlgItemMessage(hwndMain, IDC_CMETHOD, CB_SETITEMDATA, index, (LPARAM) NO_COMPRESSION);
	    index = SendDlgItemMessage(hwndMain, IDC_CMETHOD, CB_ADDSTRING, 0, (LPARAM) "TA");
	    SendDlgItemMessage(hwndMain, IDC_CMETHOD, CB_SETITEMDATA, index, (LPARAM) LZ77_COMPRESSION);
	    index = SendDlgItemMessage(hwndMain, IDC_CMETHOD, CB_ADDSTRING, 0, (LPARAM) "TA:CC");
	    SendDlgItemMessage(hwndMain, IDC_CMETHOD, CB_SETITEMDATA, index, (LPARAM) ZLIB_COMPRESSION);
      break;
    case HPI_V2 :
      ShowWindow(GetDlgItem(hwndMain, IDC_STATBAR), SW_HIDE);
      index = SendDlgItemMessage(hwndMain, IDC_CMETHOD, CB_ADDSTRING, 0, (LPARAM) "None");
	    SendDlgItemMessage(hwndMain, IDC_CMETHOD, CB_SETITEMDATA, index, (LPARAM) NO_COMPRESSION);
	    index = SendDlgItemMessage(hwndMain, IDC_CMETHOD, CB_ADDSTRING, 0, (LPARAM) "Auto");
	    SendDlgItemMessage(hwndMain, IDC_CMETHOD, CB_SETITEMDATA, index, (LPARAM) ZLIB_COMPRESSION);
      break;
  }  
}

void SetComboBox(HWND hwnd, int id, int value)
{
  int count;
  int x;

  count = SendDlgItemMessage(hwnd, id, CB_GETCOUNT, 0, 0);
  for (x = 0; x < count; x++) {
    if (value == SendDlgItemMessage(hwnd, id, CB_GETITEMDATA, x, 0)) {
    	SendDlgItemMessage(hwnd, id, CB_SETCURSEL, x, 0);  
      return;
    } 
  }
	SendDlgItemMessage(hwnd, id, CB_SETCURSEL, 0, 0);  
}

LPSTR GetCommandParameter(LPSTR cmd, LPSTR parm)
{
  *parm = 0;

  while (isspace(*cmd))
    cmd++;

  if (!*cmd)
    return cmd;

  if (*cmd == '"') {
    cmd++;
    while (*cmd && (*cmd != '"')) {
      if (*cmd == '\\') {
        if ((cmd[1] == '"') || (cmd[1] == '\\'))
          cmd++;
        *parm++ = *cmd++;
      }
      else
        *parm++ = *cmd++;
    }
    while (*cmd == '"')
      cmd++;
  }
  else {
    while (*cmd && !isspace(*cmd))
      *parm++ = *cmd++;
  }
  *parm = 0;

  while (isspace(*cmd))
    cmd++;

  return cmd;
}


/*
void DumpMessage(LPSTR fmt, ...)
{
  va_list argptr;
  char tstr[1024];

  if (!DumpFile) {
    DumpFile = fopen("dump.txt", "wt");
    if (!DumpFile)
      return;
  }
  
  va_start(argptr, fmt);
  vsprintf(tstr, fmt, argptr);
  va_end(argptr);
  fputs(tstr, DumpFile);
  fflush(DumpFile);
}
*/
/*
void DumpTree(TREENODE *t, int l)
{
	int	i;

	if (!t)
    return;
	DumpTree(t->tree_l, l+1);
	for (i=0;  i<l;  i++)
    DumpMessage("  ");
	DumpMessage("0x%08X", (int) t->tree_p);
  for (i = 0; i < 17; i++) {
    DumpMessage(" 0x%02X", (unsigned char) Window[i+(int)t->tree_p]);
  }
  DumpMessage("\n");
	DumpTree(t->tree_r, l+1);
}	*/

void SetBar(int id, int CurPos, int Max)
{
	int BarPos;
  double cpos = CurPos;
  double cmax = Max;
	RECT rc;
	HWND hwnd = GetDlgItem(hwndMain, id);
	HDC hdc = GetDC(hwnd);

	GetClientRect(hwnd, &rc);
  cpos = cpos / cmax;
  cpos *= rc.right;
	BarPos = (int) cpos;
  if (BarPos > rc.right)
    BarPos = rc.right;
	if (BarPos) {
	  SelectObject(hdc, GetSysColorBrush(COLOR_HIGHLIGHT));
	  PatBlt(hdc, 0, 0, BarPos, rc.bottom, PATCOPY);
  	BarPos++;
	}
	if (BarPos < rc.right) {
  	SelectObject(hdc, GetSysColorBrush(COLOR_3DFACE));
	  PatBlt(hdc, BarPos, 0, rc.right-BarPos, rc.bottom, PATCOPY);
	}
	ReleaseDC(hwnd, hdc);
}

LPSTR GetTADirectory(LPSTR TADir)
{
	HKEY hk;
	int size = MAX_PATH;

	*TADir = 0;
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, TADIR_REG_ENTRY, 0, KEY_READ, &hk))
	  return TADir;
	RegQueryValueEx(hk, "Dir", 0, NULL, TADir, &size);
	RegCloseKey(hk);
	return TADir;
}

LPSTR GetTAKDirectory(LPSTR TAKDir)
{
	HKEY hk;
	int size = MAX_PATH;

	*TAKDir = 0;
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, TAKDIR_REG_ENTRY, 0, KEY_READ, &hk))
	  return TAKDir;
	RegQueryValueEx(hk, "Path", 0, NULL, TAKDir, &size);
	RegCloseKey(hk);
	return TAKDir;
}

int BrowseForDirectory(void)
{
	BROWSEINFO bi;
	LPITEMIDLIST iid;
  LPMALLOC g_pMalloc;
	char DispName[MAX_PATH];

  bi.hwndOwner = hwndMain;
  bi.pidlRoot = NULL;
  bi.pszDisplayName = DispName;
  bi.lpszTitle = "Select directory to pack:";
  bi.ulFlags = BIF_RETURNONLYFSDIRS; 
  bi.lpfn = NULL;
  bi.lParam = 0;
  bi.iImage = 0;

	iid = SHBrowseForFolder(&bi);

	if (!iid)
		return FALSE;

	
	SHGetPathFromIDList(iid, PackDirectory);

  SHGetMalloc(&g_pMalloc);         
  g_pMalloc->lpVtbl->Free(g_pMalloc, iid);
  g_pMalloc->lpVtbl->Release(g_pMalloc);  
	
	SetDlgItemText(hwndMain, IDC_DIRNAME, PackDirectory);

	UpdateWindow(hwndMain);

	return TRUE;
}

int BrowseForDirectory2(void)
{
  OPENFILENAME ofn;
	char Name[MAX_PATH];

	Name[0] = 0;
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwndMain;
  ofn.lpstrFile = Name;
	ofn.lpstrInitialDir = PackDirectory;
  ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = "Select directory to pack";
  ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR |
              OFN_HIDEREADONLY | OFN_NOTESTFILECREATE;

  if (!GetSaveFileName(&ofn))
    return FALSE;

	UpdateWindow(hwndMain);

	return TRUE;
}

int OpenSaveFile(char *Name, const char *filter, char *defext)
{
  OPENFILENAME ofn;

  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = hwndMain;
  ofn.lpstrFilter = filter;
  ofn.nFilterIndex = 1;
  ofn.lpstrFile = Name;
	ofn.lpstrInitialDir = CurrentSaveDirectory;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | 
              OFN_HIDEREADONLY | OFN_NOTESTFILECREATE;
	ofn.lpstrDefExt = defext;

  if (!GetSaveFileName(&ofn))
    return FALSE;

	UpdateWindow(hwndMain);

	SetDlgItemText(hwndMain, IDC_HPINAME, Name);

	strcpy(CurrentSaveDirectory, Name);
	CurrentSaveDirectory[ofn.nFileOffset] = 0;

	return TRUE;
}

void ScanFileNames(LPSTR DName, DIRENTRY *Start)
{
	HANDLE ff;
	WIN32_FIND_DATA fd;
	char ScanDir[MAX_PATH];
	DIRENTRY *Prev = NULL;
	DIRENTRY *This;
	int count;

	strcpy(ScanDir, DName);
	strcat(ScanDir, "\\*");
	count = 0;
	ff = FindFirstFile(ScanDir, &fd);
	do {
		if (strcmp(".", fd.cFileName) == 0)
			continue;
		if (stricmp("..", fd.cFileName) == 0)
			continue;
		count++;
		//StatusMessage("%s\\%s", DName, fd.cFileName);
		This = GetMem(sizeof(DIRENTRY), TRUE);
		This->Name = DupString(fd.cFileName);
		if (Prev) {
			Prev->Next = This;
			This->Prev = Prev;
		}
		Prev = This;

		if (!Start->FirstSub)
			Start->FirstSub = This;
		Start->LastSub = This;

  	strcpy(ScanDir, DName);
		strcat(ScanDir, "\\");
		strcat(ScanDir, fd.cFileName);
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			This->dirflag = TRUE;
			ScanFileNames(ScanDir, This);
		}
		else {
			This->dirflag = FALSE;
			This->count = fd.nFileSizeLow;
			This->FileName = DupString(ScanDir);
      This->Date = FileTimeToTimeT(&fd.ftLastWriteTime);

			FileTotal += fd.nFileSizeLow;
		}
	}
	while (FindNextFile(ff, &fd));
	FindClose(ff);
	Start->count = count;
}

int GetDirSize1(DIRENTRY *Start)
{
	DIRENTRY *This;
  int dsize = 0;

  This = Start->FirstSub;
	while (This) {

    dsize += strlen(This->Name)+1;

		if (This->FirstSub) {
			dsize += GetDirSize1(This);
		}
		else {
			dsize += sizeof(HPIENTRY1);
		}
    This = This->Next;
	}

  dsize += (2 * sizeof(int));
 	dsize += Start->count * sizeof(HPIENTRY1);

	return dsize;
}

int EncryptAndWrite(int fpos, void *b, int buffsize)
{
	int count;
	int tkey;
	int result;
	char *buff;
	
	if (Key) {
	  buff = GetMem(buffsize, FALSE);
	  for (count = 0; count < buffsize; count++) {
		  tkey = (fpos + count) ^ Key;
      buff[count] = tkey ^ ~((char *) b)[count];
		}
  	fseek(HPIFile, fpos, SEEK_SET);
	  result = fwrite(buff, 1, buffsize, HPIFile);
	  FreeMem(buff);
	}
	else {
  	fseek(HPIFile, fpos, SEEK_SET);
	  result = fwrite(b, 1, buffsize, HPIFile);
	}

	return result;
}

int ReadAndDecrypt(int fpos, char *buff, int buffsize)
{
	int count;
	int tkey;
	int result;
	
	fseek(HPIFile, fpos, SEEK_SET);
	result = fread(buff, 1, buffsize, HPIFile);
	if (Key) {
	  for (count = 0; count < buffsize; count++) {
		  tkey = (fpos + count) ^ Key;
      buff[count] = tkey ^ ~buff[count];
		}
	}
	return result;
}

int BuildDirectoryBlock1(char *Directory, DIRENTRY *dir, int DStart)
{
	int c = DStart;
	int nofs;
	char *n;
	DIRENTRY *d;

  *((int *)(Directory+c)) = dir->count;
	c += sizeof(int);
	*((int *)(Directory+c)) = c+sizeof(int);
	c += sizeof(int);
	nofs = c + (dir->count * sizeof(HPIENTRY1));
  d = dir->FirstSub;
  while (d) {
 	  //StatusMessage("Adding %s to directory", d->Name);
    *((int *)(Directory+c)) = nofs;
	  c += sizeof(int);
    n = d->Name;
    while (*n)
   		Directory[nofs++] = *n++;
 	  Directory[nofs++] = 0;
    *((int *)(Directory+c)) = nofs;
	  c += sizeof(int);
 	  if (d->dirflag) {
		  Directory[c++] = 1;
	    nofs = BuildDirectoryBlock1(Directory, d, nofs);
		}
    else {
			Directory[c++] = 0;
		  d->DirOffset = (int *) (Directory+nofs);
		  nofs += sizeof(int);
      *((int *)(Directory+nofs)) = d->count;
		  nofs += sizeof(int);
		  Directory[nofs++] = CMethod;
		}
		d = d->Next;
	}
	return nofs;
}

int WriteChar(int fpos, char *OutBlock, int *OutSize, void *data, int dsize, HPICHUNK *BlockHeader)
{
	int x;
	char *d = data;
	int o = *OutSize;
	unsigned char n;

	BlockHeader->CompressedSize += dsize;
	for (x = 0; x < dsize; x++) {
		n = (unsigned char) *d++;
		if (BlockHeader->Encrypt)
  		n = (n ^ o) + o;
		BlockHeader->Checksum += n;
		OutBlock[o++] = (char) n;
		if (o == OUTBLOCKSIZE) {
			EncryptAndWrite(fpos, OutBlock, o);
			fpos += o;
			o = 0;
		}
	}
	*OutSize = o;

	return fpos;
}

int CompFunc(void *d1, void *d2)
{
  unsigned int p1;
  unsigned int p2;
  char *k1;
  char *k2;
  int result;
  int i;
  int l;
  int m;

  if (d1 == d2)
    return 0;

//  if (!d1)
//    return -1;
//  if (!d2)
//    return 1;

  p1 = (unsigned int) d1;
  p2 = (unsigned int) d2;
  k1 = Window+p1;
  k2 = Window+p2;
  result = 0;
  l = 0;

  m = 17; //min(17, BlockSize);
  for (i = 0; i < m; i++) {
    //if ((p2+i) >= WOut)
    //  break;
    result = k1[i] - k2[i];
    if (result)
      break;
  }
  if (/*(p2 >= WIn) && */(WinLen <= i) && ((p2 & 0xFFF) != 0xFFF)) {
    WinLen = i;
    WinOfs = p2;
  }

  if (!result)
    result = p1-p2;

  return result;
}

int SearchWindow(int data)
{
  WinLen = 1;
  WinOfs = 0;

  TreeSearch(&SearchTree, CompFunc, (void *) data);

  if (WinLen > BlockSize)
    WinLen = BlockSize;

  return (WinLen >= 2);
}

void StopHPIThread(void)
{
	SetDlgItemText(hwndMain, IDOK, "&Pack");
	if (HPIFile)
	  fclose(HPIFile);
	SetBar(IDC_STATBAR, 0, 1);
	SetBar(IDC_FILEBAR, 0, 1);
	StatusMessage("Done!");
	Packing = FALSE;
  if (Closing)
    PostMessage(hwndMain, WM_CLOSE, 0, 0);
	if (PackHandle) {
	  PackHandle = NULL;
	  ExitThread(1);
	}
}

int LZ77CompressChunk(char *Chunk, HPICHUNK *BlockHeader, int fpos)
{
	//char *Window;
	char OutBlock[OUTBLOCKSIZE];
	int OutSize = 0;
	char flags;
	char data[17];
	int dptr;
	int mask;
	int olpair;
	int checksum = 0;
  int i;
  int Len;
	
  BlockSize = BlockHeader->DecompressedSize;

	mask = 0x01;
	flags = 0;
	dptr = 1;
	WIn = 0;
	WOut = 0;
  TreeInit(&SearchTree);
	ChunkPtr = 0;
	Window = Chunk;

	do {
    //DumpMessage("\nBlockSize: 0x%X   WIn: 0x%X   WOut: 0x%X   ChunkPtr: 0x%X\n", BlockSize, WIn, WOut, ChunkPtr);
		if (SearchWindow(ChunkPtr)) {
      //DumpMessage("Matched offset: 0x%X   Length: 0x%X\n", WinOfs, WinLen);

      WinOfs &= 0xFFF;
			flags |= mask;
		  olpair = ((WinOfs+1) << 4) | (WinLen-2);
			data[dptr++] = LOBYTE(LOWORD(olpair));
			data[dptr++] = HIBYTE(LOWORD(olpair));
		}
		else {
			data[dptr++] = Chunk[ChunkPtr];
			WinLen = 1;
		}
    Len = WinLen;
  	BlockSize -= Len;
    for (i = 0; i < Len; i++) {

      //DumpMessage("Adding ChunkPtr: 0x%X\n", ChunkPtr);

      TreeAdd(&SearchTree, CompFunc, (void *) (ChunkPtr), NULL);
      ChunkPtr++;
      WOut++;
      if (WOut > 4095) {
        //DumpMessage("Deleting WIn: 0x%X\n", WIn);
        if (!TreeDelete(&SearchTree, CompFunc, (void *) WIn, NULL)) {
          StatusMessage("Error deleting tree node 0x%X", WIn);
          //DumpMessage("Error deleting tree node 0x%X", WIn);
        }
			  WIn++;
      }
    }

		if (mask == 0x80) {
			data[0] = flags;
			fpos = WriteChar(fpos, OutBlock, &OutSize, data, dptr, BlockHeader);
			mask = 0x01;
			flags = 0;
			dptr = 1;
		}
		else
			mask <<= 1;
	}
	while (BlockSize > 0);
  
	flags |= mask;
	data[dptr++] = 0;
	data[dptr++] = 0;
	data[0] = flags;

	fpos = WriteChar(fpos, OutBlock, &OutSize, data, dptr, BlockHeader);

	if (OutSize) {
		EncryptAndWrite(fpos, OutBlock, OutSize);
		fpos += OutSize;
	}

	CompleteCount += BlockHeader->DecompressedSize;
	FileCount += BlockHeader->DecompressedSize;
	//BlockHeader->CompressedSize += zs.total_out;

	SetBar(IDC_STATBAR, CompleteCount, TotalCount);
  SetBar(IDC_FILEBAR, FileCount, FileTotal);
  if (!Packing)
		StopHPIThread();

	TreeDestroy(&SearchTree, NULL);
	return fpos;
}

int ZLibCompressChunk(char *Chunk, HPICHUNK *BlockHeader, int fpos)
{
	z_stream zs;
	int result;
	char *out;
	unsigned char n;
	int x;

	out = GetMem(131072, FALSE);

  zs.next_in = Chunk;
  zs.avail_in = BlockHeader->DecompressedSize;
  zs.total_in = 0;

  zs.next_out = out;
  zs.avail_out = 131072;
  zs.total_out = 0;

  zs.msg = NULL;
  zs.state = NULL;
  zs.zalloc = Z_NULL;
  zs.zfree = Z_NULL;
  zs.opaque = NULL;

  zs.data_type = Z_BINARY;
  zs.adler = 0;
  zs.reserved = 0;

	result = deflateInit(&zs, CLevel);
  if (result != Z_OK) {
    StatusMessage("Error on deflateInit %d", result);
		StatusMessage("Message: %s", zs.msg);
    return 0;
  }

	result = deflate(&zs, Z_FINISH);
  if (result != Z_STREAM_END) {
    StatusMessage("Error on deflate %d", result);
		StatusMessage("Message: %s", zs.msg);
    zs.total_out = 0;
  }

	if (zs.total_out) {
		//d = (unsigned char *) out;
	  for (x = 0; (unsigned int) x < zs.total_out; x++) {
		  n = (unsigned char) out[x];
			if (BlockHeader->Encrypt)	{
  		  n = (n ^ x) + x;
				out[x] = n;
			}
		  BlockHeader->Checksum += n;
		}

		EncryptAndWrite(fpos, out, zs.total_out);

		fpos += zs.total_out;
		CompleteCount += BlockHeader->DecompressedSize;
		FileCount += BlockHeader->DecompressedSize;
	  BlockHeader->CompressedSize += zs.total_out;

    SetBar(IDC_STATBAR, CompleteCount, TotalCount);
	  SetBar(IDC_FILEBAR, FileCount, FileTotal);
		if (!Packing)
			StopHPIThread();

	}

	result = deflateEnd(&zs);
  if (result != Z_OK) {
    StatusMessage("Error on deflateEnd %d", result);
		StatusMessage("Message: %s", zs.msg);
    zs.total_out = 0;
  }

	FreeMem(out);

	return fpos;

}


int CompressChunk(char *Chunk, HPICHUNK *BlockHeader, int fpos)
{
	switch (CMethod) {
		case LZ77_COMPRESSION :
			return LZ77CompressChunk(Chunk, BlockHeader, fpos);
		case ZLIB_COMPRESSION :
			return ZLibCompressChunk(Chunk, BlockHeader, fpos);
		default :
			return fpos;
	 }
}

int CompressFile(char *FName, int fpos, DIRENTRY *d)
{
	FILE *InFile;
	char TName[MAX_PATH];
	int BlockCount;
	int bpos;
	int *BlockPtr;
	int PtrBlockSize;
	HPICHUNK BlockHeader;
	int remain;
	int BlockNo;
	int hpos;
  char *InBlock;
  int Compressed;
  int CompPercent;

	strcpy(TName, PackDirectory);
	strcat(TName, "\\");
	strcat(TName, FName);

	StatusMessage("Packing %s", FName);

	InFile = fopen(TName, "rb");
	if (!InFile) {
		StatusMessage("** Unable to open %s", TName);
		return fpos;
	}

	*d->DirOffset = fpos;

  SetBar(IDC_STATBAR, 0, 1);

	if (CMethod) {
    BlockCount = d->count / 65536;
    if ((d->count % 65536))
      BlockCount++;

	  bpos = fpos;

	  PtrBlockSize = BlockCount * sizeof(int);
	  BlockPtr = GetMem(PtrBlockSize, TRUE);

    EncryptAndWrite(bpos, BlockPtr, PtrBlockSize);

	  fpos += PtrBlockSize;

	  InBlock = GetMem(INBLOCKSIZE, FALSE);

	  remain = d->count;
	  BlockNo = 0;
	  CompleteCount = 0;
	  TotalCount = d->count;

    Compressed = 0;

    while (remain) {
		  BlockHeader.Marker = HEX_SQSH;
		  BlockHeader.Unknown1 = 0x02;
		  BlockHeader.CompMethod = CMethod;
		  BlockHeader.Encrypt = 0x01;
		  BlockHeader.CompressedSize = 0;
		  BlockHeader.DecompressedSize = (remain > 65536 ? 65536 : remain);
		  BlockHeader.Checksum = 0;

		  EncryptAndWrite(fpos, &BlockHeader, sizeof(BlockHeader));
		  hpos = fpos;

		  fpos += sizeof(BlockHeader);

		  ZeroMemory(InBlock, INBLOCKSIZE);

		  fread(InBlock, 1, BlockHeader.DecompressedSize, InFile);

      CompressChunk(InBlock, &BlockHeader, fpos);

		  BlockPtr[BlockNo] = BlockHeader.CompressedSize+sizeof(BlockHeader);

		  EncryptAndWrite(hpos, &BlockHeader, sizeof(BlockHeader));

		  fpos += BlockHeader.CompressedSize;
      Compressed += BlockHeader.CompressedSize; 

		  remain -= BlockHeader.DecompressedSize;

		  BlockNo++;
		}

    CompPercent = (int) ((100.0 * (double) Compressed) / (double) d->count);
    StatusMessage("Size: %d  Compressed: %d  (%d%%)", d->count, Compressed, CompPercent);
	  SetBar(IDC_FILEBAR, FileCount, FileTotal);

	  UncompTotal += d->count;
	  CompTotal += Compressed;

    EncryptAndWrite(bpos, BlockPtr, PtrBlockSize);
	  FreeMem(BlockPtr);
	  FreeMem(InBlock);
	}
	else {
		InBlock = GetMem(d->count, FALSE);
		fread(InBlock, 1, d->count, InFile);
		EncryptAndWrite(fpos, InBlock, d->count);
		FreeMem(InBlock);
		fpos += d->count;
    StatusMessage("Size: %d  Compressed: %d  (100%%)", d->count, d->count);
	  UncompTotal += d->count;
	  CompTotal += d->count;
		FileCount += d->count;
	  SetBar(IDC_FILEBAR, FileCount, FileTotal);
	}

  SetBar(IDC_STATBAR, 1, 1);

	fclose(InFile);
	return fpos;
}

int CompressAndEncrypt(char *Path, int fpos, DIRENTRY *dir)
{
	DIRENTRY *d;
	char FName[MAX_PATH];

	d = dir->FirstSub;

	//StatusMessage("Packing %s", Path);
	while (d) {
		if (*Path) {
  		strcpy(FName, Path);
  		strcat(FName, "\\");
		  strcat(FName, d->Name);
		}
		else
		  strcpy(FName, d->Name);
		if (d->dirflag)
			fpos = CompressAndEncrypt(FName, fpos, d);
		else
			fpos = CompressFile(FName, fpos, d);
		d = d->Next;
	}

	return fpos;
}

void CompressChunk2(char *Chunk, char *out, HPICHUNK *BlockHeader)
{
	z_stream zs;
	int result;
	unsigned char n;
	int x;

  zs.next_in = Chunk;
  zs.avail_in = BlockHeader->DecompressedSize;
  zs.total_in = 0;

  zs.next_out = out;
  zs.avail_out = BlockHeader->DecompressedSize;
  zs.total_out = 0;

  zs.msg = NULL;
  zs.state = NULL;
  zs.zalloc = Z_NULL;
  zs.zfree = Z_NULL;
  zs.opaque = NULL;

  zs.data_type = Z_BINARY;
  zs.adler = 0;
  zs.reserved = 0;

	result = deflateInit(&zs, CLevel);
  if (result != Z_OK) {
    StatusMessage("Error on deflateInit %d", result);
		StatusMessage("Message: %s", zs.msg);
    return;
  }

	result = deflate(&zs, Z_FINISH);
  if (result != Z_STREAM_END) {
    if (result != Z_OK) {
      StatusMessage("Error on deflate %d", result);
	  	StatusMessage("Message: %s", zs.msg);
    }
    zs.total_out = 0;
  }

  if ((zs.total_out + sizeof(HPICHUNK)) > (unsigned) BlockHeader->DecompressedSize)
    zs.total_out = 0;

	if (zs.total_out) {
		//d = (unsigned char *) out;
	  for (x = 0; (unsigned int) x < zs.total_out; x++) {
		  n = (unsigned char) out[x];
			if (BlockHeader->Encrypt)	{
  		  n = (n ^ x) + x;
				out[x] = n;
			}
		  BlockHeader->Checksum += n;
		}

    BlockHeader->CompressedSize = zs.total_out;
	}

	result = deflateEnd(&zs);
  if (result != Z_OK) {
    StatusMessage("Error on deflateEnd %d", result);
		StatusMessage("Message: %s", zs.msg);
    zs.total_out = 0;
  }
}

unsigned char *CheckCalc2(long *cs, char *buff, long size)
{
  int count;
  unsigned int c;
  unsigned char *check = (unsigned char *) cs;

  for (count = 0; count < size; count++) {
    c = (unsigned char) buff[count];
    check[0] += c;
    check[1] ^= c;
    check[2] += (c ^ ((unsigned char) (count & 0x000000FF)));
    check[3] ^= (c + ((unsigned char) (count & 0x000000FF)));
  }
  return check;  
}

void CompressFile2(char *FName, DIRENTRY *d)
{
	FILE *InFile;
	char TName[MAX_PATH];
	HPICHUNK BlockHeader;
  char *InBlock;
  char *OutBlock;
  int CompPercent;
  int CompFlag;

	strcpy(TName, PackDirectory);
	strcat(TName, "\\");
	strcat(TName, FName);

  d->fpos = ftell(HPIFile);

  StatusMessage("Packing %s", FName);

	InFile = fopen(TName, "rb");
	if (!InFile) {
		StatusMessage("** Unable to open %s", TName);
		return;
	}

//  SetBar(IDC_STATBAR, 0, 1);

  InBlock = GetMem(d->count, FALSE);
  OutBlock = NULL;
	fread(InBlock, d->count, 1, InFile);
  d->Checksum = 0;
 
  CheckCalc2(&d->Checksum, InBlock, d->count);

  CompFlag = CMethod;

  //CompFlag = 0;

  if (CompFlag)
    CompFlag = (d->count > sizeof(HPICHUNK));
  
	if (CompFlag) {
	  BlockHeader.Marker = HEX_SQSH;
	  BlockHeader.Unknown1 = 0x02;
	  BlockHeader.CompMethod = CMethod;
	  BlockHeader.Encrypt = 0;
	  BlockHeader.CompressedSize = 0;
	  BlockHeader.DecompressedSize = d->count;
	  BlockHeader.Checksum = 0;

    OutBlock = GetMem(d->count, FALSE);

    CompressChunk2(InBlock, OutBlock, &BlockHeader);

    CompFlag = BlockHeader.CompressedSize;
  }

	//CompleteCount += BlockHeader.DecompressedSize;
	FileCount += d->count;
  UncompTotal += d->count;

  if (CompFlag) {
	  //for (x = 0; x < BlockHeader.CompressedSize; x++) {
		//  BlockHeader.Checksum += (unsigned char) OutBlock[x];
		//}
    d->CompSize = BlockHeader.CompressedSize + sizeof(HPICHUNK);
	  CompTotal += (d->CompSize + sizeof(HPICHUNK));

    CompPercent = (int) ((100.0 * (double) (d->CompSize + sizeof(HPICHUNK))) / (double) d->count);
    StatusMessage("Size: %d  Compressed: %d  (%d%%)", d->count, BlockHeader.CompressedSize, CompPercent);

		fwrite(&BlockHeader, sizeof(BlockHeader), 1, HPIFile);

		fwrite(OutBlock, BlockHeader.CompressedSize, 1, HPIFile);
	}
	else {
    d->CompSize = 0;
	  CompTotal += d->count;

    fwrite(InBlock, d->count, 1, HPIFile);
    StatusMessage("Size: %d  Compressed: %d  (100%%)", d->count, d->count);
	}

  SetBar(IDC_FILEBAR, FileCount, FileTotal);

  FreeMem(InBlock);
  if (OutBlock)
    FreeMem(OutBlock);

	fclose(InFile);

  if (!Packing)
		StopHPIThread();
}

void CountEntries2(DIRENTRY *dir, int *dsize, int *dcount, int *fcount)
{
	DIRENTRY *d;

	d = dir->FirstSub;

	//StatusMessage("Packing %s", Path);
	while (d) {
		*dsize += d->NameLen;
    if (d->dirflag) {
      (*dcount)++; 
			CountEntries2(d, dsize, dcount, fcount);
    }
    else {
      (*fcount)++;
    }
		d = d->Next;
	}
}

void BuildBlocks2(DIRENTRY *dir, HPIDIR2 *HPIDir, char *DirBlock, int *DirOfs, char *NameBlock, int *NameOfs)
{
	DIRENTRY *d;
  HPIDIR2 *HPISub;
  HPIENTRY2 *HPIEntry;
  int dcount;
  int fcount;

	d = dir->FirstSub;
  
  dcount = 0;
  HPISub = (HPIDIR2 *) (DirBlock + *DirOfs);

  HPIDir->FirstSubDir = *DirOfs;

  while (d) {
    if (d->dirflag) {
      HPIDir->SubCount++;
      HPISub->NamePtr = *NameOfs;
      HPISub->FirstSubDir = 0;
      HPISub->SubCount = 0;
      HPISub->FirstFile = 0;
      HPISub->FileCount = 0;

      strcpy(NameBlock + *NameOfs, d->Name);
      *NameOfs += d->NameLen;
      HPISub++;
      dcount++;
    }
 		d = d->Next;
	}

  HPIDir->SubCount = dcount;
  *DirOfs += (dcount * sizeof(HPIDIR2));

  d = dir->FirstSub;

  fcount = 0;
  HPIEntry = (HPIENTRY2 *) (DirBlock + *DirOfs);

  HPIDir->FirstFile = *DirOfs;

	while (d) {
    if (!d->dirflag) {
      HPIDir->FileCount++;
      HPIEntry->NamePtr = *NameOfs;
      HPIEntry->Start = d->fpos;
      HPIEntry->DecompressedSize = d->count;
      HPIEntry->CompressedSize = d->CompSize;
      HPIEntry->Date = d->Date;
      HPIEntry->Checksum = d->Checksum;

      strcpy(NameBlock + *NameOfs, d->Name);
      *NameOfs += d->NameLen;
      HPIEntry++;
      fcount++;
    }
		d = d->Next;
	}
  HPIDir->FileCount = fcount;
  *DirOfs += (fcount * sizeof(HPIENTRY2));

  HPISub = (HPIDIR2 *) (DirBlock + HPIDir->FirstSubDir);

	d = dir->FirstSub;

	while (d) {
    if (d->dirflag) {
			BuildBlocks2(d, HPISub, DirBlock, DirOfs, NameBlock, NameOfs);
      HPISub++;
    }
 		d = d->Next;
	}

}

void TraverseAndPack2(char *Path, DIRENTRY *dir)
{
	DIRENTRY *d;
	char FName[MAX_PATH];

	d = dir->FirstSub;

	//StatusMessage("Packing %s", Path);
	while (d) {
    d->NameLen = strlen(d->Name) + 1;
		if (*Path) {
  		strcpy(FName, Path);
  		strcat(FName, "\\");
		  strcat(FName, d->Name);
		}
		else
		  strcpy(FName, d->Name);
		if (d->dirflag)
			TraverseAndPack2(FName, d);
    else {
			CompressFile2(FName, d);
    }
		d = d->Next;
	}
}

void FreeDir(DIRENTRY *dir)
{
  DIRENTRY *n;

  while (dir) {
    FreeDir(dir->FirstSub);
    if (dir->Name)
      FreeMem(dir->Name);
		if (dir->FileName)
			FreeMem(dir->FileName);
    n = dir->Next;
    FreeMem(dir);
    dir = n;
  }
}


void PackHPI1(DIRENTRY *Root)
{
	int DirSize;
  char *Directory;
	int endpos;
  HPIVERSION Version;
  HPIHEADER1 Header1;

  DirSize = GetDirSize1(Root);

 	DirSize += (sizeof(HPIHEADER1) + sizeof(HPIVERSION));

  StatusMessage("Building HPI directory (0x%X)", DirSize);

  Directory = GetMem(DirSize, FALSE);

	Version.HPIMarker = HEX_HAPI;
	Version.Version = HPI_V1;
	Header1.DirectorySize = DirSize;

	Header1.Start = sizeof(HPIHEADER1) + sizeof(HPIVERSION);

	if (CMethod == LZ77_COMPRESSION) {
	  Header1.Key = 0x7D;
	  Key = ~((Header1.Key * 4)	| (Header1.Key >> 6));
	}
	else {
		Header1.Key = 0;
		Key = 0;
	}

	MoveMemory(Directory, &Version, sizeof(Version));
	MoveMemory(Directory+sizeof(Version), &Header1, sizeof(Header1));

  BuildDirectoryBlock1(Directory, Root, sizeof(HPIHEADER1) + sizeof(HPIVERSION));

  fwrite(Directory, DirSize, 1, HPIFile);

  endpos = CompressAndEncrypt("", DirSize, Root);

  fseek(HPIFile, endpos, SEEK_SET);

  fwrite(szTrailer, 1, strlen(szTrailer), HPIFile);

  EncryptAndWrite(Header1.Start, Directory+Header1.Start, DirSize-Header1.Start);
}

void PackHPI2(DIRENTRY *Root)
{
  HPIVERSION Version;
  HPIHEADER2 Header2;
  int dcount;
  int fcount;
  char *NameBlock;
  int NameSize;
  int NameOfs;
  char *DirBlock;
  int DirSize;
  int DirOfs;
  HPIDIR2 *HPIRoot;
  char *OutBlock;
  HPICHUNK BlockHeader;
  int CompFlag;

	Version.HPIMarker = HEX_HAPI;
	Version.Version = HPI_V2;
  fwrite(&Version, sizeof(Version), 1, HPIFile);

  Header2.DirBlock = 0;
  Header2.DirSize = 0;
  Header2.NameBlock = 0;
  Header2.NameSize = 0;
  Header2.Data = sizeof(Version) + sizeof(Header2);
  Header2.Last78 = 0;
  fwrite(&Header2, sizeof(Header2), 1, HPIFile);


  TraverseAndPack2("", Root);

  NameSize = 0;
  dcount = 0;
  fcount = 0;

  CountEntries2(Root, &NameSize, &dcount, &fcount);

  NameSize++;
  NameBlock = GetMem(NameSize, TRUE);

  DirSize = ((dcount+1) * sizeof(HPIDIR2)) + (fcount * sizeof(HPIENTRY2));
  DirBlock = GetMem(DirSize, TRUE);

  NameBlock[0] = 0;
  HPIRoot = (HPIDIR2 *) DirBlock;

  HPIRoot->NamePtr = 0;
  HPIRoot->FirstSubDir = 0;
  HPIRoot->SubCount = 0;
  HPIRoot->FirstFile = 0;
  HPIRoot->FileCount = 0;

  NameOfs = 1;
  DirOfs = sizeof(HPIDIR2);

  BuildBlocks2(Root, HPIRoot, DirBlock, &DirOfs, NameBlock, &NameOfs);

  OutBlock = NULL;

  CompFlag = CMethod;

  if (CompFlag)
    CompFlag = (NameSize > sizeof(HPICHUNK));
  
	if (CompFlag) {
	  BlockHeader.Marker = HEX_SQSH;
	  BlockHeader.Unknown1 = 0x02;
	  BlockHeader.CompMethod = CMethod;
	  BlockHeader.Encrypt = 1;
	  BlockHeader.CompressedSize = 0;
	  BlockHeader.DecompressedSize = NameSize;
	  BlockHeader.Checksum = 0;

    OutBlock = GetMem(NameSize, FALSE);

    CompressChunk2(NameBlock, OutBlock, &BlockHeader);

    CompFlag = BlockHeader.CompressedSize;
  }

  Header2.NameBlock = ftell(HPIFile);
  if (CompFlag) {
    Header2.NameSize = BlockHeader.CompressedSize + sizeof(HPICHUNK);
    fwrite(&BlockHeader, sizeof(HPICHUNK), 1, HPIFile);
    fwrite(OutBlock, BlockHeader.CompressedSize, 1, HPIFile);
  }
  else {
    Header2.NameSize = NameSize;
    fwrite(NameBlock, NameSize, 1, HPIFile);
  }
    
  if (OutBlock)
    FreeMem(OutBlock);

  OutBlock = NULL;

  CompFlag = CMethod;

  if (CompFlag)
    CompFlag = (DirSize > sizeof(HPICHUNK));
  
	if (CompFlag) {
	  BlockHeader.Marker = HEX_SQSH;
	  BlockHeader.Unknown1 = 0x02;
	  BlockHeader.CompMethod = CMethod;
	  BlockHeader.Encrypt = 1;
	  BlockHeader.CompressedSize = 0;
	  BlockHeader.DecompressedSize = DirSize;
	  BlockHeader.Checksum = 0;

    OutBlock = GetMem(DirSize, FALSE);

    CompressChunk2(DirBlock, OutBlock, &BlockHeader);

    CompFlag = BlockHeader.CompressedSize;
  }

  Header2.DirBlock = ftell(HPIFile);
  if (CompFlag) {
    Header2.DirSize = BlockHeader.CompressedSize + sizeof(HPICHUNK);
    fwrite(&BlockHeader, sizeof(HPICHUNK), 1, HPIFile);
    fwrite(OutBlock, BlockHeader.CompressedSize, 1, HPIFile);
  }
  else {
    Header2.DirSize = DirSize;
    fwrite(DirBlock, DirSize, 1, HPIFile);
  }
    
  if (OutBlock)
    FreeMem(OutBlock);

  fwrite(szTrailer2, sizeof(szTrailer2)-1, 1, HPIFile);

  fseek(HPIFile, sizeof(HPIVERSION), SEEK_SET);
  fwrite(&Header2, sizeof(Header2), 1, HPIFile);

  StatusMessage("%d files, %d directories", fcount, dcount);
}

DWORD WINAPI PackHPIThread(LPVOID v)
{
  SYSTEMTIME StartTime;
  SYSTEMTIME StopTime;
	int pct;
	char TempName[MAX_PATH];
	HANDLE hf;
	WIN32_FIND_DATA fd;
  DIRENTRY *Root = NULL;

	OldFile = fopen(SaveName, "rb");
	if (OldFile) {
		hf = FindFirstFile(SaveName, &fd);
		if (hf != INVALID_HANDLE_VALUE)
			FindClose(hf);
		OldDate = fd.ftLastWriteTime;
	}
	else {
		ZeroMemory(&OldDate, sizeof(OldDate));
	}
	strcpy(TempName, SaveName);
	strcat(TempName, ".tmp");

	HPIFile = fopen(TempName, "wb");
	if (!HPIFile) {
	  MessageBox(hwndMain, "Unable to create HPI file.", TempName, MB_OK);
	  PackHandle = NULL;
    AutoMode = FALSE;
	  return FALSE;
	}

	Packing = TRUE;
  SetDlgItemText(hwndMain, IDOK, "&Stop");
	FileCount = 0;
	FileTotal = 0;

	SendMessage(GetDlgItem(hwndMain, IDC_FILEBAR), PBM_SETPOS, 0, 0);

	CompTotal = 0;
	UncompTotal = 0;

  GetLocalTime(&StartTime);
  StatusMessage("Start time: %02d/%02d/%04d  %02d:%02d:%02d", 
    StartTime.wMonth, StartTime.wDay, StartTime.wYear,
    StartTime.wHour, StartTime.wMinute, StartTime.wSecond);

  StatusMessage("Packing %s into %s", PackDirectory, SaveName);

  switch (CProgram) {
    case HPI_V1 :
      StatusMessage("Packing for TA");
      switch (CMethod) {
        case NO_COMPRESSION :
          StatusMessage("No compression");
          break;
        case LZ77_COMPRESSION :
          StatusMessage("TA compression");
          break;
        case ZLIB_COMPRESSION :
          StatusMessage("TA:CC compression");
          break;
      }
      break;
    case HPI_V2 :
      StatusMessage("Packing for TA:Kingdoms");
      switch (CMethod) {
        case NO_COMPRESSION :
          StatusMessage("No compression");
          break;
        case ZLIB_COMPRESSION :
          StatusMessage("Auto compression");
          break;
      }
      break;
  }

	StatusMessage("Scanning...");

  Root = GetMem(sizeof(DIRENTRY), TRUE);

  ScanFileNames(PackDirectory, Root);

  switch (CProgram) {
    case HPI_V1 :
      PackHPI1(Root);
      break;
    case HPI_V2 :
      PackHPI2(Root);
      break;
  }

  if (OldFile)
    fclose(OldFile);

  fclose(HPIFile);


  DeleteFile(SaveName);
	MoveFile(TempName, SaveName);

	SetBar(IDC_STATBAR, 0, 1);
	SetBar(IDC_FILEBAR, 0, 1);
  
	pct = (int) ((100.0 * (double) CompTotal) / (double) UncompTotal);
  StatusMessage("Total size: %d  Compressed: %d  (%d%%)", UncompTotal, CompTotal, pct);

	StatusMessage("Done!");

	FreeDir(Root);
  Root = NULL;

  Packing = FALSE;
	PackHandle = NULL;
	SetDlgItemText(hwndMain, IDOK, "&Pack");

  GetLocalTime(&StopTime);
  StatusMessage("Stop time: %02d/%02d/%04d  %02d:%02d:%02d", 
    StopTime.wMonth, StopTime.wDay, StopTime.wYear,
    StopTime.wHour, StopTime.wMinute, StopTime.wSecond);
  
  StopTime.wSecond -= StartTime.wSecond;
  if ((short) StopTime.wSecond < 0) {
    StopTime.wMinute--;
    StopTime.wSecond += 60;
  }
  StopTime.wMinute -= StartTime.wMinute;
  if ((short) StopTime.wMinute < 0) {
    StopTime.wHour--;
    StopTime.wMinute += 60;
  }
  StopTime.wHour -= StartTime.wHour;
  if ((short) StopTime.wHour < 0) {
    StopTime.wDay--;
    StopTime.wHour += 24;
  }
  StatusMessage("Duration: %02d:%02d:%02d", 
    StopTime.wHour, StopTime.wMinute, StopTime.wSecond);

  if (Closing || AutoMode)
    PostMessage(hwndMain, WM_CLOSE, 0, 0);
  //if (DumpFile)
  //  fclose(DumpFile);
  //DumpFile = NULL;
  return TRUE;
}

void PackHPIFile(void)
{
	char buff[1024];

  if (!PackHandle) {
	  GetDlgItemText(hwndMain, IDC_DIRNAME, PackDirectory, sizeof(PackDirectory));
	  GetDlgItemText(hwndMain, IDC_HPINAME, SaveName, sizeof(SaveName));

    if (!AutoMode) {
		  if (FileExists(SaveName))	{
			  sprintf(buff, "The file %s already exists.  If you continue, it will be OVERWRITTEN.  Continue?", SaveName);
			  if (MessageBox(hwndMain, buff, szAppTitle, MB_YESNO | MB_ICONWARNING) != IDYES)
				  return;
      }
    }
	
    SendDlgItemMessage(hwndMain, IDC_STATLIST, LB_RESETCONTENT, 0, 0);

	  WritePrivateProfileString(szAppName, "PackDirectory", PackDirectory, ININame);
	  WritePrivateProfileString(szAppName, "SaveName", SaveName, ININame);
	  WritePrivateProfileString(szAppName, "CurrentSaveDirectory", CurrentSaveDirectory, ININame);
		sprintf(buff, "%d", CMethod);
	  WritePrivateProfileString(szAppName, "CompMethod", buff, ININame);
		//sprintf(buff, "%d", CLevel);
	  //WritePrivateProfileString(szAppName, "CompLevel", buff, ININame);
		sprintf(buff, "%d", CProgram);
	  WritePrivateProfileString(szAppName, "CompProgram", buff, ININame);

    PackHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) PackHPIThread, 0, 0, &PackThreadID);
		if (PackHandle) {
			CloseHandle(PackHandle);
		}
  }
  else
		Packing = FALSE;
}

LRESULT CALLBACK HelpAboutProc(HWND hDlg, UINT message, UINT wParam, LONG lParam)
{
  switch (message) {
    case WM_INITDIALOG : return TRUE;
    case WM_COMMAND :
      switch (LOWORD(wParam)) {
        case IDCANCEL :
        case IDOK : EndDialog(hDlg, 0);
                    return TRUE;
      }
                    
  }   
  return FALSE;
}

LRESULT WndProcMainInit(HWND hwnd, UINT message, WPARAM wParam, LONG lParam)
{
  HANDLE hi;
  char *c;
  char parm[1024];
  int index;

  hwndMain = hwnd;

  GetModuleFileName(NULL, ININame, sizeof(ININame));
  c = strrchr(ININame, '.');
  if (c)
    *c = 0;
  strcat(ININame, ".INI");

  hi = LoadIcon(hThisInstance, MAKEINTRESOURCE(IDI_MAIN));
  SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM) hi);

  hi = LoadImage(hThisInstance, MAKEINTRESOURCE(IDI_MAIN), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
  SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM) hi);

  SendMessage(hwnd, WM_SETTEXT, 0, (LPARAM) szAppTitle);

  CProgram = HPI_V2;
	GetTADirectory(CurrentSaveDirectory);
  if (CurrentSaveDirectory[0])
    CProgram = HPI_V1;
  else {
    GetTAKDirectory(CurrentSaveDirectory);
    if (CurrentSaveDirectory[0])
      CProgram = HPI_V2;
  }


	GetPrivateProfileString(szAppName, "PackDirectory", "", PackDirectory, sizeof(PackDirectory), ININame);
	GetPrivateProfileString(szAppName, "SaveName", "", SaveName, sizeof(SaveName), ININame);
	GetPrivateProfileString(szAppName, "CurrentSaveDirectory", "", CurrentSaveDirectory, sizeof(CurrentSaveDirectory), ININame);
	CMethod = GetPrivateProfileInt(szAppName, "CompMethod", CMethod, ININame);
	CProgram = GetPrivateProfileInt(szAppName, "CompProgram", CProgram, ININame);
	CLevel = GetPrivateProfileInt(szAppName, "CompLevel", CLevel, ININame);

	index = SendDlgItemMessage(hwndMain, IDC_CPROGRAM, CB_ADDSTRING, 0, (LPARAM) "TA");
	SendDlgItemMessage(hwndMain, IDC_CPROGRAM, CB_SETITEMDATA, index, (LPARAM) HPI_V1);
	index = SendDlgItemMessage(hwndMain, IDC_CPROGRAM, CB_ADDSTRING, 0, (LPARAM) "TA:K");
	SendDlgItemMessage(hwndMain, IDC_CPROGRAM, CB_SETITEMDATA, index, (LPARAM) HPI_V2);

  SetComboBox(hwndMain, IDC_CPROGRAM, CProgram);

  SetMethodCombo(CProgram);

  SetComboBox(hwndMain, IDC_CMETHOD, CMethod);

  if (CommandLine && *CommandLine) {
    c = CommandLine;
    while (*c) {
      c = GetCommandParameter(c, parm);
      if (stricmp(parm, "-d") == 0) {
        c = GetCommandParameter(c, parm);
        strcpy(PackDirectory, parm);
      }
      else if (stricmp(parm, "-f") == 0) {
        c = GetCommandParameter(c, parm);
        strcpy(SaveName, parm);
      }
      else if (stricmp(parm, "auto") == 0)
        AutoMode = TRUE;
      else {
        MessageBox(hwndMain, "Invalid parameter\r\nCommand line format:\r\nHPIPack [-d DirectoryToPack] [-f FileToCreate] [auto]", parm, MB_OK);
        AutoMode = FALSE;
        break;
      }
    }
  }

	SetDlgItemText(hwndMain, IDC_DIRNAME, PackDirectory);
	SetDlgItemText(hwndMain, IDC_HPINAME, SaveName);

  if (AutoMode)
    PostMessage(hwndMain, WM_COMMAND, IDOK, 0);

	return TRUE;
}

LRESULT WndProcMainCommand(HWND hwnd, UINT message, WPARAM wParam, LONG lParam)
{
  UINT idItem = LOWORD(wParam);
  UINT wNotifyCode = HIWORD(wParam);
  HWND hwndCtl = (HWND) lParam;
  int index;

  switch (idItem) {
		case IDM_FILEEXIT :
		case IDCANCEL :
      PostMessage(hwnd, WM_CLOSE, 0, 0);
      return 0;
		case IDC_BROWSE :
			BrowseForDirectory();
			return 0;
		case IDC_HPIBROWSE :
	    GetDlgItemText(hwndMain, IDC_HPINAME, SaveName, sizeof(SaveName));
			OpenSaveFile(SaveName, HPIFilter, "ufo");
			return 0;
		case IDC_CPROGRAM :
			index = SendDlgItemMessage(hwndMain, IDC_CPROGRAM, CB_GETCURSEL, 0, 0);
      CProgram = SendDlgItemMessage(hwndMain, IDC_CPROGRAM, CB_GETITEMDATA, index, 0);
      SetMethodCombo(CProgram);
      SetComboBox(hwndMain, IDC_CMETHOD, CMethod);
      // drop thru
		case IDC_CMETHOD :
			index = SendDlgItemMessage(hwndMain, IDC_CMETHOD, CB_GETCURSEL, 0, 0);
      CMethod = SendDlgItemMessage(hwndMain, IDC_CMETHOD, CB_GETITEMDATA, index, 0);
			return 0;
		case IDOK :
			PackHPIFile();
			return 0;
		case IDM_HELPABOUT :
      DialogBox(hThisInstance, MAKEINTRESOURCE(IDD_ABOUT), hwndMain, HelpAboutProc);
      return 0;
  }
  return FALSE;
}

LRESULT WndProcMainNotify(HWND hwnd, UINT message, WPARAM wParam, LONG lParam)
{
  // Process WM_NOTIFY messages

  LPNMHDR pnmh = (LPNMHDR) lParam;
  UINT idItem = pnmh->idFrom;
  
  switch (idItem) {
    case IDC_DIRTREE : 
      //return NotifyHPITree(pnmh->hwndFrom, pnmh->code, idItem, pnmh);
			break;
  }
  return FALSE;
}

LRESULT WndProcMainClose(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  if (PackHandle) {
    Packing = FALSE;
    Closing = TRUE;
    return 0;
  }
	DestroyWindow(hwnd);
  hwndMain = NULL;
  PostQuitMessage(0);
  if (ProgHeap)
    HeapDestroy(ProgHeap);
  ProgHeap = NULL;
  return FALSE;
}

LRESULT CALLBACK WndProcMain(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message) {
    case WM_INITDIALOG :
      return WndProcMainInit(hwnd, message, wParam, lParam);
    case WM_COMMAND :
      return WndProcMainCommand(hwnd, message, wParam, lParam);
//    case WM_NOTIFY : 
//			return WndProcMainNotify(hwnd, message, wParam, lParam);
//		case WM_SIZE :
//			return WndProcMainSize(hwnd, message, wParam, lParam);
    case WM_CLOSE :
      return WndProcMainClose(hwnd, message, wParam, lParam);
  }   
  return FALSE;
}

int PASCAL WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPSTR lpszCmdLine, int nCmdShow)
{
  MSG	  msg;

  hThisInstance = hInstance;

  CommandLine = lpszCmdLine;

  hwndMain = CreateDialog(hThisInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, WndProcMain);

  if (!hwndMain) {
    MessageBox(NULL, "Can't create dialog box", szAppTitle, MB_OK);
    return FALSE;
  }
    
  while (GetMessage(&msg, NULL, 0, 0)) 
    if ((!hwndMain) || !IsDialogMessage(hwndMain, &msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  return (msg.wParam);
}
