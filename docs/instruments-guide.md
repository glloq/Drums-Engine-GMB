# Guide de creation d'instruments — Drums Engine MIDI

## Prerequis

Avant de creer un instrument, vous devez avoir au moins un **actionneur** configure (solenoide, servo ou moteur). Chaque instrument associe une **note MIDI** a un ou plusieurs actionneurs.

### Workflow global

```
1. Configurer les actionneurs (onglet Actionneurs)
2. Creer un instrument (onglet Instruments)
3. Envoyer du MIDI (port 5004) → l'instrument joue
```

---

## Types d'actionneurs supportes

### Solenoide

Electro-aimant qui frappe mecaniquement. Ideal pour percussion directe.

| Comportement | Description | Parametres |
|-------------|-------------|------------|
| **Strike** | Frappe simple on/off. La velocite MIDI controle la duree d'activation. | `temps min` (vel=1), `temps max` (vel=127), `cooldown` |
| **Hold (PWM)** | Maintien en position avec reduction PWM progressive. Pour les mutes sur NoteOn/NoteOff. | `PWM activation`, `PWM maintien`, `temps max PWM`, `temps actif max` |
| **Mute** | Solenoide qui etouffe le son. Active sur NoteOn ou NoteOff. | Memes parametres que Hold |

**Quand utiliser :**
- **Strike** → kick, snare, tom, cymbale — tout ce qui frappe
- **Hold** → mute de cymbale (etouffe tant que maintenu)
- **Mute** → meme principe que Hold, optimise pour le role de mute

**Preset rapide :** `Solenoide - Strike (Kick/Snare/Tom)`, `Solenoide - Mute`

**Bus typique :** MCP23017 (I2C GPIO) — un module gere 16 solenoides

**Exemple de configuration :**
```
Type:         Solenoide
Comportement: Strike
Bus:          MCP23017 (I2C GPIO)
Pin:          1
Temps min:    8 ms   (frappe douce, vel=1)
Temps max:    30 ms  (frappe forte, vel=127)
Cooldown:     20 ms  (protection thermique)
```

---

### Servo

Moteur positionnable avec precision angulaire. Pour les mouvements controles.

| Comportement | Description | Parametres |
|-------------|-------------|------------|
| **Position** | Position continue : la velocite MIDI ou un CC controle l'angle. | `angle min`, `angle max`, `angle repos` |
| **Strike** | Oscillation entre 2 angles a chaque NoteOn. Simule un batteur. | `angle min`, `angle max` |
| **Comb/Brush** | Mouvement de brosse (balai de jazz). | `angle min`, `angle max` |
| **Pitch Bend** | Tension de peau (change la hauteur du son). | `angle min`, `angle max` |
| **Hi-Hat Controller** | Servo pilote par CC#4 (pedale hi-hat). Position continue. | `angle min` (ferme), `angle max` (ouvert) |
| **Mute** | Servo qui etouffe le son. Monte/descend sur NoteOn/NoteOff. | `angle min` (mute), `angle max` (libre) |

**Quand utiliser :**
- **Position** → hi-hat pedale (CC continu), controle de damper
- **Strike** → maracas, shaker, baguette servo
- **Comb/Brush** → balais de jazz, brosses
- **Hi-Hat Controller** → pedale de hi-hat (pilotee par CC#4)
- **Mute** → etouffer une cymbale ou un tom

**Preset rapide :** `Servo - Mute`, `Servo - Strike`, `Servo - Pedale Hi-Hat`

**Bus typique :** PCA9685 (I2C PWM) — un module gere 16 servos

**Exemple de configuration :**
```
Type:         Servo
Comportement: Hi-Hat Controller
Bus:          PCA9685 (I2C PWM)
Pin:          1
Angle min:    15°  (hi-hat ferme)
Angle max:    90°  (hi-hat ouvert)
Angle repos:  90°  (ouvert au demarrage)
```

---

### Moteur

Moteur DC pour rotations, balayages et mouvements continus.

| Comportement | Description | Parametres |
|-------------|-------------|------------|
| **Rotation (optique)** | Tourne et compte les passages via un capteur optique. Nombre de tours = velocite. | `PWM min/max`, `Pin capteur GPIO` |
| **Chrono (duree)** | Tourne pendant une duree fixe puis s'arrete. La velocite controle la vitesse. | `PWM min/max` |
| **Vitesse continue** | Tourne en continu. NoteOn = demarre (vel→vitesse), NoteOff = stoppe. | `PWM min/max` |
| **Balayage (sweep)** | Avance jusqu'a la fin de course puis s'arrete. Avec 2 fins de course : aller-retour. | `PWM min/max`, `Fin de course 1/2 GPIO` |
| **Alternance** | Alterne la direction entre 2 fins de course a chaque NoteOn. | `PWM min/max`, `Fin de course 1/2 GPIO` |

**Quand utiliser :**
- **Rotation optique** → rouleau de tambour, percussion rotative avec position precise
- **Chrono** → actionner un mecanisme pendant un temps donne (ex: frotter une surface)
- **Vitesse continue** → ventilateur, rouleau, tout ce qui tourne tant que la note est tenue
- **Balayage** → bras mecanique aller-retour, essuie-glace
- **Alternance** → mouvement pendulaire, cloche

**Preset rapide :** `Moteur - Rotation optique`, `Moteur - Chrono`, `Moteur - Vitesse continue`, `Moteur - Balayage`, `Moteur - Alternance`

**Bus typique :** ESP32 LEDC PWM (controle direct de vitesse)

**Exemple de configuration :**
```
Type:         Moteur
Comportement: Balayage (sweep)
Bus:          ESP32 LEDC PWM
Pin:          25
PWM min:      20%  (vitesse minimale)
PWM max:      100% (vitesse maximale)
Fin de course 1: GPIO 34 (micro-switch depart)
Fin de course 2: GPIO 35 (micro-switch arrivee)
Inverser sens:    non
```

---

## Types d'instruments

### Simple (1 actionneur frappe)

Le type le plus courant. Un actionneur frappe a chaque NoteOn.

**Champs :**
- Actionneur de frappe (selecteur)

**Exemple — Grosse caisse :**
```
Type:       Simple
Nom:        Kick
Note MIDI:  C1 (36)
Actionneur: Kick Solenoid (solenoide strike)
Retrigger:  Ignore
```

**Fonctionnement :**
- NoteOn → actionneur frappe (duree proportionnelle a la velocite)
- NoteOff → rien (la frappe est deja terminee)

---

### Frappe alternee (2 actionneurs)

Deux actionneurs alternent a chaque NoteOn (round-robin). Permet des roulements rapides.

**Champs :**
- Actionneur frappe A
- Actionneur frappe B

**Exemple — Caisse claire avec roulement :**
```
Type:         Frappe alternee
Nom:          Snare Roll
Note MIDI:    D1 (38)
Actionneur A: Snare Solenoid Left
Actionneur B: Snare Solenoid Right
Retrigger:    Reset
```

**Fonctionnement :**
- NoteOn n°1 → actionneur A frappe
- NoteOn n°2 → actionneur B frappe
- NoteOn n°3 → actionneur A frappe
- ... et ainsi de suite

---

### Avec mute (frappe + mute)

Un actionneur frappe et un autre etouffe le son. Le mute s'active sur NoteOff.

**Champs :**
- Actionneur de frappe
- Actionneur mute

**Exemple — Tom avec mute :**
```
Type:              Avec mute
Nom:               Tom High
Note MIDI:         D2 (50)
Actionneur frappe: Tom Solenoid
Actionneur mute:   Tom Mute Servo
Retrigger:         Reset
```

**Fonctionnement :**
- NoteOn → solenoide frappe + servo mute se releve (laisse resonner)
- NoteOff → servo mute s'abaisse (etouffe le son)

---

### Servo alterne (angle A / angle B)

Un servo bascule entre ses 2 positions a chaque NoteOn. Pas de frappe, juste un mouvement.

**Champs :**
- Servo

**Exemple — Cloche agogo :**
```
Type:  Servo alterne
Nom:   Agogo Toggle
Note:  A2 (57)
Servo: Agogo Servo (position min=30°, max=150°)
```

**Fonctionnement :**
- NoteOn n°1 → servo va a l'angle min (30°)
- NoteOn n°2 → servo va a l'angle max (150°)
- NoteOn n°3 → servo va a l'angle min (30°)
- ...

---

### Shaker (servo + solenoide alternes)

Combine un servo oscillant et un solenoide. Simule un shaker ou des maracas.

**Champs :**
- Servo oscillation
- Solenoide frappe

**Exemple — Shaker :**
```
Type:      Shaker
Nom:       Shaker
Note:      C#2 (49)
Servo:     Shaker Servo (oscille)
Solenoide: Shaker Strike (frappe a chaque position)
```

**Fonctionnement :**
- NoteOn → servo alterne angle + solenoide frappe simultanement
- Simule le mouvement "secouer" d'un shaker

---

### Brosse double servo (descente + brosse)

Deux servos : un descend le mecanisme sur la surface, l'autre fait le mouvement de brosse.

**Champs :**
- Servo descente (abaisse le systeme)
- Servo brosse (oscillation)

**Exemple — Balai de jazz sur caisse claire :**
```
Type:          Brosse double servo
Nom:           Snare Brush
Note:          D1 (38)
Servo descente: Brush Lower Servo
Servo brosse:   Brush Sweep Servo
```

**Fonctionnement :**
- NoteOn → servo descente abaisse + servo brosse commence a osciller (vitesse = velocite)
- NoteOff → servo descente releve + servo brosse stoppe

---

### Hi-Hat (frappe + pedale + mute)

Configuration complete d'un hi-hat : frappe, pedale (CC#4), et mute. Cree automatiquement 3 instruments (closed, pedal, open).

**Champs :**
- Actionneur de frappe (solenoide)
- Servo pedale (pilote par CC#4)
- Servo mute (etouffe la cymbale)
- Note fermee, Note pedale, Note ouverte

**Exemple — Hi-Hat complet :**
```
Type:           Hi-Hat
Nom:            Hi-Hat
Actionneur:     HH Strike Solenoid
Servo pedale:   HH Pedal Servo (CC#4)
Servo mute:     HH Mute Servo
Note fermee:    F#1 (42)
Note pedale:    G#1 (44)
Note ouverte:   A#1 (46)
```

**Fonctionnement :**
- 3 instruments sont crees automatiquement :
  - **Closed** (note 42) : frappe + mute active (etouffe)
  - **Pedal** (note 44) : frappe + mute active (etouffe, son de pedale)
  - **Open** (note 46) : frappe + mute relache (laisse resonner)
- CC#4 controle en continu la position du servo pedale (0 = ferme, 127 = ouvert)

---

### Moteur (vitesse/chrono/sweep/alternance)

Pour les instruments motorises. Le comportement depend du sous-type choisi.

**Champs :**
- Actionneur moteur
- Comportement (Vitesse continue / Chrono / Rotation optique / Balayage / Alternance)

**Exemple — Rouleau motorise :**
```
Type:          Moteur
Nom:           Motor Roll
Note:          C2 (48)
Actionneur:    Roll Motor
Comportement:  Vitesse continue
```

**Exemple — Balayage de bras mecanique :**
```
Type:          Moteur
Nom:           Sweep Arm
Note:          D2 (50)
Actionneur:    Arm Motor (avec fins de course)
Comportement:  Balayage (sweep)
```

**Fonctionnement selon comportement :**

| Comportement | NoteOn | NoteOff |
|-------------|--------|---------|
| Vitesse continue | Moteur tourne (vel → vitesse) | Moteur stoppe |
| Chrono (duree) | Moteur tourne pendant X ms | Auto-stop |
| Rotation optique | Moteur tourne N tours | Auto-stop au compteur |
| Balayage | Moteur avance jusqu'a fin de course | Retour (si 2 FdC) |
| Alternance | Moteur change de direction | — |

---

### Personnalise (editeur complet)

Acces a l'editeur complet avec actions NoteOn/NoteOff, bindings CC, et pipeline.

**Champs :**
- Liste de tous les actionneurs (checkbox)
- Editeur NoteOn Actions (commandes directes)
- Editeur NoteOff Actions
- Editeur CC Bindings

**Quand utiliser :**
- Quand aucun type predifini ne correspond
- Pour combiner plusieurs actionneurs avec des logiques complexes
- Pour utiliser des delais, des bindings CC avances, ou des actions multiples

---

## Actions directes (NoteOn / NoteOff)

Dans le mode Personnalise et dans l'editeur Pipeline, chaque action envoie une commande a un actionneur.

### Champs d'une action

| Champ | Description |
|-------|-------------|
| **Actionneur** | Quel actionneur cibler |
| **Commande** | PULSE (frappe), POSITION (angle), PWM (vitesse), OFF (stop) |
| **Source valeur** | VELOCITY (velocite MIDI), FIXED (valeur fixe), CC_VAR (variable CC), INVERTED |
| **Valeur fixe** | Si source = FIXED, la valeur envoyee (0-127) |
| **Delai** | Temps avant execution (ms) |
| **Duree min/max** | Pour PULSE : duree en ms mappee sur la velocite |
| **Courbe** | LINEAR, EXPO, LOG — comment la velocite est mappee |
| **Groupe alternance** | 0 = toujours, 1+ = round-robin entre groupes |

### Exemple — Action NoteOn avec delai

```
Action 1: Actionneur=Kick Solenoid, Commande=PULSE, Source=VELOCITY, Delai=0ms
Action 2: Actionneur=Kick LED, Commande=PWM, Source=VELOCITY, Delai=5ms
```

Le solenoide frappe immediatement, la LED s'allume 5ms apres.

---

## CC Bindings

Les bindings CC permettent de piloter un actionneur en continu avec un controleur MIDI (molette, pedale, potentiometre).

| Champ | Description |
|-------|-------------|
| **CC#** | Numero du controleur MIDI (0-127) |
| **Actionneur** | Quel actionneur piloter |
| **Commande** | POSITION, PWM, etc. |
| **Plage min/max** | CC 0 → plage min, CC 127 → plage max |
| **Courbe** | LINEAR, EXPO, LOG |
| **Inverse** | Inverser la direction du CC |

### Exemple — Pedale hi-hat

```
CC#:        4 (Hi-Hat pedal standard)
Actionneur: HH Pedal Servo
Commande:   POSITION
Plage:      15° - 90°
Courbe:     LINEAR
Inverse:    non
```

CC#4 = 0 → servo a 15° (ferme) / CC#4 = 127 → servo a 90° (ouvert)

### Routages speciaux

- **Pitch Bend** est route sur CC virtuel #126
- **Channel Aftertouch** est route sur CC virtuel #125

---

## Mode Retrigger

Que se passe-t-il quand une note MIDI est rejouee avant que la precedente soit terminee ?

| Mode | Comportement | Usage typique |
|------|-------------|---------------|
| **Ignore** | La 2e note est ignoree | Defaut, protection contre les doubles |
| **Reset** | Stoppe la 1ere, relance | Roulements, frappes rapides |
| **Stack** | Les 2 jouent en parallele | Avance : couches de velocite |

---

## Pipeline (traitement avance)

Chaque instrument peut avoir un pipeline optionnel qui traite la velocite avant l'envoi a l'actionneur. Voir [pipeline.md](pipeline.md) pour la documentation complete.

Le pipeline est accessible via l'onglet "Pipeline" en selectionnant un instrument.

### Quand utiliser le pipeline ?

- **Pas necessaire** pour un instrument simple (frappe directe)
- **Utile** pour : filtrer par velocite, appliquer des courbes, ajouter des delais, router vers des actionneurs differents selon la force de frappe

---

## Exemples complets

### Exemple 1 — Kit minimal (kick + snare + hi-hat)

**Actionneurs :**

| Nom | Type | Comportement | Bus | Pin |
|-----|------|-------------|-----|-----|
| Kick Sol | Solenoide | Strike | MCP23017 | 1 |
| Snare Sol | Solenoide | Strike | MCP23017 | 2 |
| HH Sol | Solenoide | Strike | MCP23017 | 3 |
| HH Pedal Servo | Servo | Hi-Hat Controller | PCA9685 | 1 |
| HH Mute Servo | Servo | Mute | PCA9685 | 2 |

**Instruments :**

| Nom | Type | Note | Actionneurs |
|-----|------|------|-------------|
| Kick | Simple | C1 (36) | Kick Sol |
| Snare | Simple | D1 (38) | Snare Sol |
| Hi-Hat | Hi-Hat | F#1/G#1/A#1 | HH Sol + HH Pedal + HH Mute |

### Exemple 2 — Percussion avancee avec moteur

**Actionneurs :**

| Nom | Type | Comportement | Bus | Pin | Details |
|-----|------|-------------|-----|-----|---------|
| Roll Motor | Moteur | Vitesse continue | ESP32 LEDC | 25 | PWM 20-100% |
| Sweep Arm | Moteur | Balayage | ESP32 LEDC | 26 | FdC1=GPIO34, FdC2=GPIO35 |
| Cymbal Sol | Solenoide | Strike | MCP23017 | 4 | 8-30ms |
| Cymbal Mute | Servo | Mute | PCA9685 | 3 | 15-90° |

**Instruments :**

| Nom | Type | Note | Actionneurs |
|-----|------|------|-------------|
| Motor Roll | Moteur (vitesse) | C2 (48) | Roll Motor |
| Sweep | Moteur (balayage) | D2 (50) | Sweep Arm |
| Crash | Avec mute | C#2 (49) | Cymbal Sol + Cymbal Mute |

### Exemple 3 — Jazz brushes (balais)

**Actionneurs :**

| Nom | Type | Comportement | Bus | Pin |
|-----|------|-------------|-----|-----|
| Brush Lower | Servo | Position | PCA9685 | 5 |
| Brush Sweep | Servo | Comb/Brush | PCA9685 | 6 |
| Snare Sol | Solenoide | Strike | MCP23017 | 2 |

**Instruments :**

| Nom | Type | Note | Actionneurs |
|-----|------|------|-------------|
| Snare Brush | Brosse double servo | D1 (38) | Brush Lower + Brush Sweep |
| Snare Rim | Simple | E1 (40) | Snare Sol |

**Fonctionnement :**
- NoteOn sur D1 : le systeme descend (Lower) + le balai brosse (Sweep). Velocite = vitesse de brossage.
- NoteOff sur D1 : le systeme remonte + le balai stoppe.
- NoteOn sur E1 : frappe de rimshot classique.
