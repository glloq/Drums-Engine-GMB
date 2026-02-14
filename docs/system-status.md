# État des lieux actuel

## Résumé
Le système actuel est opérationnel sur ESP32 (Arduino + FreeRTOS) avec une architecture modulaire et une UI web embarquée.

## Capacités déjà implémentées
- Scheduler temps réel basé sur timer hardware (`SCHEDULER_TICK_HZ=1000`).
- Traitement MIDI et routage vers pipelines compilés.
- Gestion d'actionneurs: solénoïde, servo, moteur PWM.
- HAL I2C/GPIO/LEDC pour MCP23017, PCA9685, GPIO natif.
- API web REST + WebSocket pour CRUD instruments/actionneurs/boucles.
- Endpoint de capacités (`/api/capabilities`) exploité par l'UI pour guider les choix type↔bus.
- Persistance LittleFS (`actuators.json`, `instruments.json`, `loops`).
- Support de migration de configuration legacy V4 dans la couche storage.

## Limites identifiées
- Pipelines V1 encore volontairement minimalistes (construction avancée incomplète).
- Portabilité hors ESP32 non finalisée.
- Benchmarks latence/jitter sous charge à formaliser dans une méthode unique.
- Peu de documentation de référence orientée exploitation (tests, diagnostics, troubleshooting).

## Priorités court terme
1. Stabiliser les tests de performance temps réel.
2. Renforcer la validation des payloads API et la gestion d'erreurs.
3. Finaliser le flux UI → compilation pipeline.
4. Mettre en place une documentation opérationnelle de test matériel.

## À prévoir (moyen/long terme)
- Blocs avancés (humanize, random, LFO, sync externe).
- Portabilité ESP-IDF natif / Linux RT.
- Outils de diagnostic (traces, métriques exportables).
- Stratégie CI avec tests d'intégration embarqués/simulés.
