#ifdef _WIN32
#include <process.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define EXE(X) X ".exe"
#else
#define EXE(X) X
#endif

static void usage() {
  fprintf(stderr, "just call 'build' without arguments\n");
}

static int run(char ** args) {
  assert(args && args[0]);

#ifdef _WIN32
  if (0 == _spawnvp(_P_WAIT, args[0], (const char * const *)args)) {
    return 0;
  }
#else
#error TODO: implement fork+execv
#endif

  fprintf(stderr, "failed to run child process\n");
  return 1;
}

int main(int argc, char ** argv) {
  if (argc != 1) return (usage(), 1);

  // TODO: support other compilers/platforms
  char * args[] = {
    EXE("clang"), "-Wall", "-o", EXE("syncloo"), "syncloo.c", 0
  };
  if (run(args)) return 1;

  return 0;
}
