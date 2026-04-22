#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>

#define TAILLE_TAMPON 10
#define SHARE_MEMORY "/ma_structure"
#define SEMAPHORE "/mon_semaphore"
#define  SEMAPHORE_VIDE "/mon_semaphore_vide"
#define SEMAPHORE_PLEIN  "/mon_semaphore_plein"
#define  SEMAPHORE_IMG "/mon_semaphore_img"
#define  SEMAPHORE_WORKER "/mon_semaphore_worker"
#define USER_RIGHT S_IRUSR | S_IWUSR | S_IRGRP
#define FILTRE_GRAY 0
#define FILTRE_BICHROMIE 1
#define FILTRE_FLOU 2



#define V(sem) sem_post(sem)
#define P(sem) sem_wait(sem)
typedef struct filter_request {
    pid_t pid;
    char chemin[256];
    int filtre;
    int parametres[5];
} filter_request;
typedef struct req_buf {
  struct filter_request tab[TAILLE_TAMPON];
  int ecr;
}req_buf;
#pragma pack(push, 1)
typedef struct BMP_Header{
    uint8_t  type[2];
    uint32_t file_size;
    uint8_t  reserved[4];
    uint32_t offset_start_framebuffer;
}BMP_Header;
typedef struct BMP_Info_Header{
    uint32_t header_size;
    uint32_t width;
    uint32_t height;
    uint16_t n_color_planes;
    uint16_t bits_per_pixel;
    uint32_t compression_method;
    uint32_t raw_size_framebuffer;
    int32_t  h_res;
    int32_t  v_res;
    uint32_t n_color_palettes;
    uint32_t n_important_colors;
}BMP_Info_Header;
typedef struct pixel{
    uint8_t b;
    uint8_t g;
    uint8_t r;
}pixel;
typedef struct segment_image{
    struct BMP_Header *header;
    struct BMP_Info_Header *info;
    struct pixel pa[] ;
}segment_image;
#pragma pack(pop)
