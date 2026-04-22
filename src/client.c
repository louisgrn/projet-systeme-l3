#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <string.h>
#include <stdlib.h>
#include "communication.h"
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#define FIFO_REQ "/tmp/fifo_rep_"
#include "write_read.h"

void afficher_usage(char *nom_prog) {
  fprintf(stderr, "Usage: %s [-f gray|duotone|flou] <chemin_image>\n",
      nom_prog);
}

void gestion_timeout([[maybe_unused]] int sig) {
  const char *str = "\nError: Timeout.\n";
  char nom[256];
  snprintf(nom, sizeof(nom), "%s%d", FIFO_REQ, getpid());
  unlink(nom);
  if (write(STDOUT_FILENO, str, strlen(str)) == -1) {
    exit(EXIT_FAILURE);
  }
  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <chemin_image>\n", argv[0]);
    return EXIT_FAILURE;
  }
  //Option par défaut si l'utilisateur ne donne pas de filtre :
  int filtre = FILTRE_GRAY;
  int opt;
  while ((opt = getopt(argc, argv, "f:")) != -1) {
    if (opt == 'f') {
      if (strcmp(optarg, "gray") == 0) {
        filtre = FILTRE_GRAY;
      } else if (strcmp(optarg, "bichromie") == 0) {
        filtre = FILTRE_BICHROMIE;
      } else if (strcmp(optarg, "flou") == 0) {
        filtre = FILTRE_FLOU;
      } else {
        fprintf(stderr, "Filtre inconnu: %s\n", optarg);
        afficher_usage(argv[0]);
        return EXIT_FAILURE;
      }
    } else {
      afficher_usage(argv[0]);
      return EXIT_FAILURE;
    }
  }
  struct stat st;
  if (stat(argv[optind], &st) == 0) {
    if (st.st_size > 100 * 1024 * 1024) {
      fprintf(stderr, "Erreur : Image trop grande\n");
      return EXIT_FAILURE;
    }
  } else {
    fprintf(stderr, "Erreur :Lors de l'ouverture du fichier\n");
    return EXIT_FAILURE;
  }
  sem_t *mutex = sem_open(SEMAPHORE, 0);
  if (mutex == SEM_FAILED) {
    perror("sem_open");
    return EXIT_FAILURE;
  }
  sem_t *plein = sem_open(SEMAPHORE_PLEIN, 0);
  if (plein == SEM_FAILED) {
    perror("sem_open request");
    sem_close(mutex);
    return EXIT_FAILURE;
  }
  sem_t *vide = sem_open(SEMAPHORE_VIDE, 0);
  if (vide == SEM_FAILED) {
    perror("sem_open request");
    sem_close(mutex);
    sem_close(plein);
    return EXIT_FAILURE;
  }
  char nom[256];
  snprintf(nom, sizeof(nom), "%s%d", FIFO_REQ, getpid());
  if (mkfifo(nom, USER_RIGHT) == -1) {
    perror("mkfifo");
    sem_close(mutex);
    sem_close(plein);
    return EXIT_FAILURE;
  }
  struct sigaction action;
  action.sa_handler = gestion_timeout;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  sigaction(SIGALRM, &action, NULL);
  int d = shm_open(SHARE_MEMORY, O_RDWR, USER_RIGHT);
  if (d == -1) {
    perror("shm_open");
    sem_close(mutex);
    sem_close(plein);
    sem_close(mutex);
    sem_close(vide);
    unlink(nom);
    return EXIT_FAILURE;
  }
  struct req_buf *data = mmap(
        NULL,
        sizeof(struct req_buf),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        d,
        0);
  if (data == MAP_FAILED) {
    perror("mmap");
    sem_close(plein);
    sem_close(mutex);
    sem_close(vide);
    close(d);
    unlink(nom);
    return EXIT_FAILURE;
  }
  P(vide);
  P(mutex);
  strcpy(data->tab[data->ecr].chemin, argv[optind]);
  data->tab[data->ecr].filtre = filtre;
  data->tab[data->ecr].pid = getpid();
  data->ecr = (data->ecr + 1) % TAILLE_TAMPON;
  V(mutex);
  V(plein);
  printf("Requete envoyé\n");
  alarm(10);
  int tube = open(nom, O_RDONLY);
  alarm(0);
  if (tube == -1) {
    perror("open FIFO");
    munmap(data, sizeof(struct req_buf));
    close(d);
    unlink(nom);
    sem_close(mutex);
    sem_close(plein);
    sem_close(mutex);
    sem_close(vide);
    return EXIT_FAILURE;
  }
  BMP_Header header;
  BMP_Info_Header info;
  ssize_t r = full_read(tube, &header, sizeof(header));
  if (r != sizeof(header)) {
    perror("read header");
    close(tube);
    munmap(data, sizeof(struct req_buf));
    close(d);
    unlink(nom);
    sem_close(mutex);
    sem_close(plein);
    sem_close(mutex);
    sem_close(vide);
    return EXIT_FAILURE;
  }
  r = full_read(tube, &info, sizeof(info));
  if (r != sizeof(info)) {
    perror("read info");
    close(tube);
    munmap(data, sizeof(struct req_buf));
    close(d);
    unlink(nom);
    sem_close(mutex);
    sem_close(plein);
    sem_close(mutex);
    sem_close(vide);
    return EXIT_FAILURE;
  }
  size_t row_size = (size_t) info.width * sizeof(pixel);
  size_t padding = (4 - (row_size % 4)) % 4;
  size_t padded_row_size = row_size + padding;
  size_t pixel_size = padded_row_size * (size_t) info.height;
  pixel *p = malloc(pixel_size);
  if (p == NULL) {
    perror("malloc");
    close(tube);
    munmap(data, sizeof(struct req_buf));
    close(d);
    unlink(nom);
    sem_close(mutex);
    sem_close(plein);
    sem_close(mutex);
    sem_close(vide);
    return EXIT_FAILURE;
  }
  r = full_read(tube, p, pixel_size);
  if (r != (ssize_t) pixel_size) {
    fprintf(stderr, "FIFO fermé prématurément ou erreur lecture\n");
    free(p);
    close(tube);
    munmap(data, sizeof(struct req_buf));
    close(d);
    unlink(nom);
    sem_close(mutex);
    sem_close(plein);
    sem_close(vide);
    return EXIT_FAILURE;
  }
  char fichier[256];
  snprintf(fichier, sizeof(fichier), "image_%d.bmp", getpid());
  int new_img = open(fichier, O_CREAT | O_WRONLY | O_TRUNC, USER_RIGHT);
  if (new_img == -1) {
    perror("open fichier");
    free(p);
    close(tube);
    munmap(data, sizeof(struct req_buf));
    close(d);
    unlink(nom);
    sem_close(mutex);
    sem_close(plein);
    sem_close(mutex);
    sem_close(vide);
    return EXIT_FAILURE;
  }
  if (full_write(new_img, &header, sizeof(header)) != sizeof(header)) {
    perror("write header");
    free(p);
    close(tube);
    munmap(data, sizeof(struct req_buf));
    close(d);
    unlink(nom);
    sem_close(mutex);
    sem_close(plein);
    sem_close(mutex);
    sem_close(vide);
    return EXIT_FAILURE;
  }
  if (full_write(new_img, &info, sizeof(info)) != sizeof(info)) {
    perror("write info");
    free(p);
    close(tube);
    munmap(data, sizeof(struct req_buf));
    close(d);
    unlink(nom);
    sem_close(mutex);
    sem_close(plein);
    sem_close(mutex);
    sem_close(vide);
    return EXIT_FAILURE;
  }
  if (full_write(new_img, p, pixel_size) != (ssize_t) pixel_size) {
    perror("write pixels");
    free(p);
    close(tube);
    munmap(data, sizeof(struct req_buf));
    close(d);
    unlink(nom);
    sem_close(mutex);
    sem_close(plein);
    sem_close(mutex);
    sem_close(vide);
    return EXIT_FAILURE;
  }
  close(tube);
  close(d);
  unlink(nom);
  sem_close(mutex);
  sem_close(vide);
  return EXIT_SUCCESS;
}
