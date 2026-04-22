#include <sys/types.h>
#include <stddef.h>



//  Tente d'écrire count octets dans le fichier donné par le descripteur
//  fd à partir de la zone mémoire pointé par buf.
//   Retourne le nombre d'octets ecrits avec succès.
//  Retourne 0 en cas de fin de fichier. Retourne -1 en cas d'erreur et
//  errno est fixée en consequence.

ssize_t full_write(int fd, const void *buf, size_t count);


//  Tente lire count octets du contenu du fichier donné par le descripteur
//  fd en l'écrivant dans la zone mémoire pointé par buf.
//  fd en l'écrivant dans la zone mémoire pointé par buf.
//   Retourne le nombre d'octets lus avec succès.
//  Retourne 0 en cas de fin de fichier. Retourne -1 en cas d'erreur et
//  errno est fixée en consequence.
ssize_t full_read(int fd, void *buf, size_t count);
