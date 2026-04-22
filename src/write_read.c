#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include "write_read.h"


ssize_t safe_write(int fd ,const void *buf , size_t count);
ssize_t safe_read(int fd , void *buf , size_t count) ;

ssize_t full_write(int fd, const void *buf, size_t count) {
  size_t total = 0;
  const char *ptr = (const char *) buf;
  while (count > 0) {
    ssize_t n_w = safe_write( fd , ptr , count);
    if (n_w < 0) {
      break;
    }
    if (n_w == 0) {
      break;
    }
    total += (size_t) n_w;
    ptr += n_w;
    count -= (size_t) n_w;
  }
  return (ssize_t) total;
}

ssize_t full_read(int fd, void *buf, size_t count){

 size_t total = 0;
  char *ptr = ( char *) buf;
  while (count > 0) {
    ssize_t n_w = safe_read (fd , ptr ,count) ;
    if (n_w < 0) {
      break;
    }
    if (n_w == 0) {
      break;
    }
    total += (size_t) n_w;
    ptr += n_w;
    count -= (size_t) n_w;
  }
  return (ssize_t) total;
}


ssize_t safe_read(int fd , void *buf , size_t count) {
  ssize_t n;
  do {
    n = read(fd , buf , count );
  }
  while (n == -1 && errno == EINTR );
  return n;
}


ssize_t safe_write(int fd , const void *buf , size_t count) {
  ssize_t n;
  do {
    n = write(fd , buf , count );
  }
  while (n == -1 && errno == EINTR );
  return n;
}
