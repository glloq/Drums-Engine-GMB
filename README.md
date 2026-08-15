# Drums-Engine-MIDI

Moteur embarque temps reel pour percussion robotisee, pilote par MIDI, avec configuration via interface web sur ESP32.

## Base active
- Le code firmware actif est dans `engine/`.
- La documentation projet est centralisee dans `docs/`.

## Demarrage rapide

### PlatformIO (recommande)
```bash
cd engine
pio run                 # Compiler
pio run -t upload       # Flasher firmware
pio run -t uploadfs     # Uploader UI web (LittleFS)
```

L'interface web servie par le firmware est embarquee en PROGMEM. Apres toute
modification de `engine/data/index.html`, regenerez l'en-tete embarque :
```bash
python3 tools/gen_index_gz.py
```

### Arduino IDE 2.x
1. Ouvrir `engine/engine.ino`
2. Installer les libs requises (voir [`docs/deployment-guide.md`](docs/deployment-guide.md))
3. Board: ESP32 Dev Module
4. Compiler (Ctrl+R) et flasher (Ctrl+U)

Au premier boot, l'ESP32 demarre en mode AP (`MidiPerc-Setup`/`midiperc`).
Connectez-vous et configurez le WiFi via `POST /api/wifi`.

## Tests API
```bash
# Lancer les 31 tests automatises contre l'ESP32
./tests/test_api.sh http://midipercussion.local [API_TOKEN]
```
Le token est affiche au boot sur Serial ou via `GET /api/auth-token`.

## Architecture

```
Core 1 (RT 1kHz)                    Core 0 (App)
┌────────────────────┐              ┌──────────────────┐
│ MIDI Layer         │              │ Web Server REST  │
│ Event Processor    │              │ Loop Engine      │
│ Scheduler          │              │ WiFi Manager     │
│ Actuator Manager   │              │ Storage LittleFS │
│ HAL Drivers        │              └──────────────────┘
└────────────────────┘
```

Protections concurrence : spinlocks dual-core, mutex I2C, ISR atomique, ecriture LittleFS atomique.

## Documentation
- Index documentation: [`docs/README.md`](docs/README.md)
- Etat des lieux actuel: [`docs/system-status.md`](docs/system-status.md)
- Architecture technique: [`docs/architecture.md`](docs/architecture.md)
- API web: [`docs/web-api.md`](docs/web-api.md)
- Scheduler temps reel: [`docs/realtime-scheduler.md`](docs/realtime-scheduler.md)
- Guide de deploiement: [`docs/deployment-guide.md`](docs/deployment-guide.md)
- Page Cablage (branchements generes): [`docs/wiring-page.md`](docs/wiring-page.md)
- Audit production: [`docs/production-readiness.md`](docs/production-readiness.md)
- Roadmap: [`docs/roadmap.md`](docs/roadmap.md)

## Licence
Projet distribue sous licence MIT (voir `LICENSE`).
