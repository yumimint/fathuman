#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "fatfs/diskio.h"
#include "fatfs/ff.h"

#undef perror
#define perror(msg) fprintf(stderr, "%s: %s\n", msg, strerror(errno))

FILE* xdfp;
struct stat xdfst;
int offset = 0;

typedef void (*filinfo_callback_t)(const FILINFO* fno, const char* path);
struct tm get_fattime(const FILINFO* fi);

void report(const FILINFO* fno, const char* path) {
  char timebuf[100];
  struct tm t = get_fattime(fno);
  static const char* dtfmt = "%Y-%m-%d %H:%M:%S";

  size_t n = strftime(timebuf, sizeof(timebuf), dtfmt, &t);
  if (n == 0) {
    perror("strftime");
    fprintf(stderr, "struct tm {%d, %d, %d, %d, %d, %d}\n", t.tm_year, t.tm_mon,
            t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    if (t.tm_mon < 0) t.tm_mon = 0;
    if (t.tm_mday < 1) t.tm_mday = 1;
    strftime(timebuf, sizeof(timebuf), dtfmt, &t);
  }

  char fnamebuf[512];
  snprintf(fnamebuf, sizeof fnamebuf, "%s%s%s", path, *path ? "/" : "",
           fno->fname);

  printf("-rw-rw-rw- 1 %d %d %8lu %s %s\n", xdfst.st_uid, xdfst.st_gid,
         fno->fsize, timebuf, fnamebuf);
}

DSTATUS
disk_initialize(BYTE pdrv) { return FR_OK; }

DSTATUS
disk_status(BYTE pdrv) { return FR_OK; }

static const BYTE bios_parameter_block[] = {
    // BytsPerSec: Sector size [byte] (2)
    0x00,
    0x04,
    // SecPerClus: Cluster size [sector] (1)
    0x01,
    // RsvdSecCnt: Size of reserved area [sector] (2)
    0x01,
    0x00,
    // NumFATs: Number of FAT copies (1)
    0x02,
    // RootEntCnt: Number of root directory entries for FAT12/16 (2)
    0xC0,
    0x00,
    // TotSec16: Volume size [sector] (2)
    0xD0,
    0x04,
    // Media: Media descriptor (1)
    0xFE,
    // FATSz16: FAT size [sector] (2)
    0x02,
    0x00,
    // SecPerTrk: Track size [sector] (2)
    0x08,
    0x00,
    // NumHeads: Number of heads (2)
    0x02,
    0x00,
    // HiddSec: Number of special hidden sectors (4)
    0x00,
    0x00,
    0x00,
    0x00,
    // TotSec32: Volume size [sector] (4)
    0x00,
    0x00,
    0x00,
    0x00,
};

DRESULT
disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count) {
  fseek(xdfp, sector * _MIN_SS + offset, SEEK_SET);
  fread(buff, _MIN_SS, count, xdfp);

  if (sector == 0) {
    const int BPB_BytsPerSec = 11;
    memcpy(buff + BPB_BytsPerSec, bios_parameter_block,
           sizeof bios_parameter_block);
  }

  return 0;
}

struct tm get_fattime(const FILINFO* fi) {
  struct tm t;
  memset(&t, 0, sizeof t);
  // yyyyyyy mmmm ddddd
  t.tm_year = 80 + ((fi->fdate >> 9) & 0x7f);
  t.tm_mon = ((fi->fdate >> 5) & 0x0f) - 1;
  t.tm_mday = fi->fdate & 0x1f;
  // hhhhh mmmmmm sssss
  t.tm_hour = (fi->ftime >> 11) & 0x1f;
  t.tm_min = (fi->ftime >> 5) & 0x3f;
  t.tm_sec = (fi->ftime & 0x1f) << 1;
  return t;
}

const char* f_errstr(FRESULT r) {
  static const char* errstrs[] = {
      "Succeeded",
      "A hard error occurred in the low level disk I/O layer",
      "Assertion failed",
      "The physical drive cannot work",
      "Could not find the file",
      "Could not find the path",
      "The path name format is invalid",
      "Access denied due to prohibited access or directory full",
      "Access denied due to prohibited access",
      "The file/directory object is invalid",
      "The physical drive is write protected",
      "The logical drive number is invalid",
      "The volume has no work area",
      "There is no valid FAT volume",
      "The f_mkfs() aborted due to any parameter error",
      "Could not get a grant to access the volume within defined period",
      "The operation is rejected according to the file sharing policy",
      "LFN working buffer could not be allocated",
      "Number of open files > _FS_SHARE",
      "Given parameter is invalid",
  };
  if (r >= 19 && r < 0) return "Unknown error";
  return errstrs[r];
}

#define ERR_WRAP(fx)                                                          \
  {                                                                           \
    FRESULT fr;                                                               \
    fr = (fx);                                                                \
    if (fr) {                                                                 \
      printf("%s:%d: Error %d calling %s: %s\n", __FILE__, __LINE__, fr, #fx, \
             f_errstr(fr));                                                   \
      exit(1);                                                                \
    }                                                                         \
  }

/**
 * This is a countermeasure against cases where data has been tampered with and
 * a circular structure has been created in the directory entries. Normally,
 * this check is unnecessary.
 */
bool check_visited_dir(const DIR* dp) {
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

void scan_files(
    char* path, /* Start node to be scanned (also used as work area) */
    filinfo_callback_t func) {
  FRESULT res;
  DIR dir;
  int i;
  char* fn; /* This function is assuming non-Unicode cfg. */

  res = f_opendir(&dir, path); /* Open the directory */
  if (res != FR_OK) {
    printf("%s (%d): %s\n", f_errstr(res), res, path);
    return;
  }
  if (check_visited_dir(&dir)) {
    printf("%d %d %d [%s]\n", dir.clust, dir.sect, dir.index, path);
    f_closedir(&dir);
    return;
  }

  i = strlen(path);
  for (;;) {
    FILINFO fno;
    res = f_readdir(&dir, &fno); /* Read a directory item */

    if (res != FR_OK || fno.fname[0] == 0)
      break; /* Break on error or end of dir */

    if (fno.fattrib & AM_VOL) continue; /* Ignore volume entry */

    if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0)
      continue; /* Ignore dot entry */

    if (fno.fattrib & AM_DIR) { /* It is a directory */
      sprintf(&path[i], "%s%s", i == 0 ? "" : "/", fno.fname);
      scan_files(path, func);
      path[i] = 0;
    } else { /* It is a file. */
      func(&fno, path);
    }
  }
  f_closedir(&dir);
}

int copyout(const char* target, const char* out) {
  int exitcode = EXIT_SUCCESS;
  FIL fp;
  ERR_WRAP(f_open(&fp, target, FA_READ));

  FILE* f = NULL;
  BYTE* buf = malloc(fp.fsize);
  if (!buf) {
    perror("malloc");
    exitcode = EXIT_FAILURE;
    goto bailout;
  }

  UINT r;
  ERR_WRAP(f_read(&fp, buf, fp.fsize, &r));
  ERR_WRAP(f_close(&fp));

  f = fopen(out, "wb");
  if (!f) {
    perror(out);
    exitcode = EXIT_FAILURE;
    goto bailout;
  }
  if (fp.fsize > 0) {
    if (fwrite(buf, fp.fsize, 1, f) != 1) {
      perror("fwrite");
      exitcode = EXIT_FAILURE;
      goto bailout;
    }
  }

bailout:
  if (f) fclose(f);
  free(buf);
  return exitcode;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <action> <file> [<arguments>]\n", argv[0]);
    fprintf(stderr,
            "Actions:\n\tlist     - List all the files "
            "(recursively)\n\tcopyout - "
            "extract one file, specify as third argument\n");
    return EXIT_FAILURE;
  }
  xdfp = fopen(argv[2], "rb");
  if (!xdfp) {
    perror(argv[2]);
    return EXIT_FAILURE;
  }

  fstat(fileno(xdfp), &xdfst);

  {
    unsigned char dim_head[256];
    fread(dim_head, 256, 1, xdfp);
    if (!strncmp(dim_head + 0xab, "DIFC HEADER", 11)) offset = 256;
  }

  int exitcode = EXIT_SUCCESS;

  FATFS fs;
  ERR_WRAP(f_mount(&fs, "", 1));

  if (!strcmp(argv[1], "list")) {
    char scan_path[512];
    if (argc > 4) strcpy(scan_path, argv[3]);
    scan_files(scan_path, report);
    check_visited_dir(NULL); /* release work memory */
  } else if (!strcmp(argv[1], "copyout")) {
    if (argc < 5) {
      fprintf(stderr, "Usage: %s copyout <imgfile> <file> <outfile>\n",
              argv[0]);
      return EXIT_FAILURE;
    }
    exitcode = copyout(argv[3], argv[4]);
  }

  ERR_WRAP(f_mount(NULL, "", 0));
  fclose(xdfp);

  return exitcode;
}
