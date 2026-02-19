# Pipeline — Logique de traitement MIDI → Actionneur

## Vue d'ensemble

Chaque **instrument** possède un **pipeline** : une chaîne de blocs de traitement qui transforme une vélocité MIDI entrante avant de l'envoyer vers un actionneur physique (moteur, solénoïde, servo…).

```
Note MIDI (vélocité 0-127)
  │
  ▼
┌─────────────────────────┐
│  Lookup note → pipeline │  O(1) via table note_to_pipeline[128]
└──────────┬──────────────┘
           ▼
┌─────────────────────────┐
│  Bloc 1 (condition?)    │──▶ bloqué → pipeline interrompu, aucune commande
│  Bloc 2 (transform?)    │
│  Bloc 3 (time/output?)  │──▶ terminal → commande envoyée, pipeline terminé
│  ...                     │
│  (max 8 blocs)          │
└──────────┬──────────────┘
           ▼
  Aucun bloc terminal atteint ?
  → commande PULSE par défaut vers output_actuator_id
           ▼
┌─────────────────────────┐
│  Scheduler              │  Ordonne par timestamp
└──────────┬──────────────┘
           ▼
┌─────────────────────────┐
│  ActuatorManager        │  Dispatch vers le driver matériel
└─────────────────────────┘
```

---

## Structures de données

### CompiledBlock

```c
struct CompiledBlock {
  uint8_t type;     // BlockType : CONDITION(0), TRANSFORM(1), TIME(2), OUTPUT_BLOCK(3)
  uint8_t subtype;  // Sous-type selon le type
  uint8_t param1;   // Paramètre 1 (sémantique variable)
  uint16_t param2;  // Paramètre 2 (sémantique variable)
};
```

### CompiledPipeline

```c
struct CompiledPipeline {
  uint8_t block_count;                                // 0 à MAX_BLOCKS_PER_PIPELINE (8)
  uint8_t output_actuator_id;                         // Actionneur par défaut (0xFF = auto)
  CompiledBlock blocks[MAX_BLOCKS_PER_PIPELINE];
  ActionStep note_on_actions[MAX_ACTIONS_PER_EVENT];  // Actions directes NoteOn (bypass blocs)
  uint8_t note_on_count;
  ActionStep note_off_actions[MAX_ACTIONS_PER_EVENT]; // Actions directes NoteOff
  uint8_t note_off_count;
  CCBinding cc_bindings[MAX_CC_BINDINGS];             // Mappings CC
  uint8_t cc_binding_count;
  uint8_t retrigger_mode;                             // IGNORE(0), RESET(1), STACK(2)
  uint8_t alternate_group_count;                      // 0 = pas d'alternance, N = round-robin
};
```

### Limites système

| Constante                    | Valeur | Description                      |
|------------------------------|--------|----------------------------------|
| `MAX_PIPELINES`              | 32     | Nombre max de pipelines          |
| `MAX_BLOCKS_PER_PIPELINE`    | 8      | Blocs max par pipeline           |
| `MAX_ACTIONS_PER_EVENT`      | 4      | Actions NoteOn/NoteOff max       |
| `MAX_CC_BINDINGS`            | 4      | Bindings CC max par pipeline     |
| `MAX_CC_ROUTES`              | 16     | Routes CC totales (aplaties)     |
| `MAX_GLOBAL_VARS`            | 128    | Variables globales (CC, custom)  |

---

## Types de blocs

### CONDITION (type 0) — Filtres

Les blocs condition laissent passer ou bloquent le pipeline. Si un bloc retourne `-1`, **le pipeline est immédiatement interrompu** et aucune commande n'est envoyée.

| Subtype | Id            | Description                                      | param1                          | param2               |
|---------|---------------|--------------------------------------------------|---------------------------------|----------------------|
| 0       | `threshold`   | Compare une variable ou la vélocité à un seuil   | bit7=mode, bits0-6=var (0x7F=vel) | seuil (0-127)     |
| 1       | `round_robin` | Alternance round-robin entre N groupes           | index du groupe cible (0-based) | nombre total de groupes |
| 2       | `velocity_split` | Filtre par plage de vélocité                  | vélocité min                    | vélocité max         |
| 3       | `random`      | Porte probabiliste                               | réservé                         | probabilité (0-100%) |
| 4       | `note_range`  | Filtre par plage de note MIDI                    | note min                        | note max             |

**Threshold — détails du param1 :**
- `bit7 = 0` : la valeur doit être **≥** param2 pour passer
- `bit7 = 1` : la valeur doit être **<** param2 pour passer
- `bits 0-6` : index de variable (0-126 = variable globale, `0x7F` = vélocité courante)

---

### TRANSFORM (type 1) — Transformations de valeur

Les blocs transform modifient la vélocité courante et la transmettent au bloc suivant.

| Subtype | Id               | Description                        | param1                    | param2                      |
|---------|------------------|------------------------------------|---------------------------|-----------------------------|
| 0       | `velocity_curve` | Applique une courbe                | type courbe (0/1/2)       | réservé                     |
| 1       | `gain`           | Multiplie la vélocité              | réservé                   | gain % (10-400, 100 = ×1)  |
| 2       | `clamp`          | Limite à une plage                 | min (0-127)               | max (0-127)                 |
| 3       | `invert`         | Inverse la valeur (127 - val)      | réservé                   | réservé                     |
| 4       | `set_var`        | Stocke la vélocité dans une variable globale (pass-through) | index variable (0-127) | réservé |

**Courbes disponibles (param1 de velocity_curve) :**
- `0` LINEAR — pas de changement
- `1` EXPO — `(normalisé²) × 127` → réponse progressive
- `2` LOG — `√(normalisé) × 127` → réponse rapide puis plateau

---

### TIME (type 2) — Contrôle temporel

| Subtype | Id         | Description                                   | param1                          | param2           | Terminal ? |
|---------|------------|-----------------------------------------------|---------------------------------|------------------|-----------|
| 0       | `pulse`    | Mappe vélocité → durée de pulse, envoie cmd   | durée min ms (0 = durée fixe)   | durée max ms     | **OUI**   |
| 1       | `delay`    | Accumule un délai                             | réservé                         | délai ms         | non       |
| 2       | `ramp`     | Génère des micro-steps progressifs            | nb steps (0 = auto: durée/5ms)  | durée totale ms  | **OUI**   |
| 3       | `gate`     | Rate-limiter (intervalle minimum)             | réservé                         | intervalle min ms | non       |

**Pulse (subtype 0) — détails :**
- Si `param1 > 0` : durée = `map(vélocité, 0, 127, param1, param2)` → modulation dynamique
- Si `param1 == 0` : durée fixe = param2 ms, quelle que soit la vélocité

**Delay (subtype 1) :**
- Plusieurs blocs delay s'additionnent (accumulation)
- Le délai s'applique à la commande finale

**Ramp (subtype 2) :**
- `param1 = 0` (auto) : nombre de steps = `durée / 5ms`, clamped [2, 20]
- Génère N commandes POSITION de 0 → vélocité courante
- `stepVal[i] = (vélocité × (i+1)) / stepCount`

**Gate (subtype 3) :**
- Bloque le pipeline si la dernière activation < `param2` ms
- Utile pour limiter le taux de frappe

---

### OUTPUT (type 3) — Sortie vers actionneur

Tous les blocs OUTPUT sont **terminaux** : ils génèrent une commande et arrêtent le pipeline.

| Subtype | Id         | Description                    | param1        | param2                         |
|---------|------------|--------------------------------|---------------|--------------------------------|
| 0       | `pulse`    | Envoie une commande PULSE      | actuator ID   | durée (0 = dérivée vélocité)   |
| 1       | `position` | Envoie une commande POSITION   | actuator ID   | réservé                        |
| 2       | `pwm`      | Envoie une commande PWM        | actuator ID   | réservé                        |
| 3       | `off`      | Envoie une commande OFF        | actuator ID   | réservé                        |

Le `param1` (actuator ID) permet de rediriger vers un actionneur **différent** de l'actionneur par défaut du pipeline.

---

## Algorithme de traitement

Fichier : `engine/src/event/event_processor.cpp` — `_executePipeline()`

```
executePipeline(pipelineIdx, velocity, timestamp, midiNote):
  pipeline = lookup.pipelines[pipelineIdx]
  currentValue = velocity
  delayAccum = 0

  pour chaque bloc dans pipeline.blocks:
    switch bloc.type:
      CONDITION:
        result = processCondition(bloc, currentValue, pipelineIdx, midiNote)
        si result < 0 → RETURN (pipeline bloqué)
        currentValue = result

      TRANSFORM:
        currentValue = processTransform(bloc, currentValue)

      TIME:
        si DELAY    → delayAccum += bloc.param2 × 1000 µs
        si PULSE    → envoie commande pulse → RETURN (terminal)
        si RAMP     → envoie micro-steps   → RETURN (terminal)
        si GATE     → vérifie intervalle, bloque ou passe

      OUTPUT:
        envoie commande vers bloc.param1 (actuator) → RETURN (terminal)

  // Fin de boucle sans terminal
  si output_actuator_id != 0xFF:
    envoie PULSE(currentValue) vers output_actuator_id avec delayAccum
```

### Cas particuliers

| Situation                  | Comportement                                                     |
|----------------------------|------------------------------------------------------------------|
| Pipeline vide (0 blocs)    | Envoie PULSE par défaut vers `output_actuator_id`                |
| Condition échoue           | Pipeline interrompu immédiatement, aucune commande               |
| Pas de bloc terminal       | Commande PULSE par défaut avec le délai accumulé                 |
| `output_actuator_id = 0xFF`| Aucune commande par défaut (pipeline silencieux sans terminal)   |
| Bloc terminal rencontré    | Commande envoyée, blocs suivants ignorés                         |

---

## Routing MIDI

### NoteOn

```
NoteOn (note, velocity)
  │
  ├─ note_to_pipeline[note] == 0xFF → ignoré
  │
  ├─ note déjà active ? → selon retrigger_mode :
  │     IGNORE → rien
  │     RESET  → fire NoteOff actions, puis retraiter
  │     STACK  → incrémente compteur, traiter en parallèle
  │
  ├─ note_on_actions définis ? → fire actions directes (BYPASS pipeline)
  │
  └─ sinon → _executePipeline()
```

### NoteOff

```
NoteOff (note)
  │
  ├─ note pas active → ignoré
  │
  ├─ STACK et compteur > 1 → décrémenter, pas d'action
  │
  ├─ note_off_actions définis → fire actions directes
  │
  └─ sinon → envoie OFF vers output_actuator_id
```

### CC / Pitch Bend / Aftertouch

```
CC (cc_number, value)
  │
  ├─ Stocke dans variables[cc_number]
  │
  ├─ Anti-flood : max 1 dispatch / 5ms par CC#
  │
  └─ Pour chaque route cc_routes[cc_number] :
       - Applique inversion si configurée
       - Applique courbe (linear/expo/log)
       - Mappe [0-127] → [range_min, range_max]
       - Envoie commande vers actuator
```

**Routages spéciaux :**
- **Pitch Bend** → CC virtuel #126 (0-16383 → 0-127)
- **Channel Aftertouch** → CC virtuel #125
- **Poly Aftertouch** → CC virtuel #125

---

## Modes Retrigger

| Mode       | Valeur | NoteOn pendant note active              | NoteOff pendant STACK          |
|------------|--------|-----------------------------------------|--------------------------------|
| **IGNORE** | 0      | Ignoré                                  | —                              |
| **RESET**  | 1      | Fire NoteOff, reset, puis nouveau NoteOn| —                              |
| **STACK**  | 2      | Incrémente compteur, traite les deux    | Décrémente ; actions au dernier|

---

## API Web

### Schéma des blocs

```
GET /api/pipeline/block-schema
```

Retourne la définition complète de tous les types/sous-types de blocs avec leurs paramètres, valeurs par défaut, descriptions, et flag `terminal`.

### Lecture du pipeline

```
GET /api/pipeline/blocks?instrumentId=<id>
```

Retourne :
```json
{
  "instrumentId": 1,
  "blockCount": 2,
  "outputActuatorId": 5,
  "retriggerMode": 0,
  "blocks": [
    {"type": "condition", "subtype": 0, "param1": 127, "param2": 50},
    {"type": "transform", "subtype": 1, "param1": 0, "param2": 150}
  ],
  "noteOnActions": [...],
  "noteOffActions": [...],
  "ccBindings": [...]
}
```

### Écriture du pipeline

```
PUT /api/pipeline/blocks?instrumentId=<id>
```

Body identique à la réponse GET (sans `instrumentId`/`blockCount`). Déclenche une recompilation du `PipelineLookup`.

### Test du pipeline

```
POST /api/pipeline/test
Body: {"instrumentId": 1, "velocity": 80}
```

Crée un NoteOn synthétique et l'exécute à travers le pipeline. Utile pour tester sans contrôleur MIDI.

---

## Compilation

Fichier : `engine/src/event/pipeline_compiler.cpp`

À chaque sauvegarde d'instrument ou au démarrage, le compilateur :

1. Parcourt tous les instruments activés
2. Pour chaque instrument, compile les blocs JSON → `CompiledBlock`
3. Construit la table `note_to_pipeline[128]` pour lookup O(1)
4. Construit la table de routage CC aplatie (`cc_to_first[]`, `cc_to_count[]`)
5. Le `PipelineLookup` résultant est utilisé par `EventProcessor` en runtime

---

## Interface utilisateur

### Éditeur de pipeline (index.html)

L'éditeur est accessible depuis la fiche d'un instrument. Il comporte :

- **Bouton "Ajouter un bloc de traitement"** — affiché quand le pipeline est vide
- **Section Blocs Pipeline** — apparaît dès qu'un bloc existe, avec :
  - Visualisation en chaîne avec flèches ↓
  - Icônes par type (condition, transform, time, output)
  - Boutons réordonner (↑↓), éditer (✎), supprimer (×)
  - Badge compteur de blocs
  - Bouton "+ Ajouter un bloc" en bas de liste
- **Picker de blocs** — modal affichant les 4 types × sous-types
- **Éditeur de paramètres** — formulaire dynamique selon le schéma du bloc
- **Actionneur de sortie par défaut** — sélecteur dropdown
- **Mode Retrigger** — Ignore / Reset / Stack
- **Actions NoteOn/NoteOff** — commandes directes (bypass pipeline)
- **Bindings CC** — mappages CC → actionneur

### Flux de sauvegarde

```
Utilisateur modifie le pipeline dans l'UI
  → peSavePipeline() collecte blocks, actions, CC bindings
  → PUT /api/pipeline/blocks?instrumentId=X
  → Backend met à jour le JSON instrument
  → PipelineCompiler::compileAll() reconstruit le PipelineLookup
  → EventProcessor utilise les nouvelles tables
```

---

## Exemples de pipelines

### Pipeline simple — frappe directe

```
[vide] → PULSE par défaut vers output_actuator_id
```

La vélocité MIDI est envoyée directement comme commande PULSE.

### Pipeline avec courbe et seuil

```
[CONDITION threshold ≥ 30] → [TRANSFORM velocity_curve EXPO] → PULSE par défaut
```

Ignore les vélocités faibles (< 30), applique une courbe exponentielle, puis frappe.

### Pipeline avec délai et ramp

```
[TRANSFORM gain ×200%] → [TIME delay 50ms] → [TIME ramp auto 100ms]
```

Double la vélocité, attend 50ms, puis fait une montée progressive sur 100ms.

### Pipeline avec alternance

```
[CONDITION round_robin groupe 0 sur 2] → [OUTPUT pulse actuator 3]
```

Alterne entre deux groupes : ce bloc ne laisse passer qu'une frappe sur deux.
