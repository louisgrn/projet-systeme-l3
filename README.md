# Projet Systèmes d'Exploitation - Traitement d'Images (Client/Serveur)



## Description
Ce projet implémente une architecture client/serveur en C permettant d'appliquer des filtres sur des images au format BMP. La communication entre le client et le serveur s'effectue via des mécanismes de communication inter-processus (IPC) POSIX : mémoire partagée, sémaphores et tubes nommés (FIFOs). Le serveur s'exécute en arrière-plan (daemon) et traite les images de manière multithreadée.

## Fonctionnalités
* **Filtres disponibles :** Nuances de gris (`gray`), Bichromie (`bichromie`), et Flou gaussien (`flou`).
* **Serveur Daemon :** Le serveur s'exécute en tâche de fond de manière autonome.
* **Traitement Multithread :** Le serveur utilise 8 threads (workers) pour diviser et accélérer le traitement d'une image.
* **Format supporté :** Images BMP d'une taille maximale de 100 Mo.

## Compilation
Le projet inclut un `makefile` pour faciliter la compilation.

* Pour compiler le projet complet (client et serveur), exécutez la commande suivante à la racine du projet :
  ```bash
  make
  ```
  Cela générera les exécutables `client` et `serveur` dans un dossier `build/`.

* Pour nettoyer les fichiers compilés (supprimer le dossier `build`) :
  ```bash
  make clean
  ```

## Utilisation

### 1. Démarrer le serveur
Avant de lancer un client, vous devez démarrer le serveur :
```bash
./build/serveur
```

### 2. Lancer un client
Utilisez la commande suivante pour envoyer une image au serveur et appliquer un filtre :
```bash
./build/client [-f gray|bichromie|flou] <chemin_image>
```

* **Options de filtrage :**
  * `-f gray` : Applique un filtre nuances de gris (option par défaut si aucun argument n'est fourni).
  * `-f bichromie` : Applique un effet de bichromie.
  * `-f flou` : Applique un flou gaussien.

* **Exemple d'utilisation :**
  ```bash
  ./build/client -f flou mon_image.bmp
  ```

Une fois le traitement terminé, le client télécharge le résultat et génère une nouvelle image nommée `image_<PID>.bmp` dans votre répertoire courant. Le client dispose également d'un timeout de 10 secondes (géré par `SIGALRM`) pour éviter de rester bloqué indéfiniment en cas de problème.

## Architecture Technique
* **Mémoire Partagée (`shm_open`) :** Utilisée pour stocker les requêtes des clients dans un tampon circulaire de 10 places.
* **Sémaphores POSIX (`sem_open`) :** Assurent la synchronisation entre les processus. Le projet utilise des sémaphores pour l'exclusion mutuelle, la gestion des places vides/pleines du tampon, et la limitation du nombre total de workers.
* **Tubes Nommés (FIFOs) :** Utilisés par le serveur pour renvoyer les données de l'image modifiée au processus client correspondant (le tube est nommé `/tmp/fifo_rep_<PID>`).
