# Budget memoire statique

Le moteur n'alloue rien dynamiquement en fonctionnement : toutes les tables sont
dimensionnees a la compilation depuis `engine/src/core/config.h`. Ce document
donne le cout de ces tables, pour que la prochaine personne qui veut relever une
limite sache ce qu'elle depense.

## Pourquoi les limites ont ete relevees

Un mapping General MIDI de percussion couvre les notes 35 a 81, soit 47
articulations ; la table `GM_DRUM_NOTES` de `core/types.h` en liste deja 55. Avec
`MAX_INSTRUMENTS 16` et `MAX_PIPELINES 32`, un kit GM complet ne pouvait
simplement pas etre represente : il fallait choisir quelles articulations
abandonner.

| Constante | Avant | Apres | Raison |
|---|---:|---:|---|
| `MAX_INSTRUMENTS` | 16 | 64 | Kit GM complet + un second canal de percussions accordees |
| `MAX_PIPELINES` | 32 | 128 | Un pipeline par articulation, marge pour plusieurs canaux |
| `MAX_ACTUATORS` | 32 | 64 | Plusieurs actionneurs par articulation (frappe + etouffoir) |
| `MAX_CC_ROUTES` | 64 | 128 | Suit le nombre d'instruments |

## Cout mesure

Tailles relevees sur hote 64 bits (`g++`, pointeurs 8 octets). Sur ESP32 les
membres contenant des pointeurs sont plus petits ; les postes dominants (tables
de notes, pipelines, configurations) n'en contiennent pas et ne bougent donc pas.
Le chiffre qui fait foi reste la sortie de `pio run -e esp32 --target size` en CI.

| Objet global | Avant | Apres | Delta |
|---|---:|---:|---:|
| `EventProcessor` (dont `PipelineLookup`) | 7 728 | 32 816 | +25 088 |
| `InstrumentManager` | 3 480 | 15 384 | +11 904 |
| `ActuatorFactory` | 10 816 | 11 840 | +1 024 |
| `ActuatorManager` | 304 | 1 136 | +832 |
| `GlobalState` | 416 | 896 | +480 |
| **Total** | **22 744** | **62 072** | **+39 328** |

Soit environ **+38 ko de `.bss`** sur une piece qui dispose de ~320 ko de DRAM.

### D'ou vient la depense

- **Tables (canal, note)** : 2 ko chacune (`16 x 128`), une pour les pipelines et
  une pour les instruments. C'est le prix du correctif de routage : le canal MIDI
  est desormais une vraie cle et non une verification apres coup. Voir
  `core/midi_routing.h`.
- **Pipelines compiles** : `128 x 198` = ~25 ko, le poste principal. Un
  `CompiledPipeline` embarque ses blocs, ses actions NoteOn/NoteOff et ses
  liaisons CC.
- **Instruments** : `64 x 208` = ~13 ko.

### Ce qui a paye une partie de la note

L'`ActuatorFactory` gagne 64 emplacements et un cinquieme type d'actionneur pour
seulement +1 ko, parce que son pool a change de forme. Elle detenait
`MAX_ACTUATORS` exemplaires de **chaque** classe d'actionneur cote a cote :

```
avant :  32 x (Solenoid + Servo + Motor)                 ~= 10,2 ko d'objets
naif  :  64 x (Solenoid + Servo + Motor + Stepper)       ~= 27,6 ko d'objets
apres :  64 x un emplacement dimensionne sur le plus gros ~=  7,7 ko d'objets
```

Le pool est desormais type-efface : `MAX_ACTUATORS` emplacements d'octets
alignes, remplis par placement `new` (`ActuatorSlot` dans
`actuator/actuator_factory.h`). Deux `static_assert` garantissent qu'un nouveau
type d'actionneur ne peut pas depasser silencieusement l'emplacement, et
`destroy()` appelle le destructeur virtuel a la reconstruction — ce qui rend au
systeme les ressources detenues par l'objet (l'emplacement d'ISR optique d'un
`MotorActuator`, par exemple), ce que l'ancienne re-affectation ne faisait pas.

## Si vous devez relever encore les limites

1. `MAX_PIPELINES` est le poste le plus cher : ~198 octets par unite.
2. Les gardes sont dans `core/types.h` (`static_assert`) : les index de pipeline
   sont stockes en `uint8_t` avec `0xFF` comme marqueur « aucun », et les index
   d'instrument en `int8_t`. Depasser 127 instruments ou 255 pipelines demande
   d'elargir ces types, pas seulement la constante.
3. Verifiez la marge reelle avec `pio run -e esp32 --target size` : le tas doit
   rester confortable pour le serveur web asynchrone et la pile WiFi.
