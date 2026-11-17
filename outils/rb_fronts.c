// Records the edges of the Pi 4 lines GPIO5 (LNK2→controller) and GPIO9 (controller→LNK2), to the microsecond,
// to measure the timing of the LNK2 handshake when it is plugged in.
// Reads the GPLEV0 register directly through /dev/gpiomem (works even in UART mode).
// Usage: rb_fronts <duration s> <file>   → lines « t_ns level_GPIO5 level_GPIO9 » at each change.
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

#define MASQUE ((1u << 5) | (1u << 9))
#define MAX_FRONTS 8000000

static uint64_t maintenant(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage : %s <durée s> <fichier>\n", argv[0]);
    return 1;
  }
  double duree = atof(argv[1]);
  int fd = open("/dev/gpiomem", O_RDONLY | O_SYNC);
  if (fd < 0) {
    perror("/dev/gpiomem");
    return 1;
  }
  volatile uint32_t *gpio = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
  if (gpio == MAP_FAILED) {
    perror("mmap");
    return 1;
  }
  uint64_t *t = malloc(sizeof(uint64_t) * MAX_FRONTS);
  uint32_t *v = malloc(sizeof(uint32_t) * MAX_FRONTS);
  size_t n = 0;
  uint64_t debut = maintenant(), fin = debut + (uint64_t) (duree * 1e9);
  uint32_t prec = gpio[13] & MASQUE;  // GPLEV0
  t[n] = debut;
  v[n++] = prec;
  uint64_t tc = debut;
  while (n < MAX_FRONTS) {
    uint32_t lv = gpio[13] & MASQUE;
    if (lv != prec) {
      tc = maintenant();
      t[n] = tc;
      v[n++] = lv;
      prec = lv;
    } else if ((tc = maintenant()) > fin) {
      break;
    }
  }
  FILE *f = fopen(argv[2], "w");
  for (size_t i = 0; i < n; i++)
    fprintf(f, "%llu %u %u\n", (unsigned long long) (t[i] - debut), (v[i] >> 5) & 1, (v[i] >> 9) & 1);
  fclose(f);
  fprintf(stderr, "%zu fronts en %.1f s\n", n, duree);
  return 0;
}
