# Documentation Drums-Engine-MIDI

Ce dossier regroupe la documentation utile et maintenue du projet.

## Lecture recommandée
1. [`system-status.md`](system-status.md) — état actuel du système, limites, priorités.
2. [`architecture.md`](architecture.md) — architecture logicielle et flux d'exécution.
3. [`web-api.md`](web-api.md) — endpoints REST/WebSocket exposés par le firmware.
4. [`realtime-scheduler.md`](realtime-scheduler.md) — objectifs de timing et stratégie scheduler.
5. [`blocks-v1.md`](blocks-v1.md) — catalogue minimal des blocs pipeline V1.
6. [`roadmap.md`](roadmap.md) — plan d'évolution.
7. [`ui-v1.md`](ui-v1.md) — structure UI cible.
8. [`code-audit.md`](code-audit.md) — audit du code actuel et plan de modifications.
9. [`frontend-backend-linkage.md`](frontend-backend-linkage.md) — contrôle des liaisons UI/API.

## Règles de maintenance
- Toute nouvelle doc doit être ajoutée ici (pas à la racine).
- Privilégier des fichiers `.md` nommés clairement.
- Documenter l'état réel du code (`engine/src`) avant la vision long terme.
