#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define _CRT_SECURE_NO_WARNINGS
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PATH_SEP "/"

#if 0
#  define DEBUG_PROTOCOL(...) fprintf(stderr, __VA_ARGS__);
#else
#  define DEBUG_PROTOCOL(...)
#endif

static void usage(const char * argv0) {
  fprintf(stderr,
      "\n"
      "usage: \n"
      "    %s --from <path> --to <path>\n"
      "\n",
      argv0);
  abort();
}

static void read_eol() {
  char lf = 0;
  assert(1 == fread(&lf, 1, 1, stdin));
  if (lf == '\r') assert(1 == fread(&lf, 1, 1, stdin));
  assert(lf == '\n');
}
static void read_id(const char buf[4]) {
  char id[4] = { 0 };
  assert(1 == fread(id, 4, 1, stdin));
  assert(0 == strncmp(id, buf, 4));
}
static uint64_t read_u64(int len) {
  assert(len <= 8);

  char buf[9] = { 0 };
  assert(1 == fread(buf, len, 1, stdin));

  char * end = 0;
  uint64_t res = strtoll(buf, &end, 16);
  assert(end && *end == 0);
  assert(len);

  return res;
}
static char * read_filename(const char * root) {
  uint64_t len = read_u64(3);

  int root_len = strlen(root);
  char * buf = calloc(len + root_len + 2, 1);
  strcpy(buf, root);
  strcat(buf, "/");
  assert(len == fread(buf + root_len + 1, 1, len, stdin));
  // TODO: replace with a check if realpath is inside root
  assert(0 == strstr(buf, ".."));
  return buf;
}

#define write_message(...)        \
  assert(printf(__VA_ARGS__));    \
  assert(0 == fflush(stdout));    \
  DEBUG_PROTOCOL(__VA_ARGS__);

static void recurse_files(const char * root, const char * path);

static _Bool file_attrs(const char * path, uint64_t * mtime, uint64_t * fsize, _Bool * isdir) {
#ifdef _WIN32
  WIN32_FILE_ATTRIBUTE_DATA attrs = { 0 };
  if (!GetFileAttributesExA(path, GetFileExInfoStandard, &attrs)) return 0;

  if (isdir) *isdir = attrs.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;

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
  if (mtime) *mtime = mktime(&tmt);

  ULARGE_INTEGER ul = { 0 };
  ul.HighPart = attrs.nFileSizeHigh;
  ul.LowPart = attrs.nFileSizeLow;
  if (fsize) *fsize = ul.QuadPart;
#else
  struct stat st = { 0 };
  if (0 != stat(path, &st)) return 0;
  if (mtime) *mtime = st.st_mtimespec.tv_sec;
  if (fsize) *fsize = st.st_size;
  if (isdir) *isdir = st.st_mode & S_IFDIR;
#endif

  return 1;
}

static uint32_t crc_table[256];
static void crc_init() {
  for (int n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++) {
      if (c & 1) c = 0xedb88320 ^ (c >> 1);
      else c >>= 1;
    }
    crc_table[n] = c;
  }
}
static uint32_t crc_step(uint32_t crc, unsigned char * buf, unsigned len) {
  uint32_t c = crc ^ ~0U;
  for (int n = 0; n < len; n++) {
    int idx = (c ^ buf[n]) & 0xFF;
    c = crc_table[idx] ^ (c >> 8);
  }
  return c ^ ~0U;
}
static uint32_t crc_file(const char * path) {
  FILE * f = fopen(path, "rb");
  if (!f) return 0;

  uint32_t crc = 0;
  while (!feof(f)) {
    unsigned char buf[1024];
    int rd = fread(buf, 1, 1024, f);
    if (rd > 0) crc = crc_step(crc, buf, rd);
  }

  fclose(f);

  return crc;
}

static void process_path(const char * root, const char * parent, const char * file, _Bool is_dir) {
  if (strcmp(".",  file) == 0) return;
  if (strcmp("..", file) == 0) return;

  int fpath_len = strlen(parent) + strlen(file) + 1;
  char * fullpath = malloc(fpath_len + 1);
  strcpy(fullpath, parent);
  strcat(fullpath, PATH_SEP);
  strcat(fullpath, file);
  assert(0 == strncmp(root, parent, strlen(root)));

  const char * rel_file = fullpath + strlen(root) + 1;
  unsigned rel_flen = strlen(rel_file);

  if (is_dir) {
    write_message("MKDR%03x%s\n", rel_flen, rel_file);

    read_id("mkdr");
    read_eol();

    recurse_files(root, fullpath);
    return;
  }

  // Check remote file mod time

  write_message("MTIM%03x%s\n", rel_flen, rel_file);

  read_id("mtim");
  uint64_t mtime = read_u64(8);
  read_eol();

  uint64_t loc_mtime = 0;
  uint64_t loc_fsize = 0;
  assert(file_attrs(fullpath, &loc_mtime, &loc_fsize, 0));
  if (mtime > loc_mtime) return;

  FILE * f = fopen(fullpath, "rb");
  assert(f);

  write_message("DATA%08llx%03x%s\n", loc_fsize, rel_flen, rel_file);

  uint32_t crc = 0;
  while (loc_fsize > 0) {
    unsigned char buf[1024];
    uint64_t n = loc_fsize > 1024 ? 1024 : loc_fsize;
    assert(1 == fread(buf, n, 1, f));
    assert(1 == fwrite(buf, n, 1, stdout));
    crc = crc_step(crc, buf, n);
    loc_fsize -= n;
  }
  printf("\n");
  assert(0 == fflush(stdout));

  fclose(f);

  read_id("data");
  read_eol();

  write_message("CR32%03x%s\n", rel_flen, rel_file);

  read_id("cr32");
  assert((uint64_t)crc == read_u64(8));
  read_eol();
}

static void recurse_files(const char * root, const char * path) {
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
    process_path(root, path, ffd.cFileName, is_dir);
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
    process_path(root, path, de->d_name, de->d_type == DT_DIR);
  }
  closedir(dir);
#endif
}

static void receive_files(const char * root) {
  _Bool isdir = 0;
  file_attrs(root, 0, 0, &isdir);
  assert(isdir && "target path is not a directory");

  while (!feof(stdin)) {
    char id[4] = { 0 };
    if (1 != fread(id, 4, 1, stdin)) return;

    if (0 == strncmp(id, "MKDR", 4)) {;
      char * fname = read_filename(root);
      read_eol();

      assert(0 == mkdir(fname, 0777) || errno == EEXIST);

      write_message("mkdr\n");

      free(fname);
    } else if (0 == strncmp(id, "MTIM", 4)) {;
      char * fname = read_filename(root);
      read_eol();

      uint64_t loc_mtime = 0;
      file_attrs(fname, &loc_mtime, 0, 0);

      write_message("mtim%08llx\n", loc_mtime);

      free(fname);
    } else if (0 == strncmp(id, "DATA", 4)) {
      uint64_t data_len  = read_u64(8);
      char * fname = read_filename(root);
      read_eol();

      FILE * out = fopen(fname, "wb");
      assert(out);

      while (data_len > 0) {
        char buf[1024];
        uint64_t n = data_len > 1024 ? 1024 : data_len;
        assert(1 == fread(buf, n, 1, stdin));
        assert(1 == fwrite(buf, n, 1, out));
        data_len -= n;
      }
      read_eol();

      fclose(out);
      free(fname);

      write_message("data\n");
    } else if (0 == strncmp(id, "CR32", 4)) {
      char * fname = read_filename(root);
      read_eol();

      write_message("cr32%08x\n", crc_file(fname));
    } else {
      assert(0 && "invalid code received");
    }
  }
}

static int pipe_from_to(char * argv0, char * from, char * to) {
  assert(from && *from && to && *to);

#ifdef _WIN32
  SECURITY_ATTRIBUTES attr = { 0 };
  attr.nLength = sizeof(SECURITY_ATTRIBUTES);
  attr.bInheritHandle = TRUE;

  HANDLE h[4] = { 0 };
  assert(CreatePipe(h[0], h[1], &attr, 64 * 1024));
  assert(CreatePipe(h[2], h[3], &attr, 64 * 1024));

  STARTUPINFO si = { 0 };
  si.cb = sizeof(STARTUPINFO);
  si.dwFlags = STARTF_USESTDHANDLES;

  unsigned sz = snprintf(NULL, 0, "%s --from %s", argv0, from) + 1;
  char * cmdline = malloc(sz);
  sprintf(cmdline, "%s --from %s", argv0, from);

  PROCESS_INFORMATION pi_f = { 0 };
  si.hStdOutput = h[1];
  si.hStdInput = h[2];
  assert(CreateProcess(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi_f));

  free(cmdline);

  sz = snprintf(NULL, 0, "%s --to %s", argv0, to) + 1;
  cmdline = malloc(sz);
  sprintf(cmdline, "%s --from %s", argv0, to);

  PROCESS_INFORMATION pi_t = { 0 };
  si.hStdOutput = h[3];
  si.hStdInput = h[0];
  assert(CreateProcess(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi_t));

  free(cmdline);

  CloseHandle(pi_f.hThread);
  CloseHandle(pi_t.hThread);
  for (int i = 0; i < 4; i++) CloseHandle(h[i]);

  DWORD ext_f = STILL_ACTIVE;
  WaitForSingleObject(pi_f.hProcess);
  GetExitCodeProcess(pi_f.hProcess, &ext_f);
  CloseHandle(pi_f.hProcess);

  DWORD ext_t = STILL_ACTIVE;
  WaitForSingleObject(pi_t.hProcess);
  GetExitCodeProcess(pi_f.hProcess, &ext_t);
  CloseHandle(pi_t.hProcess);

  if (ext_f == 0 && ext_t == 0) return;
#else
  int from_to_fd[2];
  assert(0 == pipe(from_to_fd));
  assert(from_to_fd[0] && from_to_fd[1]);

  int to_from_fd[2];
  assert(0 == pipe(to_from_fd));
  assert(to_from_fd[0] && to_from_fd[1]);

  pid_t from_p = fork();
  if (from_p == 0) {
    close(from_to_fd[0]); dup2(from_to_fd[1], 1);
    close(to_from_fd[1]); dup2(to_from_fd[0], 0);

    char * args[] = { argv0, "--from", from, 0 };
    execv(argv0, args);
    abort();
  } else if (from_p > 0) {
    pid_t to_p = fork();
    if (to_p == 0) {
      close(from_to_fd[1]); dup2(from_to_fd[0], 0);
      close(to_from_fd[0]); dup2(to_from_fd[1], 1);

      char * args[] = { argv0, "--to", to, 0 };
      execv(argv0, args);
      abort();
    } else if (to_p > 0) {
      close(from_to_fd[0]); close(from_to_fd[1]);
      close(to_from_fd[0]); close(to_from_fd[1]);

      // Note: both sides should exit on their own when their respective inputs
      // are closed
      int sl = 0;
      assert(0 <= waitpid(from_p, &sl, 0));
      if (WIFEXITED(sl) && 0 == WEXITSTATUS(sl)) {
        assert(0 <= waitpid(to_p, &sl, 0));
        if (WIFEXITED(sl) && 0 == WEXITSTATUS(sl)) return 0;
      }
    }
  }
#endif

  fprintf(stderr, "failed to run child process\n");
  return 1;
}

int main(int argc, char ** argv) {
  // Avoids CRLF conversions on windows-like
  freopen(NULL, "rb", stdin);
  freopen(NULL, "wb", stdout);

  crc_init();

  if (argc == 3 && 0 == strcmp("--from", argv[1])) {
    recurse_files(argv[2], argv[2]);
    return 0;
  }
  if (argc == 3 && 0 == strcmp("--to", argv[1])) {
    receive_files(argv[2]);
    return 0;
  }

  if (argc == 5 && 0 == strcmp("--from", argv[1]) && 0 == strcmp("--to", argv[3])) {
    return pipe_from_to(argv[0], argv[2], argv[4]);
  }

  usage(argv[0]);
  return 0;
}
