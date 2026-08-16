# Documentation Drums-Engine-MIDI

Ce dossier regroupe la documentation utile et maintenue du projet.

## Lecture recommandee
1. [`system-status.md`](system-status.md) — etat actuel du systeme, limites, priorites.
2. [`architecture.md`](architecture.md) — architecture logicielle, concurrence, flux d'execution.
3. [`web-api.md`](web-api.md) — endpoints REST/WebSocket exposes par le firmware.
4. [`realtime-scheduler.md`](realtime-scheduler.md) — objectifs de timing, scheduler, loop engine.
5. [`roadmap.md`](roadmap.md) — plan d'evolution (Phases 1-7).
6. [`frontend-backend-linkage.md`](frontend-backend-linkage.md) — controle des liaisons UI/API.
7. [`deployment-guide.md`](deployment-guide.md) — guide de deploiement complet (flash, WiFi, securite, MIDI).
8. [`wiring-page.md`](wiring-page.md) — page "Cablage" : dossier de branchements electriques genere depuis la configuration.
9. [`production-readiness.md`](production-readiness.md) — audit production et historique des sprints.
10. [`memory-budget.md`](memory-budget.md) — cout RAM des tables statiques et limites de configuration.

## Regles de maintenance
- Toute nouvelle doc doit etre ajoutee ici (pas a la racine).
- Privilegier des fichiers `.md` nommes clairement.
- Documenter l'etat reel du code (`engine/src`) avant la vision long terme.
