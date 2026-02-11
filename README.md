# Drums-Engine-MIDI

🎯 Objectif du projet

Développer un engine modulaire temps réel déterministe, orienté :

synthèse / contrôle / génération

traitement temps réel strict

architecture extensible

intégration embarquée (ESP32 / MCU / Linux RT)

pilotage MIDI / actionneurs / audio / systèmes hybrides

Contraintes prioritaires :

🔹 Latence minimale

🔹 Jitter maîtrisé

🔹 Modularité claire

🔹 Architecture C++ propre

🔹 UI minimale alignée moteur

1️⃣ Blocs fonctionnels indispensables (14 blocs)

Architecture minimale cohérente :

Core timing

Clock interne

Scheduler temps réel

Gestion des priorités

Sync externe (MIDI Clock / Trigger)

Flux / contrôle

Input manager

Event router

State manager

Param manager

Traitement

DSP / moteur de calcul

Modulation engine

Mapping engine

Sortie

Output manager

Driver hardware abstraction

Monitoring / debug

2️⃣ Structure UI minimale (alignée 1:1 avec les blocs)

Organisation simple, claire, sans surcharge :

-------------------------------------------------
| CLOCK | SCHED | SYNC | PRIORITY             |
-------------------------------------------------
| INPUT | ROUTER | STATE | PARAM              |
-------------------------------------------------
| DSP | MOD | MAP                             |
-------------------------------------------------
| OUTPUT | DRIVER | MONITOR                   |
-------------------------------------------------


Principes UI :

1 bloc = 1 zone fonctionnelle

pas de logique cachée

monitoring temps réel visible

latence affichée

charge CPU visible

état scheduler visible

UI = reflet exact de l’architecture interne.

3️⃣ Objectifs de programmation
🎯 Déterminisme

Aucun malloc en runtime critique

Buffers pré-alloués

Pas de std::vector dynamique en audio thread

Pas de mutex bloquant

🎯 Séparation stricte

Thread UI

Thread logique

Thread temps réel

Drivers isolés

🎯 Modularité

Blocs indépendants

Interface commune IBlock

Possibilité d’ajout de modules

🎯 Sécurité temps réel

Lock-free queues

Ring buffers

Double buffering

Atomic variables uniquement



Hiérarchie priorités

Audio / moteur critique

Control modulation

Mapping

Output

Monitoring

Méthode anti-jitter

Horloge monotonic

Sleep précis ou busy-wait court

Compensation dérive

Timestamping interne

🔬 Points critiques à prévoir
Mémoire

Fragmentation interdite

Pool allocator

Allocation statique privilégiée

Multi-thread

No blocking

No std::cout en RT

Pas d’exception en thread critique

Debug

Monitoring thread séparé

Dump buffer offline

Mode simulation

Portabilité

HAL pour drivers

CMake multiplateforme

Abstraction timing

📊 Roadmap
Phase 1

Core clock

Scheduler fixe

4 blocs minimalistes

Test jitter

Phase 2

Intégration DSP

Lock-free event system

Monitoring UI

Phase 3

Drivers hardware

Sync externe

Optimisation CPU

Phase 4

Modularisation avancée

Profiling complet

Benchmark latence

🧠 Philosophie

Engine minimaliste
Temps réel strict
Architecture lisible
Aucune magie cachée

Le moteur doit rester :

compréhensible

prédictible

mesurable

extensible
