/**
 * @brief An interactive interface for dim2zip.py.
 * @file interact.c
 * Interaction uses standard input/output.
 * It first provides a list of files,
 * and then transitions to file extraction mode.
 * In extraction mode, it accepts the target name and outputs its contents.
 */
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fatfs/ff.h"

typedef void (*filinfo_callback_t)(const FILINFO* fno, const char* path);
extern const char* f_errstr(FRESULT r);

#undef perror
#define perror(msg) fprintf(stderr, "%s: %s\n", msg, strerror(errno))


/**
 *
 */
static struct tm get_fattime(const FILINFO* fi) {
  struct tm t;
  memset(&t, 0, sizeof t);
  t.tm_year = 80 + ((fi->fdate >> 9) & 0x7f);
  t.tm_mon = ((fi->fdate >> 5) & 0x0f) - 1;
  t.tm_mday = fi->fdate & 0x1f;
  t.tm_hour = (fi->ftime >> 11) & 0x1f;
  t.tm_min = (fi->ftime >> 5) & 0x3f;
  t.tm_sec = (fi->ftime & 0x1f) << 1;
  return t;
}

/**
 * This is a countermeasure against cases where data has been tampered with and
 * a circular structure has been created in the directory entries. Normally,
 * this check is unnecessary.
 */
static bool check_visited_dir(const DIR* dp) {
  static uint32_t* visit = NULL;
  static int capacity = 0;
  static int idx = 0;
  if (dp == NULL) {
    free(visit);
    visit = NULL;
    capacity = 0;
    idx = 0;
    return false;
  }

  uint32_t v = (dp->clust << 16) | dp->sect;
  for (int i = 0; i < idx; i++) {
    if (visit[i] == v) return true;
  }
  if (idx == capacity) {
    if (capacity == 0)
      capacity = 256;
    else
      capacity *= 2;
    visit = realloc(visit, sizeof(visit[0]) * capacity);
    assert(visit);
  }
  visit[idx++] = v;
  return false;
}

/**
 * @param path  Start node to be scanned (also used as work area)
 * @param func
 */
static void scan_files(char* path, filinfo_callback_t func) {
  FRESULT res;
  DIR dir;

  res = f_opendir(&dir, path); /* Open the directory */
  if (res != FR_OK) {
    fprintf(stderr, "%s (%d): %s\n", f_errstr(res), res, path);
    return;
  }

  if (check_visited_dir(&dir)) {
    fprintf(stderr, "multiple referenced entry %d %d %d [%s]\n", dir.clust, dir.sect, dir.index, path);
    f_closedir(&dir);
    return;
  }

  const int i = strlen(path);
  for (;;) {
    FILINFO fno;
    res = f_readdir(&dir, &fno); /* Read a directory item */

    if (res != FR_OK || fno.fname[0] == 0)
      break; /* Break on error or end of dir */

    func(&fno, path);

    if (fno.fattrib & AM_DIR) { /* It is a directory */
      if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0) continue;
      sprintf(&path[i], "%s%s", i == 0 ? "" : "/", fno.fname);
      scan_files(path, func);
      path[i] = 0;
    }
  }

  f_closedir(&dir);
}

/**
 *
 */
static void report(const FILINFO* fno, const char* path) {
  static const char* dtfmt = "%Y-%m-%d %H:%M:%S";
  char timebuf[64];
  struct tm t = get_fattime(fno);

  size_t n = strftime(timebuf, sizeof(timebuf), dtfmt, &t);
  if (n == 0) {
    perror("strftime");
    fprintf(stderr, "struct tm {%d, %d, %d, %d, %d, %d}\n", t.tm_year, t.tm_mon,
            t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    if (t.tm_mon < 0) t.tm_mon = 0;
    if (t.tm_mday < 1) t.tm_mday = 1;
    strftime(timebuf, sizeof(timebuf), dtfmt, &t);
  }

  char namebuf[512];
  snprintf(namebuf, sizeof namebuf, "%s%s%s", path, *path ? "/" : "",
           fno->fname);

  char flags[8] = {"----rw-"};
  if (fno->fattrib & AM_VOL) flags[0] = 'v';
  if (fno->fattrib & 0x40) flags[0] = 'l';
  if (fno->fattrib & AM_DIR) flags[0] = 'd';
  if (fno->fattrib & AM_ARC) flags[1] = 'a';
  if (fno->fattrib & AM_SYS) flags[2] = 's';
  if (fno->fattrib & AM_HID) flags[3] = 'h';
  if (fno->fattrib & AM_RDO) flags[5] = '-';
  if (fno->fattrib & 0x80) flags[6] = 'x';

  printf("%s %" PRIu32 " %s %s\n", flags, fno->fsize, timebuf, namebuf);
}

/**
 *
 */
int interactmode() {
  char buf[1024];
  setbuf(stdout, NULL);

  /* provides a list of files */
  buf[0] = 0;
  scan_files(buf, report);
  check_visited_dir(NULL);
  fputs("-eol-\n", stdout);

  /* file extraction mode */
  while (fgets(buf, sizeof(buf), stdin)) {
    FIL fp;
    FRESULT fr;
    fr = f_open(&fp, buf, FA_READ);
    if (fr != FR_OK) {
      fprintf(stdout, "ng: %s\n", f_errstr(fr));
      continue;
    }
    fputs("ok\n", stdout);
    UINT r = sizeof(buf);
    while (r == sizeof(buf)) {
      fr = f_read(&fp, buf, sizeof(buf), &r);
      assert(fr == FR_OK);
      fwrite(buf, r, 1, stdout);
    }
    fr = f_close(&fp);
    assert(fr == FR_OK);
  }
  return EXIT_SUCCESS;
}
