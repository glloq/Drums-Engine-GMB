# Drums-Engine-MIDI

## Présentation actuelle
Drums-Engine-MIDI est un moteur **temps réel modulaire** pour piloter des instruments de percussion robotisés depuis des événements MIDI.

Le système actuel cible principalement **ESP32 (Arduino + FreeRTOS)** avec :
- une architecture en couches (HAL → Actuator → Scheduler → Event → MIDI → Web),
- un scheduler à base de timer hardware,
- une API web (REST + WebSocket) pour configurer et superviser le moteur,
- une persistance LittleFS pour les actionneurs, instruments et boucles.

Le code actif du projet est concentré dans le dossier `engine/`.

---

## État des lieux technique (système actuel)
### Ce qui est déjà en place
- **Cœur temps réel** : scheduler avec file de commandes et exécution horodatée.
- **Chaîne événementielle** : réception MIDI, routage par pipeline compilé, dispatch vers actionneurs.
- **Actionneurs supportés** : solénoïde, servo, moteur PWM.
- **Couche HAL** : drivers GPIO/LEDC et I2C (MCP23017, PCA9685).
- **Configuration & stockage** : sauvegarde/chargement JSON (LittleFS), gestion des instruments/actuateurs/boucles.
- **Interface web embarquée** : pages statiques + endpoints de configuration et de test.

### Limites constatées
- Le compilateur de pipeline reste orienté **V1 minimal** (blocs de base, logique volontairement simple).
- Le moteur dépend encore fortement de la cible ESP32 (portabilité MCU/Linux RT à consolider).
- Les métriques de performance (latence/jitter sous charge) ne sont pas encore formalisées dans un protocole de benchmark unique.
- La documentation est riche en vision et architecture, mais partiellement dispersée dans plusieurs fichiers racine.

---

## État des lieux documentaire
### Documents disponibles (racine du dépôt)
- `Roadmap` : plan macro des sprints et livrables techniques.
- `architecture interne C++` : découpage en couches et structures fondamentales.
- `liste minimale des blocs indispensables.md` : définition des blocs V1.
- `objectif de programmaion pour la construction de cet engine` : vision produit/technique.
- `scheduler temps réel précis avec timing cible.` et `structure UI simple` : notes de conception complémentaires.

### Lecture recommandée
1. `README.md` (ce document) pour la vue d’ensemble.
2. `Roadmap` pour la stratégie d’exécution.
3. `architecture interne C++` pour le modèle logiciel.
4. Le code `engine/src/` pour la vérité d’implémentation actuelle.

---

## Nettoyage effectué
- **Suppression de l’ancienne version `V4_ESP32/`** du dépôt.
- Le projet s’appuie désormais sur l’implémentation consolidée dans `engine/`.

---

## Améliorations prévues (court terme)
- Stabiliser et documenter un **workflow de validation temps réel** (latence, jitter, charge CPU).
- Compléter la **construction de pipeline** côté UI (édition plus fine des blocs et visualisation de compilation).
- Renforcer la robustesse de l’API web (gestion d’erreurs homogène, validation stricte des payloads).
- Formaliser des scénarios de test pour les instruments multi-actionneurs (alternance, split vélocité, sécurité thermique).

## Améliorations à prévoir (moyen / long terme)
- Portabilité vers d’autres cibles (ESP-IDF natif, autres MCU, Linux RT).
- Extension du moteur de blocs : humanize, random, LFO, clock sync, modes avancés.
- Outils de diagnostic en continu (traces temps réel, profils de gigue, export de sessions de test).
- Industrialisation du projet : versioning de configuration, tests d’intégration automatisés, documentation “contributeur” unifiée.

---

## Démarrage rapide
```bash
cd engine
pio run
```

Chargement firmware + filesystem web :
```bash
pio run -t upload
pio run -t uploadfs
```

