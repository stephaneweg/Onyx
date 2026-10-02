# TAATU — client natif Onyx (`taatu.app`)

Client natif du monde social **TAATU** (`taatu.world`) pour Onyx OS (Raspberry Pi 4,
AArch64). Il reproduit le fonctionnement du client web officiel (une *coquille* Electron
qui charge `world.html`), mais **sans navigateur** : fenêtre Onyx + framebuffer, pile
réseau kapi + mbedTLS, décodage d'images Onyx. Comme le client web, il **n'embarque aucun
asset** : sprites, décors et sons sont téléchargés depuis `taatu.world` au runtime et mis
en cache.

> Auteur Onyx : Stéphane Wegener. Jeu/serveur/protocole : Lucas Catale (Sofo Solutions),
> qui a autorisé ce portage. Ce dossier ne contient **aucun** asset TAATU ni code serveur :
> seulement le client natif et la description du protocole observée sur le client public.

Statut : **squelette + moteur de protocole**. Voir [Feuille de route](#9-feuille-de-route).

---

## 1. Le client web en bref (ce qu'on reproduit)

- Monde social isométrique « à la Habbo » : une carte de bâtiments, des **salles**, des
  **avatars** paper-doll, du chat, des meubles, des mini-jeux, une économie (Stripe/VIP).
- **Rien de natif côté rendu** : DOM + sprites PNG, pas de moteur 3D (WebGL absent).
  Les vieilles animations Flash (« acticons ») passent par **Ruffle** (émulateur WASM).
- **Temps réel = Socket.IO 4.8.1** (WebSocket). **REST** pour l'auth, les salles, l'économie.
- **Anti-bot** : une clé HMAC de session (`ui-key`) signe les actions sensibles (§4.4).

Le binaire Electron (`Taatu.exe`) n'est qu'un habillage ; toute la logique vit sur le site.
Le client Onyx parle **directement** au même backend.

---

## 2. Architecture de l'app Onyx

Trois threads (kapi v67+), la GUI appartient au thread qui pompe :

```
 thread GUI (principal)              thread "net"                         thread "dl" (assets)
  kapi_pump_wait(16)                  TLS → WebSocket → Engine.IO →         HTTPS GET sprites/décors
  dessine la scène, présente          Socket.IO ; lit les trames,           → cache RAM:/SD:
  file d'actions (clic → déplacement) répond aux ping, signe les emits      → img_load_mem
  kapi_post(...) ← résultats          kapi_post(on_event,...) → GUI         kapi_post(on_asset,...) → GUI
```

- **GUI → net** : file d'actions protégée par `kapi_mutex_*`, réveil du thread net par
  `kapi_wake_word`/`kapi_wait_word` (futex v68).
- **net/dl → GUI** : `kapi_post(fn, ctx, value)`, exécuté par le pump sur le thread GUI
  (aucun verrou sur l'état GUI). C'est le modèle de `user/Apps/courier/net.h`.

### Fichiers

| Fichier | Rôle |
|---|---|
| `main.cpp` | fenêtre, boucle GUI, modèle de salle, rendu iso, saisie, câblage des threads |
| `net.hpp` | moteur réseau : `onyx_tls` → WebSocket (RFC 6455) → Engine.IO v4 → Socket.IO v5, + signature `_ui` (HMAC-SHA256) |
| `proto.hpp` | noms d'événements + (dé)sérialisation JSON des payloads de salle (`json.hpp`) |
| `app.txt` | nom affiché, catégorie, taille de pile |
| `Makefile` | règle de build (calquée sur `courier.elf`) |

---

## 3. Briques Onyx réutilisées (toutes présentes dans le dépôt)

| Besoin | Brique Onyx | Référence |
|---|---|---|
| Fenêtre + framebuffer `0x00RRGGBB` | `kapi_create_window(w,h,t)` → `unsigned*` | `user/kapi.h`, `Apps/2048` |
| Plein écran (sur le Pi) | `kapi_fullscreen_begin/present_fb/end` | `kapi.h` (v41) |
| Dessin (boîtes, texte bitmap, canvas) | **wtk** (`wtk::Canvas`, `wk_*`, `wtk::draw_text`) | `user/wtk/`, `Apps/2048` |
| Texte TrueType AA (pseudos, chat) | FreeType « lean » (`fnt::get/draw`) | `user/ft/fonts.h`, docs/03 §6 |
| Souris / clavier | `kapi_set_pointer_handler`, `kapi_set_key_handler` | `kapi.h` |
| Boucle d'événements | `kapi_pump_wait(ms)`, `kapi_should_exit` | `kapi.h` (v67) |
| TCP + DNS (non bloquant) | `kapi_tcp_connect/send/recv/close`, `kapi_net_status` | `kapi.h` (v21) |
| **TLS 1.2/1.3** (vérif. cert) | `onyx_tls::start/send/recv/stop` + `set_ca_bundle` | `user/tls/onyx_tls.hpp` |
| HTTPS one-shot (REST, assets) | `http.hpp` `Transport`/`tp_open/send/recv/close` ; modèle complet `Apps/courier/net.h` | `user/http.hpp` |
| **HMAC-SHA256 / SHA-1 / base64** (anti-bot + handshake WS) | mbedTLS 3.6.3 (`mbedtls_md_hmac`, `mbedtls_sha1`, `mbedtls_base64_encode`) | `third_party/mbedtls-3.6.3` |
| JSON | `json::Doc::parse` → `root()["k"][i].asStr/asInt/...`, `json::Writer` | `user/json.hpp` |
| PNG/JPEG/GIF/WebP/BMP → `0xAARRGGBB` | `img_load_mem(data,len,&im)` | `user/img/imgload.hpp` |
| Threads, mutex, event, futex | `kapi_thread_create`, `kapi_post`, `kapi_mutex_*`, `kapi_wait_word/wake_word` | `kapi.h` (v67/68) |
| Aléa matériel (clé WS, ti) | `kapi_random` | `kapi.h` (v30) |
| Cache | `RAM:/taatu/` (rapide, volatil), `SD:/apps/taatu.app/cache/` (persistant) | docs/03 §5.3 |
| Racines TLS | `SD:/res/ca-bundle` (PEM, déjà sur la carte) | `sdcard/res/ca-bundle` |
| Son PCM s16 44,1 kHz | `kapi_sound_acquire/write` | `kapi.h` (v46) |

> **Pas de décodeur MP3/OGG** dans Onyx : la radio/sons compressés nécessiteront un décodeur
> à licence permissive embarqué (`minimp3` CC0, `stb_vorbis` domaine public). Hors MVP.

**Licences** : lier mbedTLS (Apache-2.0, mention), FreeType (FTL, mention), stb (domaine
public). **Ne pas** lier NetSurf/`onyx_ws.c` (GPL) : on écrit notre propre client WebSocket.
Code de l'app : MIT (règle du dépôt).

---

## 4. Protocole (observé sur le client public, 2026-10-02)

> Capturé en instrumentant le `WebSocket` du client web en session réelle (login + salle
> peuplée). Les schémas ci-dessous sont **réels**. `x`/`z` = tuiles iso ; `direction` ∈ 0..7.

### 4.1. Connexion Socket.IO

- URL : l'origine (`https://taatu.world`) ; en dev local `http://127.0.0.1:3000`.
- Options : `path:'/socket.io/'`, `transports:['websocket','polling']`, `upgrade:true`,
  `forceNew:true`, `reconnection:true`.
- **Auth** (handshake Socket.IO, paquet `40`) :
  ```json
  { "token": "<taatu_session_token>", "v": "<version client>", "ui": 1 }
  ```
  `ui:1` annonce que le client sait signer (WebCrypto côté web ; mbedTLS côté Onyx).
- Jeton de session : obtenu par le login REST (§4.5), stocké (web : `localStorage
  taatu_session_token`). Onyx : `SD:/apps/taatu.app/config.ini` si « se souvenir ».

Pile, du bas vers le haut : **TCP (443) → TLS (vérif. cert) → WebSocket RFC 6455 (client,
trames masquées) → Engine.IO v4 → Socket.IO v5**. Détails §6.

### 4.2. Entrée dans une salle (séquence réelle)

```
C→S  set-absent     {roomId, absent:false, in_game:false, isAppart:false}
C→S  join-room      {roomId, isAppart:false, position_x, position_z, direction}
        ← ACK       {ok:true, position_x, position_z, spawn_ghost:false, user_id}
C→S  mc-sync []      ; simon-sync {roomId}            (sync mini-jeux, optionnel)
S→C  room-players   {players:[ <avatar complet>, ... ]}   ← liste initiale (voir 4.3)
S→C  room-tick      {updates:[ <état avatar>, ... ]}       ← broadcast périodique
S→C  room-chat-policy {chatMuted, chatSlowMode, slowRecoveryMs}
S→C  room-contest-state, pub-room-override, self-role-hidden, self-invisible,
     self-birthday, effects24-changed, bingo-presence     (état annexe de salle)
```

Sortie : `C→S leave-room {roomId, isAppart}`.

### 4.3. Modèle d'un avatar (`room-players[].players[]`)

```jsonc
{
  "user_id": 1065, "pseudo": "Lionello", "gender": "male",
  "user_status": "VIPMAX", "vip_paid_tier": "", "is_birthday": false,
  "position_x": 9, "position_z": 5, "direction": 7, "head_pointing": 0,
  "is_dancing": false, "is_sitting": false, "in_bed_slot": 0,
  "is_absent": false, "is_in_game": false, "invisible": false,
  "skin_tone_hex": "#e18863",
  "eye_sprite_path": "/images/sprite/Male/EYE/eyes_red.png",   // ou null
  "elite_bubble_color": null, "avatar_effect": null,
  "equipped_clothing": {
    "hair":   { "id":1006, "sprite_path":"/images/sprite/Male/HAIR/....png", "color_editable":1, "color_hex":null, "hide_base_sprite":false, "hide_bottom":false },
    "hat":    { ... }, "beard": { ... }, "glasses": { ... },
    "top":    { ... }, "bottom": { ... }, "shoes": { ... }
  }
}
```

**Composition du sprite** (paper-doll, dans l'ordre de couche, à confirmer côté web) :
corps de base (`gender`, teinté `skin_tone_hex`) → yeux (`eye_sprite_path`) → `bottom` →
`top` → `shoes` → `beard` → `hair` → `glasses` → `hat`. Chaque couche est une planche
directionnelle (8 directions × frames), ancrée sur la tuile. Tinte quand `color_editable`
et `color_hex`. `hide_base_sprite`/`hide_bottom` masquent des couches. Les URLs sont
`https://taatu.world<sprite_path>` (assets à télécharger + cacher).

### 4.4. Mouvement + état (broadcast)

- Clic pour se déplacer → **pathfinding côté client** :
  ```
  C→S update-destination {roomId, destination_x, destination_z,
                          path:[[x,z],[x,z],...], isAppart, _ui}
  ```
  puis, tuile par tuile le long du chemin :
  ```
  C→S update-position {roomId, position_x, position_z, direction,
                       is_dancing, is_sitting, in_bed_slot, head_pointing,
                       isAppart, [force:true], _ui}
  ```
- Le serveur ré-émet l'état de chaque avatar à tous via :
  ```
  S→C room-tick {updates:[{user_id, pseudo, gender, user_status,
                 position_x, position_z, direction, head_pointing,
                 is_dancing, is_sitting, in_bed_slot, is_absent,
                 is_in_game, invisible, force_sync}, ...]}
  ```
- `direction` ∈ 0..7 (sud, sud-ouest, … sens iso). Assise/danse/lit via les booléens.
- Autres : `S→C player-typing {user_id, typing}`, `S→C player-absent {user_id, absent,
  in_game}`, `S→C map-occupancy {rooms:{id:count}, apparts:{id:count}}`.

### 4.4b. Signature anti-bot `_ui` (reproduire fidèlement)

Le serveur envoie, après connexion :
```
S→C ui-key {k:"<64 hex>"}          // 32 octets → clé HMAC-SHA256
```
Le client **réinitialise un compteur** `c=0` à réception. Pour chaque **emit signé**, il
ajoute un champ `_ui` au payload :

```
c   = ++compteur                               // strictement croissant, DANS L'ORDRE
ti  = 1 si l'action suit un vrai input humain récent, sinon 0
      (auto/keepalive → 1 ; fenêtre : update-position 20 s, sinon 3 s)
msg = event + "|" + c + "|" + ti               // UTF-8, ex. "update-position|102|1"
sig = HMAC_SHA256(key, msg)                     // → hex minuscule (64 car.)
payload._ui = { c, ti, t: sig }
```

**Events signés** (observés) : `update-position`, `update-destination`, `chat-message`,
`room-acticon-emote`, `charlie-guess`. Les autres partent sans `_ui`.

Sur Onyx : `mbedtls_md_hmac(SHA256, key, 32, msg, len, out32)` → hex. `ti` = 1 si l'action
découle d'un vrai clic/touche kapi récent (sur un client natif, tout input EST réel : on
retient l'horodatage du dernier `GUI_EVENT_PTR_DOWN`/`GUI_EVENT_KEY`). En cas d'échec de
signature, émettre sans `_ui` (le serveur observe mais n'accuse pas, comme le web).

### 4.5. Auth REST (hors socket) — confirmé en live

Implémenté dans `core/httpc.hpp` + `core/auth.hpp`. Flux :

```
GET  /api/csrf                          -> 200 { token:"<64 hex>" }   (+ Set-Cookie session)
POST /api/auth/login  { pseudo, password[, remember] }
        -> 200 { session_token, user:{…} }           (succès)
        -> 401 { error:"Invalid name or password", success:false }
        -> 401 { mfa_required:true, challenge_id, … } (2FA)
POST /api/auth/mfa-verify { challenge_id, code[, remember] }  -> 200 { session_token, user }
GET  /api/auth/me   (X-Session-Token)   -> { user }           (rafraîchir)
```

En-têtes sur **chaque** requête : `Content-Type: application/json`, `X-Session-Token` (si
connecté), `X-Taatu-Device` (UUID v4 persistant, anti multi-compte), `X-Taatu-Fp`
(empreinte **FNV-1a 32 bits → 8 hex** de `UA|lang|langs|platform|tz|WxHxdepth|cores|
deviceMemory|maxTouchPoints`). Sur les POST : `X-CSRF-Token` (du `/api/csrf`) + le `Cookie`
capturé ; si 403 « Token CSRF invalide », re-`GET /api/csrf` et rejouer **une** fois.
Endpoints « publics » (sans session) : `auth/login`, `auth/mfa-verify`, `auth/register`,
`auth/forgot-password`, `auth/passkey-login-*`, `csrf`. Passkey (WebAuthn) existe aussi.

`fnv1a` et la crypto du handshake/`_ui` sont **validées sur vecteurs connus** côté PC.
**Ne jamais stocker le mot de passe** ; ne garder que le `session_token` (config.ini si
« se souvenir »). Prod = HTTPS (TLS) ; dev = `http://127.0.0.1:3000` (clair).

### 4.6. Catalogue d'événements observés

`C→S` : `join-room`, `leave-room`, `set-absent`, `chat-message`, `update-position`,
`update-destination`, `player-typing`, `net-ping`, `mc-sync`, `simon-sync`.

`S→C` : `room-players`, `room-tick`, `chat-message`, `player-typing`, `player-absent`,
`map-occupancy`, `room-chat-policy`, `room-contest-state`, `pub-room-override`,
`effects24-changed`, `self-role-hidden`, `self-invisible`, `self-birthday`,
`bingo-presence`, `ui-key`, + (hors salle) `force-logout`, `force-reload`,
`server:restarting`, `server:version-changed`, `moderation-*`, `incoming-*` (amis,
messages privés, marché joueur), etc.

### 4.7. Sous-systèmes additionnels (hors MVP, à cartographier)

Le client web porte bien plus que « salle + chat + déplacement ». À reproduire après le MVP,
en capturant leurs événements/REST avec la même sonde :

| Sous-système | Modules web | Protocole (indices) |
|---|---|---|
| **Vestiaire / apparence** | `ui/clothing.js`, `ui/clothing-color-palette.js`, `avatar/avatar.js` | emit `sync-room-appearance` ; REST catalogue vêtements ; recompose le paper-doll (§4.3) |
| **Trade joueur↔joueur** | `ui/trade-window.js` | `S→C incoming-player-market-offer/counter/auction/offer-outcome/wishlist-alert` ; emits d'offre/validation |
| **Marché joueur** | `ui/player-market-feature-panel.js`, `ui/player-market-fee.js` | REST + events marché ; commission |
| **Catalogue / achats meubles** | `ui/catalogue-feature-panel.js`, `room/furniture-catalog-bridge.js`, `core/appart-furniture-*` | REST catalogue ; placement meubles dans l'appart (broadcast déco) |
| **Économie (TAATU'S / VIP)** | `ui/stripe-tats-return.js`, `ui/stripe-vip-return.js`, `utils/vip-perks.js`, `ui/special-offer.js` | **Stripe** (paiement hors app : ouvrir dans un navigateur / hors Pi) ; `S→C vip-gift-received` |
| **Amis / messages privés** | `ui/chatbar-friends.js`, `ui/chatbar-messages.js` | `S→C incoming-friend-request`, `incoming-private-message`, `pm-typing`, `private-message-read` |
| **Modération** | `ui/chatbar-moderation.js`, `ui/moderation-feature-panel.js` | `S→C moderation-ban/kick`, `admin-kick-to-map`, `staff-*` ; emits `room-chat-mute/slow-*`, `room-contest-*` |
| **Mini-jeux** | `rooms/nightclub/*`, `charlie-*`, `mc-sync`, `simon-sync` | emits de sync ; `charlie-guess` **signé** `_ui` |
| **Acticons / émotes** | `acticons/ActIconPlayer.js`, `RuffleEmotePlayer.js` | emit `room-acticon-emote` **signé** ; (Flash → à réimplémenter en natif, pas de Ruffle sur Onyx) |

> **Paiements** : jamais saisir de données bancaires dans l'app native. Les flux Stripe
> restent délégués (navigateur), comme la coquille Electron tolère `checkout.stripe.com`.

---

## 5. Rendu 2.5D (isométrique)

- **Décor** : image de fond de la salle décodée une fois (`img_load_mem`) dans un
  `wtk::Canvas`, recopiée chaque frame (idéalement seulement les rectangles salis).
- **Projection** tuile→écran : à aligner **au pixel** sur la règle du client web (origine,
  largeur/hauteur de losange, offset). À extraire du web (`ground-tiles.js`,
  `map-screen-transitions.js`) lors de l'implémentation du rendu.
- **Avatars** : composés (§4.3) en `0xAARRGGBB`, **triés par profondeur** à chaque frame
  (clé = `position_z` puis `position_x`, ou `x+z`), dessinés avec alpha, ancrés au pied.
- **Hit-test** du clic : écran→tuile, puis pathfinding client (A* sur la grille marchable)
  → `update-destination`. Interpolation de marche entre tuiles pour le rendu.
- **Texte** : pseudos + bulles de chat avec FreeType (`fnt::draw`) ; `textColor` du message.
- **Cadence** : `kapi_pump_wait(16)` (≈60 fps). Décodage d'images **hors** boucle.

---

## 6. Pile réseau à écrire (dans `net.hpp`)

1. **TCP+TLS** : `kapi_tcp_connect(host,443)` → `onyx_tls::start(s, sock, host, START_VERIFY,
   &vr)` ; `set_ca_bundle` depuis `SD:/res/ca-bundle`. `recv` non bloquant (0 = rien), à
   poller dans le thread net avec `kapi_msleep`.
2. **WebSocket client (RFC 6455)** :
   - handshake `GET /socket.io/?EIO=4&transport=websocket` + `Upgrade: websocket`,
     `Connection: Upgrade`, `Sec-WebSocket-Key` (16 octets `kapi_random`, base64), `Origin`,
     `Sec-WebSocket-Version: 13` ;
   - réponse `101`, vérifier `Sec-WebSocket-Accept` = base64(SHA1(key + GUID)) ;
   - trames **masquées à l'envoi** (obligatoire client), réassemblage des fragments,
     ping(0x9)→pong(0xA), close(0x8). Ne pas proposer `permessage-deflate`.
3. **Engine.IO v4** (1er caractère du message texte) : `0{json}` open (`sid`,
   `pingInterval`, `pingTimeout`) ; serveur envoie `2` (ping) → répondre `3` (pong) ;
   `4…` = message Socket.IO ; `1` close.
4. **Socket.IO v5** (dans un `4`) : `40{auth}` connect → `40{"sid":…}` ; événements
   `42["nom",payload]` ; acks `43<id>[…]` ; erreurs `44{…}`. Un ack attendu (join-room) se
   lit en matchant l'id numérique.
5. **Reconnexion** : backoff 0,5→8 s ; sur perte, rejouer `join-room` dans la salle courante.

> `kapi_tcp_send` renvoie les octets mis en file (peut être partiel : renvoyer le reste).
> `onyx_tls::send` écrit tout ou `<0`. Tout ça dans le thread net, jamais dans la GUI.

---

## 7. Assets + cache

- URL = `https://taatu.world<sprite_path>` (décor, sprites, previews).
- Cache rapide `RAM:/taatu/` (volatil, sans usure SD) ; persistant
  `SD:/apps/taatu.app/cache/` (écrire en lot). Invalidation par le `t=` / version des URLs.
- Téléchargement HTTPS one-shot via `http.hpp` (thread « dl »), décodage `img_load_mem`.

---

## 8. Test

- **PC (sans Pi)** : `tools/tests/desktop_sim/` compile une app wtk pour le PC contre un
  faux noyau (`fakekapi.cpp`). Avec **`SIM_REALNET=1`**, les sockets sont celles du PC → on
  peut viser le vrai serveur TAATU. Idéal pour valider la pile WS/Engine.IO/Socket.IO.
- **Protocole seul** : la pile ne dépend que d'un transport `send`/`recv` → compilable PC et
  confrontée à un serveur socket.io local.
- **Pi** : carte SD Onyx complète (`cd kernel && make && make stage`), copier `taatu.app/`
  dans `SD:/apps/`. Crash → `aarch64-none-elf-addr2line -e taatu.elf <ELR>` (ELF non strippé).

Toolchain absente de la machine de dev Windows → voir
[`third_party/toolchain-aarch64/README.md`](../../../third_party/toolchain-aarch64/README.md)
(bootstrap pour la session cloud).

---

## 9. Feuille de route

- [x] Reverse du client web + capture du protocole de salle (ce document).
- [x] Squelette app : fenêtre, boucle GUI, câblage threads, `app.txt`, `Makefile`.
- [x] `core/` : WebSocket → Engine.IO v4 → Socket.IO v5 + signature `_ui` (HMAC-SHA256).
- [x] Modèle : parse `room-players` / `room-tick` / `chat-message`, émet `join-room` /
      `update-position` / `update-destination` / `chat-message`.
- [x] **Login REST complet** (`httpc.hpp` + `auth.hpp`) : csrf → login → mfa-verify,
      `device-id`/`fingerprint` ; crypto + `fnv1a` validés sur vecteurs. (PC compilé+exécuté.)
- [x] Transport **TLS côté PC** (Schannel, `platform/pc/tls_win.hpp`) : login+WS prod depuis
      le client PC. Testé contre `taatu.world:443` (csrf 200 + cookie ; login path complet).
- [x] **Pipeline de rendu** (portable, testé) : `sprite.hpp` (blit/mirror/tint), `avatar_render.hpp`
      (compositeur paper-doll : cellule 34×80, 7 frames × 5 lignes, dirs 5–7 = miroir),
      `roommap.hpp` (parse `ROOM_DATA` : origine, grille 0/1/2, textures, projection iso +
      unproject). `assets.hpp` (Onyx) : téléchargement TLS + cache + `img_load_mem`.
- [x] Intégration GUI Onyx : charge la salle (`/api/rooms/{id}` → `file_path` →
      `/src/rooms/*.js`), dessine le décor + avatars composités triés en profondeur, clic→tuile
      marchable→déplacement. (À **calibrer** : tailles de tuile / ancrage pieds, sur screenshot.)
- [x] **Client GUI PC** (`platform/pc/gui_win.cpp`, Win32 + GDI+) : même cœur + renderer que
      Onyx, décode les PNG, rend la salle live + avatars composités. Modes `--demo` (salle +
      avatar sans login) et `--login` (live, clic→déplacement, chat). Dump PNG `--shot` pour
      calibrer. **Vérifié** : rend la salle Patio + avatar sur la piste (voir hero.png).
      Origine iso auto-dérivée (`originX += zSize·tileW/4`) — à affiner (formule exacte du moteur `Iso`).
- [ ] Écran de login natif Onyx (champs pseudo/mot de passe, invite MFA) + `config.ini`.
- [ ] Bulles de chat au-dessus des têtes ; animation de marche (frames) ; escaliers (iso Z).
- [ ] Connexion + entrée en salle de bout en bout (prod ou serveur dev), reconnexion Wi-Fi.
- [ ] Rendu iso : décor + avatars (dots → sprites composés) triés en profondeur ; pseudos.
- [ ] Clic → pathfinding client → déplacement ; bulles de chat ; saisie du chat.
- [ ] Téléchargement + cache des assets.
- [ ] Son (PCM) ; radio (décodeur MP3/OGG embarqué) — ultérieur.
- [ ] Doc catalogue `docs/04-USER-GUIDE.md` + paquet (`tools/pkg`) une fois jouable.

### Checklist « prêt »

- [ ] TLS avec **vérification du certificat**.
- [ ] Login → salle → reconnexion auto après coupure.
- [ ] Décor + sprites dans le bon ordre ; clic pour se déplacer ; chat.
- [ ] Assets téléchargés + cachés ; **aucun asset/code TAATU dans le dépôt**.
- [ ] Aucune lib GPL liée ; mentions FreeType / mbedTLS.
- [ ] `app.txt` (+ `icon.bmp`) ; teste `KT->version` et message clair si noyau trop ancien.
