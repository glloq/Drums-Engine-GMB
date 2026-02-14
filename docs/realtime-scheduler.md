# Scheduler temps réel

## Objectifs
- Latence bout-en-bout stable: cible 1–2 ms.
- Tick scheduler: 1 kHz (`SCHEDULER_TICK_HZ`).
- Jitter réduit avec timer hardware.
- Exécution non bloquante.

## Modèle
1. Réception MIDI.
2. Event processor compile/évalue pipeline.
3. Génération de commandes horodatées.
4. Scheduler dispatch quand `execute_at <= now`.
5. Actuator manager exécute la commande sur le driver adapté.

## Découpage tâches
- **RT Core**: MIDI update + scheduler update + watchdog.
- **App Core**: boucle applicative + nettoyage websocket.

## Sécurité
- Watchdog périodique des actionneurs.
- Limites max d'activation (solénoïde/moteur).
- Contrôle du nombre d'actionneurs actifs simultanément.

## Recommandations d'amélioration
- Ajouter un protocole de benchmark standard (idle, charge moyenne, stress).
- Publier les métriques jitter/latence dans l'API status.
- Ajouter des tests de non-régression sur la queue scheduler.
