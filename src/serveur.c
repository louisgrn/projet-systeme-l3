#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "communication.h"
#include <stdint.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <syslog.h>
#include "write_read.h"
#include <errno.h>

#define FIFO_REQ "/tmp/fifo_rep_"
#define THREAD_NB_WORKER 8
#define NB_WORKER_MAX 50

struct thread_workspace {
  int thread_id;
  struct segment_image *shm;
  int ligne_debut;
  int ligne_fin;
  int filtre;

  pixel *img_out; // pour le flou gaussien
};

void *thread_work_filter(void *arg) {
  struct thread_workspace *res = (struct thread_workspace *) arg;
  uint32_t taille = res->shm->info->width;
  pixel *pa = res->shm->pa;
  const int noyau[3][3] = {
    { 1, 2, 1 },
    { 2, 4, 2 },
    { 1, 2, 1 }
  };
  const double seuil = 100.0;
  const int poids_total = 16;
  for (uint32_t x = (uint32_t) res->ligne_debut; x < (uint32_t) res->ligne_fin;
      ++x) {
    for (uint32_t y = 0; y < taille; ++y) {
      uint32_t index = x * taille + y;
      uint8_t r_in = pa[index].r;
      uint8_t g_in = pa[index].g;
      uint8_t b_in = pa[index].b;
      if (res->filtre == FILTRE_GRAY) {
        uint32_t gray_val = (uint32_t) (r_in * 0.299 + g_in * 0.587 + b_in
            * 0.114);
        pa[index].r = (uint8_t) gray_val;
        pa[index].g = (uint8_t) gray_val;
        pa[index].b = (uint8_t) gray_val;
      } else if (res->filtre == FILTRE_BICHROMIE) {
        double moyenne = 0.2126 * r_in + 0.7152 * g_in + 0.0722 * b_in;
        double rouge_out;
        double vert_out;
        double bleu_out;
        double k;
        if (moyenne < seuil) {
          k = moyenne / seuil;
          rouge_out = 94.0 * k;
          vert_out = 38.0 * k;
          bleu_out = 18.0 * k;
        } else {
          k = (moyenne - seuil) / (255.0 - seuil);
          rouge_out = 94.0 + k * (255.0 - 94.0);
          vert_out = 38.0 + k * (255.0 - 38.0);
          bleu_out = 18.0 + k * (255.0 - 18.0);
        }
        pa[index].r = (uint8_t) rouge_out;
        pa[index].g = (uint8_t) vert_out;
        pa[index].b = (uint8_t) bleu_out;
      } else if (res->filtre == FILTRE_FLOU) {
        uint32_t somme_r = 0;
        uint32_t somme_g = 0;
        uint32_t somme_b = 0;
        for (int i = -1; i <= 1; ++i) {
          for (int j = -1; j <= 1; ++j) {
            int voisin_x = (int) x + i;
            int voisin_y = (int) y + j;
            if (voisin_x >= 0 && voisin_x < (int) res->shm->info->height
                && voisin_y >= 0 && voisin_y < (int) taille) {
              uint32_t index_voisin = (uint32_t) voisin_x * taille
                  + (uint32_t) voisin_y;
              int poids = noyau[i + 1][j + 1];
              somme_r += pa[index_voisin].r * (uint32_t) poids;
              somme_g += pa[index_voisin].g * (uint32_t) poids;
              somme_b += pa[index_voisin].b * (uint32_t) poids;
            } else {
            }
          }
        }
        res->img_out[index].r = (uint8_t) (somme_r / (uint32_t) poids_total);
        res->img_out[index].g = (uint8_t) (somme_g / (uint32_t) poids_total);
        res->img_out[index].b = (uint8_t) (somme_b / (uint32_t) poids_total);
      }
    }
  }
  return NULL;
}

//variable globale
sem_t *worker;
void gestion_enfant([[maybe_unused]] int sig) {
  int errnosave = errno;
  while (waitpid(-1, NULL, WNOHANG) > 0) {
    V(worker);
  }
  errno = errnosave;
}

int daemonize(void) {
  switch (fork()) {
    case -1:
      return -1;
    case 0:
      break;
    default:
      exit(EXIT_SUCCESS);
  }
  if (setsid() < 0) {
    return -1;
  }
  switch (fork()) {
    case -1:
      return -1;
    case 0:
      break;
    default:
      exit(EXIT_SUCCESS);
  }
  umask(0);
  if (chdir("/") < 0) {
    return -1;
  }
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
  int d = open("/dev/null", O_RDWR);
  if (d != -1) {
    dup2(d, STDIN_FILENO);
    dup2(d, STDOUT_FILENO);
    dup2(d, STDERR_FILENO);
    if (d > STDERR_FILENO) {
      close(d);
    }
  }
  return 0;
}

int main(void) {
  if (daemonize() == -1) {
    return EXIT_FAILURE;
  }
  openlog("ServerImg", LOG_PID, LOG_DAEMON);
  struct sigaction action;
  action.sa_handler = SIG_IGN;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  // Immunisation du serveur contre les signaux
  if (sigaction(SIGINT, &action, NULL) == -1) {
    syslog(LOG_ERR, "Failure of sigaction");
    return EXIT_FAILURE;
  }
  if (sigaction(SIGTERM, &action, NULL) == -1) {
    syslog(LOG_ERR, "Failure of sigaction");
    return EXIT_FAILURE;
  }
  if (sigaction(SIGQUIT, &action, NULL) == -1) {
    syslog(LOG_ERR, "Failure of sigaction");
    return EXIT_FAILURE;
  }
  if (sigaction(SIGTSTP, &action, NULL) == -1) {
    syslog(LOG_ERR, "Failure of sigaction");
    return EXIT_FAILURE;
  }
  struct sigaction sign_child;
  sign_child.sa_handler = gestion_enfant;
  sigemptyset(&sign_child.sa_mask);
  sign_child.sa_flags = SA_RESTART | SA_NOCLDSTOP;
  if (sigaction(SIGCHLD, &sign_child, NULL) == -1) {
    syslog(LOG_ERR, "Failure of sigaction");
    return EXIT_FAILURE;
  }
  sem_unlink(SEMAPHORE);
  sem_unlink(SEMAPHORE_PLEIN);
  sem_unlink(SEMAPHORE_VIDE);
  sem_unlink(SEMAPHORE_WORKER);
  shm_unlink(SHARE_MEMORY);
  sem_t *mutex;
  mutex = sem_open(SEMAPHORE, O_CREAT | O_EXCL, USER_RIGHT, 1);
  if (mutex == SEM_FAILED) {
    syslog(LOG_ERR, "Failure of sem_open");
    return EXIT_FAILURE;
  }
  sem_t *vide = sem_open(SEMAPHORE_VIDE, O_CREAT | O_EXCL, USER_RIGHT,
        TAILLE_TAMPON);
  if (vide == SEM_FAILED) {
    syslog(LOG_ERR, "Failure of sem_open");
    sem_close(mutex);
    sem_unlink(SEMAPHORE);
    return EXIT_FAILURE;
  }
  worker = sem_open(SEMAPHORE_WORKER, O_CREAT | O_EXCL, USER_RIGHT,
        NB_WORKER_MAX);
  if (worker == SEM_FAILED) {
    syslog(LOG_ERR, "Failure of sem_open");
    sem_close(mutex);
    sem_close(vide);
    sem_unlink(SEMAPHORE);
    return EXIT_FAILURE;
  }
  sem_t *plein = sem_open(SEMAPHORE_PLEIN, O_CREAT | O_EXCL, USER_RIGHT, 0);
  if (plein == SEM_FAILED) {
    syslog(LOG_ERR, "Failure of sem_open");
    sem_close(mutex);
    sem_close(vide);
    sem_close(worker);
    sem_unlink(SEMAPHORE);
    sem_unlink(SEMAPHORE_VIDE);
    return EXIT_FAILURE;
  }
  int d = shm_open(SHARE_MEMORY, O_RDWR | O_CREAT, USER_RIGHT);
  if (d == -1) {
    syslog(LOG_ERR, "Failure of shm_open");
    sem_close(mutex);
    sem_close(vide);
    sem_close(plein);
    sem_close(worker);
    sem_unlink(SEMAPHORE);
    sem_unlink(SEMAPHORE_VIDE);
    sem_unlink(SEMAPHORE_PLEIN);
    return EXIT_FAILURE;
  }
  if (ftruncate(d, sizeof(req_buf)) == -1) {
    syslog(LOG_ERR, "Failure of ftruncate");
    close(d);
    shm_unlink(SHARE_MEMORY);
    sem_close(mutex);
    sem_close(vide);
    sem_close(plein);
    sem_close(worker);
    sem_unlink(SEMAPHORE);
    sem_unlink(SEMAPHORE_VIDE);
    sem_unlink(SEMAPHORE_PLEIN);
    return EXIT_FAILURE;
  }
  struct req_buf *data_init = mmap(NULL,
        sizeof(struct req_buf),
        PROT_READ | PROT_WRITE,
        MAP_SHARED, d, 0);
  if (data_init == MAP_FAILED) {
    syslog(LOG_ERR, "Failure of mmap");
    close(d);
    shm_unlink(SHARE_MEMORY);
    sem_close(mutex);
    sem_close(vide);
    sem_close(plein);
    sem_close(worker);
    sem_unlink(SEMAPHORE);
    sem_unlink(SEMAPHORE_VIDE);
    sem_unlink(SEMAPHORE_PLEIN);
    return EXIT_FAILURE;
  }
  data_init->ecr = 0;
  int lect = 0;
  while (1) {
    P(worker);
    P(plein);
    P(mutex);
    struct filter_request local_data = data_init->tab[lect];
    lect = (lect + 1) % TAILLE_TAMPON;
    V(mutex);
    V(vide);
    switch (fork()) {
      case -1:
        syslog(LOG_ERR, "Failure of fork");
        break;
      case 0:
        struct sigaction renitial;
        renitial.sa_handler = SIG_DFL;
        sigemptyset(&renitial.sa_mask);
        renitial.sa_flags = 0;
        sigaction(SIGINT, &renitial, NULL);
        sigaction(SIGTERM, &renitial, NULL);
        sigaction(SIGQUIT, &renitial, NULL);
        int image = open(local_data.chemin, O_RDONLY);
        if (image == -1) {
          syslog(LOG_ERR, "Failure of open");
          exit(EXIT_FAILURE);
        }
        struct stat info;
        if (fstat(image, &info) == -1) {
          syslog(LOG_ERR, "Failure of fstat");
          close(image);
          exit(EXIT_FAILURE);
        }
        size_t file_size = (size_t) info.st_size;
        //Ici je map l'image en mémoire
        void *img_data = mmap(NULL, file_size,
              PROT_READ, MAP_PRIVATE, image, 0);
        if (img_data == MAP_FAILED) {
          syslog(LOG_ERR, "Failure of mmap");
          close(image);
          exit(EXIT_FAILURE);
        }
        BMP_Header *header = (BMP_Header *) img_data;
        if (header->type[0] != 'B' || header->type[1] != 'M') {
          syslog(LOG_ERR, "wrong type of image");
          close(image);
          exit(EXIT_FAILURE);
        }
        BMP_Info_Header *info_header
          = (BMP_Info_Header *) ((char *) img_data + sizeof(BMP_Header));
        pixel *pixels = (pixel *) ((char *) img_data
            + header->offset_start_framebuffer);
        // pour les thead on peut tout manipuler
        struct segment_image *img = malloc(sizeof(segment_image)
              + info_header->width * info_header->height * sizeof(pixel));
        if (img == NULL) {
          syslog(LOG_ERR, "Failure of malloc");
          munmap(img_data, file_size);
          close(image);
          exit(EXIT_FAILURE);
        }
        img->header = header;
        img->info = info_header;
        size_t row_size = info_header->width * sizeof(pixel);
        size_t padding = (size_t) (4 - (row_size % 4)) % 4;
        char *src_ptr = (char *) pixels;
        char *dest_ptr = (char *) img->pa;
        for (uint32_t i = 0; i < info_header->height; i++) {
          memcpy(dest_ptr, src_ptr, row_size);
          dest_ptr += row_size;
          src_ptr += row_size + padding;
        }
        size_t taille_image = info_header->width * info_header->height
            * sizeof(pixel);
        pixel *flou = malloc(taille_image);
        if (flou == NULL) {
          syslog(LOG_ERR, "Failure of malloc");
          munmap(img_data, file_size);
          close(image);
        }
        //tableau de thread
        uint32_t nb_thread = THREAD_NB_WORKER;
        struct thread_workspace tab_for_thread[THREAD_NB_WORKER];
        uint32_t ligne_par_thread = img->info->height / (uint32_t) nb_thread;
        pthread_t tab_thread[THREAD_NB_WORKER];
        for (uint32_t i = 0; i < nb_thread; ++i) {
          tab_for_thread[i].img_out = flou;
          tab_for_thread[i].ligne_debut = (int) ((uint32_t) i
              * ligne_par_thread);
          tab_for_thread[i].ligne_fin = (i
              == nb_thread
              - 1) ? (int) img->info->height : (int) ((uint32_t) (i + 1)
              * ligne_par_thread);
          tab_for_thread[i].shm = img;
          tab_for_thread[i].filtre = local_data.filtre;
          if (pthread_create(&tab_thread[i], NULL, thread_work_filter,
                &tab_for_thread[i]) == -1) {
            syslog(LOG_ERR, "Failure of pthread_create");
          }
        }
        for (uint32_t i = 0; i < nb_thread; ++i) {
          pthread_join(tab_thread[i], NULL);
        }
        char nom[256];
        snprintf(nom, sizeof(nom), "%s%d", FIFO_REQ, local_data.pid);
        int tube = open(nom, O_WRONLY);
        if (tube == -1) {
          syslog(LOG_ERR, "Failure of open");
          free(img);
          munmap(img_data, file_size);
          close(image);
          exit(EXIT_FAILURE);
        }
        size_t padded_row_size = row_size + padding;
        size_t total_size = padded_row_size * info_header->height;
        void *p = calloc(1,
              total_size + sizeof(BMP_Header) + sizeof(BMP_Info_Header));
        if (p == NULL) {
          syslog(LOG_ERR, "Failure of malloc");
          close(tube);
          free(img);
          munmap(img_data, file_size);
          close(image);
          exit(EXIT_FAILURE);
        }
        memcpy(p, img->header, sizeof(BMP_Header));
        memcpy((char *) p + sizeof(BMP_Header), img->info,
            sizeof(BMP_Info_Header));
        BMP_Header *new_header = (BMP_Header *) p;
        new_header->offset_start_framebuffer
          = sizeof(BMP_Header) + sizeof(BMP_Info_Header);
        BMP_Info_Header *new_info
          = (BMP_Info_Header *) ((char *) p + sizeof(BMP_Header));
        new_info->header_size = sizeof(BMP_Info_Header);
        new_info->bits_per_pixel = 24;
        new_info->compression_method = 0;
        new_info->raw_size_framebuffer = (uint32_t) total_size;
        char *dst_pixels = (char *) p + sizeof(BMP_Header)
            + sizeof(BMP_Info_Header);
        char *source_pixels = (char *) ((local_data.filtre
            == FILTRE_FLOU) ? flou : img->pa);
        for (uint32_t i = 0; i < info_header->height; ++i) {
          memcpy(dst_pixels, source_pixels, row_size);
          source_pixels += row_size;
          dst_pixels += padded_row_size;
        }
        ssize_t r = full_write(tube, p,
              total_size + sizeof(BMP_Header) + sizeof(BMP_Info_Header));
        if (r == -1) {
          syslog(LOG_ERR, "Failure of full_write");
          free(p);
          close(tube);
          free(img);
          munmap(img_data, file_size);
          close(image);
          exit(EXIT_FAILURE);
        }
        free(p);
        free(flou);
        close(tube);
        free(img);
        munmap(img_data, file_size);
        close(image);
        exit(EXIT_SUCCESS);
      default:
        break;
    }
  }
  close(d);
  munmap(data_init, sizeof(struct req_buf));
  sem_close(mutex);
  sem_close(vide);
  sem_close(plein);
  sem_unlink(SEMAPHORE);
  sem_unlink(SEMAPHORE_VIDE);
  sem_unlink(SEMAPHORE_PLEIN);
  shm_unlink(SHARE_MEMORY);
  return EXIT_SUCCESS;
}
