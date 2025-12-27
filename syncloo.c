#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if __has_include(<dirent.h>)
#include <dirent.h>
#else // !__has_include(<dirent.h>)
#error "missing dirent.h"
#endif // __has_include(<dirent.h>)

#define PATH_SEP "/"

static void usage() {
  fprintf(stderr, "usage: ...");
  abort();
}

typedef struct vstr {
  char * data;
  unsigned len;
} vstr;
void vstr_free(vstr * vs) {
  assert(vs);
  if (vs->data) free(vs->data);
}
void vstr_ensure(vstr * vs, unsigned len) {
  assert(vs);

  if (vs->data && vs->len >= len) return;
  if (vs->data) vstr_free(vs);

  vs->data = malloc(len);
  assert(vs->data);
  vs->len = len;
}

static void list_files(const char * path) {
  assert(path);

  int pathlen = strlen(path);
  assert(pathlen);

  DIR * dir = opendir(path);
  if (!dir) {
    fprintf(stderr, "could not find directory: %s", path);
    abort();
  }

  vstr vs = {0};

  struct dirent * de;
  while ((de = readdir(dir))) {
    const char * n = de->d_name;
    if (strcmp(".",  n) == 0) continue;
    if (strcmp("..", n) == 0) continue;

    vstr_ensure(&vs, de->d_namlen + pathlen + 2);
    strcpy(vs.data, path);
    strcat(vs.data, PATH_SEP);
    strcat(vs.data, de->d_name);

    if (de->d_type == DT_DIR) {
      list_files(vs.data);
    } else {
      puts(vs.data);
    }
  }

  vstr_free(&vs);
  closedir(dir);
}

int main(int argc, char ** argv) {
  if (argc != 1) usage();

  list_files(".");

  return 0;
}
