#include "libc/calls/calls.h"
#include "libc/errno.h"
#include "libc/log/bsd.h"
#include "libc/paths.h"
#include "libc/runtime/runtime.h"
#include "libc/stdio/rand.h"
#include "libc/stdio/readpassphrase.h"
#include "libc/stdio/stdio.h"
#include "libc/str/blake2.h"
#include "libc/str/str.h"
#include "libc/sysv/consts/at.h"
#include "libc/sysv/consts/fileno.h"
#include "libc/sysv/consts/o.h"
#include "third_party/argon2/argon2.h"

/**
 * Opens the passed path, given as a null-terminated list of path components. If
 * a component names an individual directory and that directory does not exist,
 * then it will be created.
 */
int vopentree(char *p, va_list va) {
  int dd = AT_FDCWD, fd;

  do {
    if (-1 == (fd = openat(dd, p, O_DIRECTORY))) {
      if (errno != ENOENT) {
        err(2, "openat (%s)", p);
      }
      if (0 != mkdirat(dd, p, 0777)) {
        err(2, "mkdirat (%s)", p);
      }
      if (-1 == (fd = openat(dd, p, O_DIRECTORY))) {
        err(2, "openat (%s)", p);
      }
    }
    close(dd);
    dd = fd;
  } while ((p = va_arg(va, char *)));
  return dd;
}

int opentree(char *p, ...) {
  int rc;
  va_list va;

  va_start(va, p);
  rc = vopentree(p, va);
  va_end(va);
  return rc;
}

int open_config(void) {
  char *h;

  if ((h = getenv("XDG_CONFIG_HOME"))) {
    return opentree(h, "pwtoy", NULL);
  }
  if ((h = getenv("HOME"))) {
    return opentree(h, ".config", "pwtoy", NULL);
  }
  return -1;
}

ssize_t readinput(const char *prompt, char *buf, size_t len) {
  int fd;
  char *p, c;
  size_t r, n = 0;

  if (isatty(STDIN_FILENO)) {
    if (-1 == (fd = open(_PATH_TTY, O_WRONLY))) {
      fd = STDERR_FILENO;
    }
    write(fd, prompt, strlen(prompt));
    if (STDERR_FILENO != fd) {
      close(fd);
    }
  }
  while (0 < (r = read(STDIN_FILENO, buf + n, len - n))) {
    if ((p = memchr(buf + n, '\n', r))) {
      return p - buf;
    }
    n += r;
  }
  if (-1 == r) {
    return -1;
  }
  if (n < len) {
    return n;
  }
  r = read(STDIN_FILENO, &c, 1);
  if (-1 == r) {
    return -1;
  }
  if (!r || c == '\n') {
    return n;
  }
  errno = E2BIG;
  return -1;
}

int main(int argc, char *argv[]) {
  char *p;
  size_t c;
  FILE *f = 0;
  uint32_t ctr;
  int i, r, n, done, dd, fd, loop;
  char has[1024], pwd[1024], sel[8];

  if (-1 == (dd = open_config())) {
    n = 0;
  } else {
    if (-1 == (fd = openat(dd, "master.argon2", O_RDWR | O_CREAT, 0777))) {
      err(2, "openat");
    }
    close(dd);
    if (!(f = fdopen(fd, "r+"))) {
      err(2, "fdopen");
    }
    if (!(n = fread(has, 1, sizeof(has) - 1, f))) {
      if (ferror(f)) {
        err(2, "fread");
      }
    }
  }
  has[n] = 0;

  loop = 0;
  done = 0;
  do {
    if (loop) {
      fprintf(stderr, "Password does not match.\n");
    }
    loop += !loop;
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

  if (-1 != dd && !n) {
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

  n = strlen(pwd) + 1;
  if (argc > 1) {
    if ((c = strlen(argv[1])) > sizeof(pwd) - n) {
      errx(2, "Site too long");
    }
    memcpy(pwd + n, argv[1], c);
  } else {
    if (-1 == (c = readinput("Site> ", pwd + n, sizeof(pwd) - n))) {
      err(2, "readinput");
    }
  }
  n += c;

  bzero(sel, sizeof(sel));

  r = argon2d_hash_raw(4, 1 << 16, 4, pwd, n, sel, sizeof(sel), has, 32);
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
