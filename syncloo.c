#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define _CRT_SECURE_NO_WARNINGS
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PATH_SEP "/"

static void usage() {
  fprintf(stderr, "usage: ...");
  abort();
}

static void recurse_files(const char * path);

static uint64_t file_mtime(const char * path) {
#ifdef _WIN32
  WIN32_FILE_ATTRIBUTE_DATA attrs = { 0 };
  assert(GetFileAttributesExA(path, GetFileExInfoStandard, &attrs));

  // mod-time change depending on underlying file system (NTFS/FAT/etc)
  FILETIME local = { 0 };
  FileTimeToLocalFileTime(&attrs.ftLastWriteTime, &local);
  SYSTEMTIME sys = { 0 };
  FileTimeToSystemTime(&local, &sys);

  struct tm tmt = {0};
  tmt.tm_year  = sys.wYear - 1900;
  tmt.tm_mon   = sys.wMonth - 1;
  tmt.tm_mday  = sys.wDay;
  tmt.tm_hour  = sys.wHour;
  tmt.tm_min   = sys.wMinute;
  tmt.tm_sec   = sys.wSecond;
  tmt.tm_wday  = 0;
  tmt.tm_yday  = 0;
  tmt.tm_isdst = -1;
  return mktime(&tmt);
#else
  struct stat st = { 0 };
  assert(0 == stat(path, &st));
  return st.st_mtimespec.tv_sec;
#endif
}

static void process_path(const char * parent, const char * file, _Bool is_dir) {
  if (strcmp(".",  file) == 0) return;
  if (strcmp("..", file) == 0) return;

  int fpath_len = strlen(parent) + strlen(file) + 2;
  char * fullpath = malloc(fpath_len + 1);
  strcpy(fullpath, parent);
  strcat(fullpath, PATH_SEP);
  strcat(fullpath, file);

  if (is_dir) {
    recurse_files(fullpath);
    return;
  }

  // Check remote file mod time

  assert(printf("MTIM%04x%s\n", fpath_len, fullpath));
  assert(0 == fflush(stdout));

  char id[4] = { 0 };
  assert(1 == fread(id, 4, 1, stdin));
  assert(0 == strncmp(id, "MTIM", 4));

  char num[9] = { 0 };
  assert(1 == fread(num, 8, 1, stdin));
  char * end = 0;
  uint64_t mtime = strtoll(num, &end, 16);
  assert(end && *end == 0);

  char lf = 0;
  assert(1 == fread(&lf, 1, 1, stdin));
  if (lf == '\r') assert(1 == fread(&lf, 1, 1, stdin));
  assert(lf == '\n');

  if (mtime > file_mtime(fullpath)) return;

  printf("DATA%04x%s\n", fpath_len, fullpath);
}

static void recurse_files(const char * path) {
  assert(path);

  int pathlen = strlen(path);
  assert(pathlen);

#ifdef _WIN32
  char * search = malloc(pathlen + 3);
  strcpy(search, path);
  strcat(search, "\\*");

  WIN32_FIND_DATA ffd = { 0 };
  HANDLE h = FindFirstFile(search, &ffd);
  if (h == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "could not find directory: %s", path);
    abort();
  }

  do {
    _Bool is_dir = ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
    process_path(path, ffd.cFileName, is_dir);
  } while (0 != FindNextFile(h, &ffd));

  FindClose(h);
#else
  DIR * dir = opendir(path);
  if (!dir) {
    fprintf(stderr, "could not find directory: %s", path);
    abort();
  }

  struct dirent * de;
  while ((de = readdir(dir))) {
    process_path(path, de->d_name, de->d_type == DT_DIR);
  }
  closedir(dir);
#endif
}

int main(int argc, char ** argv) {
  if (argc != 1) usage();

  recurse_files(".");

  return 0;
}
