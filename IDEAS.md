# Idées — Zircon

Liste vivante des idées de fonctionnalités et d'applications pour Zircon (OS custom sur Circle).

Statuts : 💡 idée · 🔨 en cours · ✅ fait · ❄️ reporté

---

## Applications

| Idée | Statut | Notes |
|------|--------|-------|
| Cardfile — fiches/formulaires (façon Cardfile Win 3.11) | 💡 | Mode **édition** : définir le formulaire (liste de champs modifiable). Mode **visualisation** : CRUD sur les enregistrements (créer/modifier/lire/supprimer). Stockage des schémas + données sur SD. |

## Fonctionnalités (système / kernel)

| Idée | Statut | Notes |
|------|--------|-------|
| Compositor : dirty-rect + flush async | 💡 | Aujourd'hui : redraw plein écran chaque frame. Optimisation perf différée. |
| Port audio : synth FM + tracker | ❄️ | Amener le synth FM + tracker dans Zircon. Fixed-point + PWM. |
| Filer : associations de fichiers via ini | ❄️ | `[fileassoc] .ext=programme` (aujourd'hui hardcodé dans `open_file`). |
| Filer : copier/couper → coller (presse-papiers fichier) | 💡 | Copier/couper mémorise le path ; coller ailleurs fait copy (copier) ou move (couper). |

---

## Idées brutes (à trier)

_Notes en vrac, à classer plus tard._
