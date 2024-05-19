#include "libc/calls/calls.h"
#include "libc/errno.h"
#include "libc/log/bsd.h"
#include "libc/runtime/runtime.h"
#include "libc/stdio/rand.h"
#include "libc/stdio/readpassphrase.h"
#include "libc/stdio/stdio.h"
#include "libc/str/blake2.h"
#include "libc/str/str.h"
#include "libc/sysv/consts/o.h"
#include "third_party/argon2/argon2.h"

int main(int argc, char *argv[]) {
  FILE *f;
  char *p, *h;
  uint32_t ctr;
  int i, r, n, done, fd, dfd, lop;
  char has[1024], pwd[1024], sel[8];

  if ((h = getenv("XDG_CONFIG_HOME"))) {
    if (-1 == (dfd = open(h, O_DIRECTORY))) {
      err(2, "open");
    }
  } else if ((h = getenv("HOME"))) {
    if (-1 == (fd = open(h, O_DIRECTORY))) {
      err(2, "open");
    }
    if (-1 == (dfd = openat(fd, ".config", O_DIRECTORY))) {
      err(2, "openat");
    }
    close(fd);
  } else {
    errx(2, "missing home");
  }
  lop = 0;
restart:
  fd = openat(dfd, "pwtoy", O_DIRECTORY);
  if (fd == -1) {
    if (errno != ENOENT) {
      err(2, "openat");
    }
    if (0 != mkdirat(dfd, "pwtoy", 0777)) {
      err(2, "mkdirat");
    }
    if (lop++) {
      errx(2, "mkdir/open loop");
    }
    goto restart;
  }
  close(dfd);
  dfd = fd;
  if ((fd = openat(dfd, "master.argon2", O_RDWR | O_CREAT, 0777)) == -1) {
    err(2, "openat");
  }
  close(dfd);

  if (!(f = fdopen(fd, "r+"))) {
    err(2, "fdopen");
  }

  if (!(n = fread(has, 1, sizeof(has) - 1, f))) {
    if (ferror(f)) {
      err(2, "fread");
    }
  }
  has[n] = 0;

  lop = 0;
  done = 0;
  do {
    if (lop) {
      fprintf(stderr, "Password does not match.\n");
    } else {
      ++lop;
    }
    if (!(p = readpassphrase("> ", pwd, sizeof(pwd), 0))) {
      err(2, "readpassphrase");
    }
    if (n) {
      r = argon2d_verify(has, p, strlen(p));
      if (r != ARGON2_OK && r != ARGON2_VERIFY_MISMATCH) {
        errx(2, "argon2d_verify: %s", argon2_error_message(r));
      }
      done = r == ARGON2_OK;
    } else {
      strlcpy(has, p, sizeof(has));
      if (!(p = readpassphrase("conf> ", pwd, sizeof(pwd), 0))) {
        err(2, "readpassphrase");
      }
      done = !strcmp(p, has);
    }
  } while (!done);

  if (!n) {
    if (0 != getentropy(sel, sizeof(sel))) {
      err(2, "getentropy");
    }
    /* We only store a 32-bit hash to disk; since its only purpose is to detect
       mistypes, a 1-in-4-billion false positive rate is acceptable. */
    r = argon2d_hash_encoded(3, 1 << 13, 4, p, strlen(p), sel, sizeof(sel), 4,
                             has, sizeof(has));
    if (r != ARGON2_OK) {
      errx(2, "argon2d_hash_encoded: %s", argon2_error_message(r));
    }
    if (0 != fseek(f, 0, SEEK_SET)) {
      err(2, "fseek");
    }
    if (1 != fwrite(has, strlen(has), 1, f)) {
      err(2, "fwrite");
    }
  }

  fclose(f);

  bzero(sel, sizeof(sel));

  /* TODO include metadata in the input to this hash */
  r = argon2d_hash_raw(4, 1 << 16, 4, p, strlen(p), sel, sizeof(sel), has, 32);
  if (r != ARGON2_OK) {
    errx(2, "argon2d_hash_raw: %s", argon2_error_message(r));
  }
  for (ctr = 0; ctr < 3; ++ctr) {
    *(uint32_t *)(has + 8) ^= ctr;
    BLAKE2B256(has, 32, (uint8_t *)pwd);
    *(uint32_t *)(has + 8) ^= ctr;
    for (i = 0; i < 32; ++i) {
      printf("%02hhx", pwd[i]);
    }
    putchar(' ');
    fflush(stdout);
  }
  puts("");

  return 0;
}
