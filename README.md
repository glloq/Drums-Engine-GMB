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

Au premier boot, l'ESP32 demarre en mode AP (`MidiPerc-Setup`/`midiperc`).
Connectez-vous et configurez le WiFi via `POST /api/wifi`.

## Tests API
```bash
# Lancer les 31 tests automatises contre l'ESP32
./tests/test_api.sh http://midipercussion.local [API_TOKEN]
```
Le token est affiche au boot sur Serial ou via `GET /api/auth-token`.

## Documentation
- Index documentation: [`docs/README.md`](docs/README.md)
- État des lieux actuel: [`docs/system-status.md`](docs/system-status.md)
- Architecture technique: [`docs/architecture.md`](docs/architecture.md)
- API web: [`docs/web-api.md`](docs/web-api.md)
- Scheduler temps réel: [`docs/realtime-scheduler.md`](docs/realtime-scheduler.md)
- Guide de deploiement: [`docs/deployment-guide.md`](docs/deployment-guide.md)
- Audit production: [`docs/production-readiness.md`](docs/production-readiness.md)
- Roadmap: [`docs/roadmap.md`](docs/roadmap.md)

## Licence
Projet distribué sous licence MIT (voir `LICENSE`).
