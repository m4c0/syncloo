#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define _CRT_SECURE_NO_WARNINGS
#  include <windows.h>
#else
#  include <dirent.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PATH_SEP "/"

static void usage() {
  fprintf(stderr, "usage: ...");
  abort();
}

static void recurse_files(const char * path);

static void process_path(const char * parent, const char * file, _Bool is_dir) {
  if (strcmp(".",  file) == 0) return;
  if (strcmp("..", file) == 0) return;

  char * fullpath = malloc(strlen(parent) + strlen(file) + 2);
  strcpy(fullpath, parent);
  strcat(fullpath, PATH_SEP);
  strcat(fullpath, file);

  if (is_dir) {
    recurse_files(fullpath);
  } else {
    puts(fullpath);
  }
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
