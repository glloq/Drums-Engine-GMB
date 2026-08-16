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
| `MAX_PIPELINES` | 32 | 64 | Un pipeline par instrument — jamais plus, voir ci-dessous |
| `MAX_ACTUATORS` | 32 | 64 | Plusieurs actionneurs par articulation (frappe + etouffoir) |
| `MAX_CC_ROUTES` | 64 | 96 | Suit le nombre d'instruments |

### `MAX_PIPELINES` est egal a `MAX_INSTRUMENTS`, et ce n'est pas un compromis

Les deux chemins de compilation (`main.cpp compilePipelines()` et
`PipelineCompiler::compileAll()`) emettent **au plus un pipeline par instrument
active**. Un slot de pipeline au-dela de `MAX_INSTRUMENTS` ne peut donc jamais
etre rempli : c'est 198 octets de `.bss` mort par slot. Une premiere version de
ce lot avait mis `MAX_PIPELINES` a 128 et depassait la DRAM au link de 7 ko —
soit a peu de chose pres les 64 slots inatteignables. Deux `static_assert` dans
`core/types.h` maintiennent desormais l'egalite dans les deux sens.

## Cout mesure

Tailles relevees sur hote 64 bits (`g++`, pointeurs 8 octets). Sur ESP32 les
membres contenant des pointeurs sont plus petits ; les postes dominants (tables
de notes, pipelines, configurations) n'en contiennent pas et ne bougent donc pas.
Le chiffre qui fait foi reste la sortie de `pio run -e esp32 --target size` en CI.

| Objet global | Avant | Apres | Delta |
|---|---:|---:|---:|
| `EventProcessor` (dont `PipelineLookup`) | 7 728 | 16 016 | +8 288 |
| `InstrumentManager` | 3 480 | 15 384 | +11 904 |
| `ActuatorFactory` | 10 816 | 11 840 | +1 024 |
| `ActuatorManager` | 304 | 688 | +384 |
| `GlobalState` | 416 | 576 | +160 |
| **Total** | **22 744** | **44 504** | **+21 760** |

Soit environ **+21 ko de `.bss`**.

### La marge disponible etait de ~31 ko, pas de "320 ko"

L'ESP32 annonce 320 ko de DRAM, mais l'essentiel est deja pris par la pile WiFi,
FreeRTOS et le serveur web asynchrone. Ce qui compte est la marge restante dans
`dram0_0_seg`, et ce qui en reste apres `.bss` devient le tas.

Une premiere version de ce lot (`MAX_PIPELINES` 128, table d'anti-flood CC
indexee par `(canal, CC)`) pesait +38 ko et le link a echoue avec
`region dram0_0_seg overflowed by 7008 bytes`. La marge reelle etait donc
d'environ **31 ko**.

Valeurs mesurees en CI sur la version retenue :

```
   text     data     bss      dec      hex
1174803   454600   90209  1719612   1a3d3c   firmware.elf
```

Les deux mesures se recoupent : la version en echec portait un `.bss` d'environ
107,7 ko et debordait de 7,0 ko, ce qui place le plafond aux alentours de
**100,7 ko**. A 90,2 ko, il reste donc de l'ordre de **10 ko** — a partager avec
le tas, puisque ce qui n'est pas consomme par `.bss` revient a l'allocateur.

**N'estimez pas cette marge : mesurez-la.** `pio run -e esp32 --target size`,
execute par la CI, est la seule reponse qui fasse foi — et l'echec est un echec
de LINK, pas de compilation : aucune verification cote hote ne le verra venir.

### D'ou vient la depense

- **Instruments** : `64 x 208` = ~13 ko, le poste principal.
- **Pipelines compiles** : `64 x 198` = ~12,4 ko. Un `CompiledPipeline` embarque
  ses blocs, ses actions NoteOn/NoteOff et ses liaisons CC — dont une copie de ce
  que `InstrumentConfig` contient deja. Cette duplication (~9 ko) est le
  candidat evident si une future hausse manque de place.
- **Tables (canal, note)** : 2 ko chacune (`16 x 128`), une pour les pipelines et
  une pour les instruments. C'est le prix du correctif de routage : le canal MIDI
  est desormais une vraie cle et non une verification apres coup. Voir
  `core/midi_routing.h`.
- **Anti-flood CC** : 256 octets, et non les 4 ko qu'aurait coute une table
  `(canal, CC)`. `_processCcEvent()` ne pose l'horodatage qu'apres avoir constate
  qu'une route ecoute sur le canal de l'evenement, ce qui supprime la famine
  inter-canaux sans table a deux dimensions.

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

1. `MAX_INSTRUMENTS` coute deux fois : 208 octets d'`InstrumentConfig` **et**
   198 octets de `CompiledPipeline`, soit ~406 octets par unite. C'est le poste
   qui decide.
2. Le gisement le plus evident, si la place manque, est la duplication entre
   `InstrumentConfig` et `CompiledPipeline` : les actions NoteOn/NoteOff et les
   liaisons CC sont stockees deux fois (~9 ko a 64 instruments).
3. Les gardes sont dans `core/types.h` (`static_assert`) : les index de pipeline
   sont stockes en `uint8_t` avec `0xFF` comme marqueur « aucun », et les index
   d'instrument en `int8_t`. Depasser 127 instruments demande d'elargir ces
   types, pas seulement la constante.
4. Mesurez avec `pio run -e esp32 --target size` avant de conclure : le link
   echoue net quand `dram0_0_seg` deborde, et le tas doit rester confortable
   pour le serveur web asynchrone et la pile WiFi.
