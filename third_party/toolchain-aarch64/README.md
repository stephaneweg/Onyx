# Toolchain AArch64 pour construire les apps Onyx (bootstrap session cloud)

Les apps Onyx (dont `taatu.app`) se compilent avec **Arm GNU Toolchain 13.3.rel1,
`aarch64-none-elf`** (fournit `newlib` : `libc`, `libm`). Elle n'est **pas** commitée ici
(≈ 500 Mo décompressés) : ce dossier fournit de quoi **l'installer et construire** dans une
session Linux (le build Windows n'est pas supporté — utiliser WSL ou une machine/CI Linux).

> Pourquoi un bootstrap et pas le binaire ? Trop gros pour le dépôt, et spécifique à l'OS
> hôte. Les scripts ci-dessous téléchargent la version exacte attendue par `kernel/` et
> `user/`, puis construisent le client TAATU.

## Prérequis (session cloud Linux x86-64)

```sh
sudo apt-get update && sudo apt-get install -y curl xz-utils make g++
```

## 1. Installer la toolchain

```sh
cd third_party/toolchain-aarch64
./get-toolchain.sh                 # télécharge + extrait dans ./arm-gnu-toolchain-13.3/
export PATH="$PWD/arm-gnu-toolchain-13.3/bin:$PATH"
aarch64-none-elf-gcc --version     # doit afficher 13.3.x
```

`get-toolchain.sh` récupère depuis developer.arm.com :
`arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-elf.tar.xz`, vérifie le SHA-256, extrait.

## 2. Construire les bibliothèques d'appui (une fois)

Une app n'a **pas** besoin du noyau. Depuis `user/` :

```sh
cd ../../user
make wtk/libwtk.a ft/libft.a ft/fonts.h libc/crt0libc.o libc/onyx_syscalls.o
# mbedTLS est déjà construit : third_party/mbedtls-3.6.3/library/libmbed{tls,x509,crypto}.a
# (sinon : make -C tls)
```

## 3. Construire `taatu.app`

```sh
cd ../third_party/toolchain-aarch64
./build-taatu.sh                   # → user/taatu.elf, puis out/taatu.app/{main,app.txt}
```

ou directement la règle du `user/Makefile` (ajoutée pour ce client) :

```sh
cd user && make taatu.elf
```

Déployer : copier `out/taatu.app/` dans `SD:/apps/` d'une carte Onyx (ou via telnet/FTP,
cf. `docs/HANDOFF.md`). Pour une carte complète : `cd kernel && make && make stage`.

## 4. Tester sur PC sans Pi (recommandé pour le réseau)

`tools/tests/desktop_sim/` compile l'app pour le PC contre un faux noyau (`fakekapi.cpp`).
Avec `SIM_REALNET=1`, les sockets sont celles du PC → on peut viser le vrai serveur TAATU
et valider la pile WebSocket/Engine.IO/Socket.IO. Voir `user/Apps/taatu/DESIGN.md` §8.

## Fichiers de ce dossier

| Fichier | Rôle |
|---|---|
| `README.md` | ce document |
| `get-toolchain.sh` | télécharge + vérifie + extrait la toolchain ARM 13.3.rel1 |
| `build-taatu.sh` | construit les libs d'appui puis `taatu.elf` → `out/taatu.app/` |
| `.gitignore` | ignore la toolchain téléchargée et `out/` |

> Licences : newlib (BSD-like), mbedTLS (Apache-2.0), FreeType (FTL), stb (domaine public).
> Aucune lib GPL liée dans `taatu.app`. Code de l'app : MIT.
