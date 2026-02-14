# Drums-Engine-MIDI

Moteur embarqué temps réel pour percussion robotisée, piloté par MIDI, avec configuration via interface web sur ESP32.

## Base active
- Le code firmware actif est dans `engine/`.
- La documentation projet est centralisée dans `docs/`.

## Démarrage rapide
```bash
cd engine
pio run
```

Upload firmware + UI:
```bash
pio run -t upload
pio run -t uploadfs
```

## Documentation
- Index documentation: [`docs/README.md`](docs/README.md)
- État des lieux actuel: [`docs/system-status.md`](docs/system-status.md)
- Architecture technique: [`docs/architecture.md`](docs/architecture.md)
- API web: [`docs/web-api.md`](docs/web-api.md)
- Scheduler temps réel: [`docs/realtime-scheduler.md`](docs/realtime-scheduler.md)
- Roadmap: [`docs/roadmap.md`](docs/roadmap.md)

## Licence
Projet distribué sous licence MIT (voir `LICENSE`).
