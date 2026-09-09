# Reconnaissance automatique General-Midi-Boop (protocole v2)

Quand un Drums-Engine apparait sur une liaison MIDI, General-Midi-Boop (GMB, sur
Raspberry Pi) determine tout seul :

- quels instruments de percussion sont physiquement presents ;
- sur quels canaux MIDI ils repondent ;
- quelles notes MIDI sont reellement jouables ;
- quels CC sont routes ;
- la polyphonie et les contraintes de simultaneite ;
- les temps de preparation, de frappe et de rearticulation ;
- les comportements percussifs particuliers (hi-hat, choke, moteurs, controles
  continus) ;
- la revision courante de la configuration.

Aucune saisie manuelle cote Raspberry Pi n'est normalement necessaire.

**La configuration Drums-Engine reste la seule source de verite.** Il n'existe
pas de fichier de capacites a maintenir en parallele : tout ce que le firmware
annonce est *derive* de la configuration validee et active.

---

## 1. Reference normative

L'implementation suit ce que General-Midi-Boop **parse aujourd'hui**, pas une
version anterieure de sa documentation :

| Cote GMB | Ce que le firmware doit respecter |
|---|---|
| `docs/SYSEX_IDENTITY.md` | Le protocole v2 (blocs 0x01 / 0x10 / 0x11), la structure du descripteur |
| `src/midi/devices/DeviceManager.js` · `parseGmbHandshake()` | Handshake de **24 octets exactement**, `proto_ver = 2`, entiers 32 bits sur 5 octets dont un demi-octet haut (`0x0f`) |
| `src/midi/devices/DeviceManager.js` · `parseDescriptorChunk()` | `F0 7D 00 10 01 <total[2]> <index[2]> <payload…> F7`, au moins 10 octets, payload ASCII |
| `src/midi/devices/DeviceManager.js` · `parseChangeNotification()` | Notification de **12 octets exactement** |
| `src/midi/instrument/DescriptorProtocol.js` | Validation du descripteur : `gmb_descriptor === 2`, `instruments` non vide et ≤ 16 entrees, `channel` entier **0..15** et unique, `notes.mode ∈ {range, discrete}` |
| `src/midi/adaptation/InstrumentTypeConfig.js` | Vocabulaire `type` / `subtype` : la famille est `drums`, le sous-type de kit est `standard_kit` |

> **Piege verifie.** Le codec generique `decode7BitTo32Bit()` de GMB masque le
> 5ᵉ octet a `0x07` (plafond 31 bits). Le parseur v2, lui, utilise `0x0f`.
> S'aligner sur le codec generique perdrait le bit 31 d'un `instance_id` et
> diviserait par deux l'espace d'identifiants, sans qu'aucun test ne le signale.
> `engine/src/gmb/gmb_sysex.h` encode donc le demi-octet complet, et
> `test_gmb_sysex` le verifie bit a bit.

---

## 2. Architecture

```
Configuration Drums-Engine ACTIVE (PipelineLookup compilee + ActuatorConfig)
              │
              ▼
      gmb_capabilities        derive les capacites MUSICALES
              │
              ▼
      CapabilitySnapshot      instruments logiques, voix, notes, CC, timing
              │
       ┌──────┴───────┐
       ▼              ▼
 gmb_descriptor    gmb_sysex        JSON ASCII        codec binaire
       │              │
       ▼              ▼
  cache JSON     blocs 0x01 / 0x10 / 0x11
       │              │
       └──────┬───────┘
              ▼
      GmbSysExService     requetes, cache, revision, limiteur, compteurs
              │
       ┌──────┴───────┐
       ▼              ▼
  transports MIDI   GET /gmb/descriptor.json
```

Fichiers :

| Fichier | Role |
|---|---|
| `engine/src/gmb/gmb_sysex.h/.cpp` | Codec binaire pur : encodage 7 bits, analyse des requetes, construction des trames |
| `engine/src/gmb/gmb_identity.h/.cpp` | Identite physique stable (`instance_id`) |
| `engine/src/gmb/gmb_capabilities.h/.cpp` | Derivation des capacites depuis la configuration active |
| `engine/src/gmb/gmb_descriptor.h/.cpp` | Serialisation du descripteur en JSON ASCII |
| `engine/src/gmb/gmb_sysex_service.h/.cpp` | Etat : cache, revision, notifications, limiteur, diagnostique |
| `engine/src/web/routes_gmb.cpp` | `GET /gmb/descriptor.json` et `GET /api/gmb/status` |

Aucun calcul de capacite ne vit dans une classe d'actionneur, dans le scheduler
ou dans `main.cpp` : `main.cpp` ne fait que brancher les morceaux.

---

## 3. Bloc 0x01 — handshake

Requete (6 octets) :

```
F0 7D 00 01 00 F7
```

Reponse (**24 octets exactement**) :

```
F0 7D 00 01 01 02 <instance_id[5]> <firmware[3]> <descriptor_size[3]> <revision[5]> <flags> F7
```

| Offset | Champ | Source dans Drums-Engine |
|---|---|---|
| 5 | `proto_ver` = `02` | constante |
| 6-10 | `instance_id` | MAC eFuse repliee (§4) |
| 11-13 | `firmware` | `FIRMWARE_VERSION_MAJOR/MINOR/PATCH` (`core/config.h`) |
| 14-16 | `descriptor_size` | taille reelle du descripteur en cache, 21 bits |
| 17-21 | `revision` | `capabilitiesRevision` persistee (§7) |
| 22 | `flags` | bit 0 = HTTP disponible, bit 1 = notifications push |

`flags` bit 0 n'est arme que lorsque `GET /gmb/descriptor.json` est reellement
enregistre (`_setupGmbRoutes()`), pas « par principe » : annoncer une route
absente ferait perdre un aller-retour a l'hote.

---

## 4. Identite physique (`instance_id`)

C'est le pivot du protocole : GMB rattache une configuration enregistree, une
calibration et une latence mesuree a **un exemplaire physique**. Le contre-exemple
documente cote GMB est le firmware qui renvoie `00 00 00 00 00` pour toutes les
cartes — deux machines recoivent alors les memes reglages.

Drums-Engine derive l'identifiant de l'**adresse MAC de base gravee en eFuse**
(`esp_efuse_mac_get_default`), repliee en 32 bits par FNV-1a :

- stable apres reboot et apres reflash ;
- unique par puce ;
- independante des noms d'instruments, des profils utilisateur et de la
  configuration Wi-Fi (ce n'est pas la MAC de l'interface STA/AP, que la pile
  reseau peut se voir surcharger) ;
- jamais nulle : `0` est reserve a « pas d'identite ».

FNV-1a plutot qu'un XOR ou une troncature : les trois premiers octets d'une MAC
Espressif sont un OUI constant, donc deux cartes du meme lot ne differeraient que
sur les octets bas. `test_gmb_sysex` verifie la stabilite, l'absence de
collision entre deux cartes voisines, la diffusion sur les bits hauts, et
l'aller-retour a travers l'encodage 7 bits.

---

## 5. Bloc 0x10 — transfert segmente du descripteur

```
Requete  : F0 7D 00 10 00 <chunk_index[2]> F7
Reponse  : F0 7D 00 10 01 <total_chunks[2]> <chunk_index[2]> <payload…> F7
```

- Charge utile : **200 octets au maximum** (choix GMB, pour rester sous la MTU
  de reassemblage BLE-MIDI). Message complet : 210 octets au plus.
- Le descripteur etant restreint a l'ASCII, chaque octet est deja 7-bit safe :
  **aucun packing**, donc aucun surcout.
- Le decoupage est purement positionnel (`index × 200`), donc deterministe : un
  segment redemande renvoie exactement les memes octets.
- N'importe quel index peut etre demande, dans n'importe quel ordre.
- **Index hors bornes : aucune reponse.** Repondre un segment vide ou replie
  ferait diverger le reassemblage cote hote ; l'absence de reponse le laisse
  expirer proprement puis retomber au niveau 0. Le cas est compte
  (`outOfRangeChunks`).
- **Aucune reconstruction pendant le transfert** : les segments sortent du cache,
  qui n'est reecrit qu'a l'activation d'une configuration.

---

## 6. Bloc 0x11 — notification de changement

```
F0 7D 00 11 02 <revision[5]> <change_flags> F7        (12 octets)
```

`change_flags` : bit 0 `IDENTITY_CHANGED`, bit 1 `INSTRUMENTS_CHANGED`,
bit 2 `TIMING_CHANGED`, bit 3 `RESTART_REQUIRED`.

Emise uniquement quand les capacites ont **reellement** change. Le bit
`TIMING_CHANGED` est pose en comparant une empreinte des seuls champs de timing,
pour que l'hote sache si sa compensation de latence est concernee.

La notification est poussee depuis `MidiEngine::update()`, c'est-a-dire depuis le
contexte qui lit deja la pile MIDI : la pile AppleMIDI n'est pas reentrante et
emettre depuis Core 0 pendant que Core 1 lit serait une course.

Pas de notification au **premier** calcul apres demarrage : GMB interroge le
bloc 0x01 a la connexion et y lit la revision courante.

---

## 7. Revision de capacites

| Evenement | Effet sur la revision |
|---|---|
| Edition en brouillon (non validee) | aucun |
| Sauvegarde invalide (rejetee par le validateur) | aucun — le descripteur en cache n'est meme pas remplace |
| Configuration valide activee, capacites changees | `revision++`, cache reconstruit, bloc 0x11 emis |
| Configuration valide activee, capacites identiques | aucun — ni increment, ni notification, ni ecriture flash |
| Redemarrage a configuration inchangee | aucun |

### Detection par contenu du descripteur

Plutot que de maintenir a la main la liste des champs « qui comptent »,
`GmbSysExService::rebuild()` :

1. serialise le descripteur avec `revision = 0` — le resultat ne depend alors
   **que** des capacites ;
2. en calcule l'empreinte FNV-1a ;
3. compare a l'empreinte persistee.

Une edition d'interface qui ne change aucune capacite (recablage d'un GPIO,
position de repos, priorite d'arbitrage, renommage d'un module) produit
exactement les memes octets : pas d'increment, pas de notification, **pas de
cycle de flash**. C'est ce qui protege a la fois la duree de vie de la flash et
la tranquillite de l'hote.

`revision` et empreinte sont persistees dans `/gmb.json`, ecrites depuis Core 0,
hors du verrou temps reel. Si l'ecriture echoue, la revision **avance** au
redemarrage suivant (l'empreinte relue sera perimee) : elle ne recule jamais, ce
qui est la seule propriete dont GMB depend pour ne pas manquer une mise a jour.

---

## 8. Regroupement en instruments logiques

Drums-Engine empile quatre niveaux :

```
actionneur physique  ->  ActionStep / pipeline  ->  instrument logique  ->  note / CC MIDI
```

GMB decrit le **dernier**. La regle est donc :

> **un instrument logique GMB = un canal MIDI.**

Un kit de batterie complet — grosse caisse, caisse claire, charleston, cymbales,
chacun avec ses actionneurs et ses pipelines — est **un seul** instrument GMB sur
le canal 10, pas 47 instruments pour 47 articulations. Si la carte expose
plusieurs groupes de percussion independants sur des canaux separes, chacun
devient une entree distincte (percussion accordee sur le canal 12, par exemple).

La source du regroupement est la table `(canal, note)` **compilee**, pas la liste
d'instruments telle qu'elle a ete saisie. C'est ce qui rend le resultat
automatiquement correct sur les points ou le moteur a deja tranche :

- un instrument desactive n'a pas de pipeline, donc pas de note annoncee ;
- un instrument OMNI est deja deplie sur les 16 canaux, avec la precedence
  « canal explicite > OMNI » appliquee ;
- deux actionneurs derriere la meme note ne donnent qu'une entree dans la table.

### Canaux exposes

- Les canaux **explicitement declares** par au moins un pipeline utilisable, et
  autorises par le filtre de canaux MIDI.
- Un canal filtre n'est pas routable : ses notes ne sont pas des capacites, il
  n'est pas declare.
- Les pipelines OMNI repondent sur les 16 canaux. Les declarer seize fois
  produirait seize entrees identiques et saturerait le plafond de l'hote : leurs
  notes rejoignent les canaux deja declares. Si la configuration ne declare
  aucun canal explicite, une entree unique est posee sur le canal de percussion
  General MIDI.

### Convention de canal — le point a ne pas rater

| Vu par | Canal de percussion GM |
|---|---|
| Utilisateur, General MIDI | **10** |
| `InstrumentConfig::midiChannel`, table de routage du moteur | **10** (1-based, `0` = OMNI) |
| Descripteur GMB, `DescriptorProtocol.js` (`channel` entier 0..15) | **9** |

La conversion est un `- 1` unique, dans `gmbBuildCapabilities()`. Trois tests
dedies la verrouillent (`test_user_channel_10_is_declared_as_channel_9`,
`test_channel_one_is_declared_as_channel_zero`,
`test_omni_only_configuration_lands_on_the_gm_percussion_channel`).

---

## 9. Notes jouables

```json
"notes": { "mode": "discrete", "list": [36, 38, 42, 46, 49] }
```

Une note est annoncee si, **et seulement si**, la table compilee la route sur ce
canal vers un pipeline qui declenche au moins une action NoteOn visant un
actionneur existant et active.

- **Jamais** la plage GM 35..81 « parce que c'est un kit ». Une machine qui
  contient un kick, une caisse claire, deux charlestons et une crash annonce
  `[36, 38, 42, 46, 49]`, rien d'autre.
- **Notes en double impossibles** : la table `(canal, note)` associe au plus un
  pipeline a une note, donc un tom a double frappe (deux solenoides, une note),
  un pipeline multi-etapes, des actionneurs alternes ou un round-robin mecanique
  produisent tous une seule entree. Les actionneurs restent visibles comme
  `voices` — c'est la polyphonie qui les compte, pas la liste de notes.
- Rien ne code en dur 35..81 : General MIDI est une convention d'usage, pas une
  limite du protocole. Une percussion experimentale sur des notes arbitraires
  fonctionne a l'identique.

---

## 10. CC et controles continus

```json
"expression": { "cc": [4], "velocity": true, "channel_aftertouch": false, "poly_aftertouch": false }
```

Les CC sont lus dans la **table de routage CC compilee** (`cc_routes`), pas dans
les liaisons saisies : une liaison que le compilateur a laissee tomber (table
pleine) n'est pas une capacite. Le canal de la route est respecte (`0` = OMNI).

- Si un hi-hat est configure sur CC#4, `4` est annonce.
- Si aucune route n'utilise CC#4, il n'est **pas** annonce — meme si « une boite a
  rythmes en utiliserait normalement un ».
- Les **CC virtuels** internes du moteur (`VIRTUAL_CC_AFTERTOUCH` = 125,
  `VIRTUAL_CC_PITCH_BEND` = 126) ne sont pas des CC sur le fil : ils alimentent
  `channel_aftertouch` et `pitch_bend`, jamais `cc`.
- `poly_aftertouch` est toujours `false` : le moteur n'installe aucun
  gestionnaire d'aftertouch polyphonique.
- `pitch_bend.range_semitones` est **omis** : le moteur mappe le pitch bend sur
  une position d'actionneur (tension de peau), il ne connait pas l'intervalle
  musical que cela produit.

### Velocite

`velocity` n'est vrai que si la velocite change reellement quelque chose :

| Cas | Velocite annoncee |
|---|---|
| `PULSE` avec durees min ≠ max | oui — la duree de frappe suit la velocite |
| `PULSE` avec durees egales | **non** — la frappe est toujours identique |
| `SOLENOID_HOLD` | **non** — l'actionneur ecrit `paramMin` en PWM fixe et ignore la valeur |
| `POSITION` / `PWM` avec source velocite | oui |
| `POSITION` sur `SOLENOID_MUTE` | **non** — seuil binaire a 64 |
| Source `FIXED` ou `CC_VAR` | non |

Le detail est aussi disponible par voix (`voices[].velocity`) : une machine dont
un seul frappeur est expressif ne l'annonce pas comme une propriete uniforme.

### Controles continus

Les comportements internes ne sont pas automatiquement des capacites MIDI. Ce qui
est expose, c'est ce qui est **utilisable depuis l'exterieur** :

| Comportement | Ce que GMB en voit |
|---|---|
| `HIHAT_CONTROLLER` | le CC de pedale + les notes concernees, dans `physical.hihat` |
| `PITCH_BEND` | `expression.pitch_bend.supported` |
| `SERVO_MUTE` / `SOLENOID_MUTE` / `SERVO_POSITION` en etouffoir | `physical.choke_groups` |
| `STEPPER_POSITION` | une fenetre `timing.prepare` silencieuse |
| `MOTOR_OPTICAL_TRACK`, `COMB_BRUSH`, `MOTOR_*`, `STEPPER_STRIKE/ROTATE` | des voix sonores ordinaires |

Ce qui reste **dans** Drums-Engine : canal PCA9685, GPIO, MOSFET, bornes PWM d'un
servo, file FreeRTOS, identifiants internes. Rien de tout cela n'affecte
l'ordonnancement musical cote hote.

---

## 11. Polyphonie

Ce n'est **pas** le nombre d'actionneurs. C'est le nombre d'evenements MIDI
simultanes que la machine peut reellement executer, soit le minimum de :

1. le nombre de **mecanismes sonores** de l'instrument (un frappeur ne frappe
   qu'une note a la fois) ;
2. `PowerBudgetConfig::maxConcurrent`, le plafond en nombre applique par
   `ActuatorManager` ;
3. le nombre d'actionneurs qui tiennent sous `maxPeakMa`, calcule dans le
   meilleur des cas (les moins gourmands d'abord) et avec la meme regle que
   `powerBudgetAdmit()` : un courant non declare (`currentMa == 0`) ne consomme
   pas de budget et n'est jamais refuse.

Le plafond global du budget electrique, qui est transverse a tous les canaux, est
expose separement comme contrainte :

```json
"polyphony": {
  "max": 3,
  "constraints": [
    { "type": "one_note_per_voice" },
    { "type": "max_simultaneous_per_group", "group": "power_supply", "max": 8 }
  ]
}
```

Si aucun scalaire fiable ne peut etre calcule (aucun mecanisme sonore), le champ
`max` est **omis** plutot qu'invente.

---

## 12. Modele temporel

```json
"timing": {
  "prepare": { "base_ms": 120, "max_ms": 120, "silent": true },
  "excite":  { "latency_ms": 0, "jitter_ms": 1 },
  "rearticulation_ms": 40,
  "release_ms": 50
}
```

Derive **par instrument logique**, depuis les `ActionStep` des pipelines de ses
notes :

| Champ | Derivation |
|---|---|
| `prepare` | Fenetre entre la premiere action de POSITIONNEMENT (etouffoir qui se retire, servo qui se met en place, stepper qui rejoint sa zone) et la premiere action de FRAPPE. `silent: true` : le geste est inaudible, GMB peut l'anticiper avec son lookahead et il n'entre pas dans la compensation. Absent pour une frappe directe. |
| `excite.latency_ms` | Delai restant entre la fin de la preparation et la commande de frappe audible. Zero pour une frappe directe : le scheduler l'emet sans delai supplementaire. |
| `excite.jitter_ms` | Granularite d'un tick du scheduler (`SCHEDULER_TICK_HZ`). C'est la seule incertitude que le firmware connaisse de lui-meme. |
| `rearticulation_ms` | Intervalle minimal entre deux frappes : le `cooldownUs` declare, jamais moins que la duree d'impulsion la plus longue (une bobine ne se redeclenche pas tant qu'elle est alimentee). Aussi disponible par voix. |
| `release_ms` | Plus grand delai des actions NoteOff. Absent si l'instrument n'a aucune action NoteOff. |

`base_ms == max_ms` : contrairement a un instrument frette, la duree ne depend
pas d'un intervalle de notes — elle est fixee par le pipeline.

**Ce qui n'est pas derivable reste inconnu.** `min_note_ms` est volontairement
absent : une frappe se coupe toute seule, la duree de la note MIDI ne change rien
au son. Le transit mecanique du frappeur lui-meme (le temps que le maillet mette
a atteindre la peau) n'est pas connu du firmware : il se mesure au micro cote
GMB et alimente `sync_delay`, qui n'est jamais transmis (`SYSEX_IDENTITY.md` §9).

Les instruments a mecanismes tres differents ne sont pas ecrases sur une valeur
unique : chaque canal porte son propre bloc `timing`. Un solenoide rapide sur le
canal 10 et une timbale a pre-positionnement stepper sur le canal 12 declarent
deux modeles distincts.

---

## 13. Hi-hat, choke et relations

Le socle du descripteur reste generique — instrument, canal, notes, polyphonie,
CC, timing. Ce que ces champs ne savent pas dire va dans `physical`, un espace de
noms libre que GMB ignore s'il ne le connait pas : une extension inconnue ne peut
donc pas invalider le descripteur de base.

```json
"physical": {
  "family": "percussion",
  "hihat": { "pedal_cc": 4, "notes": { "mode": "discrete", "list": [42, 46] } },
  "choke_groups": [ { "id": "a3", "notes": { "mode": "discrete", "list": [49, 51] } } ]
}
```

- `hihat` relie le CC continu de pedale aux articulations qu'il gouverne
  (ferme / ouvert / pedale).
- `choke_groups` : un etouffoir partage par plusieurs notes **est** une relation
  de choke — ces notes s'interrompent mutuellement, puisqu'un seul mecanisme les
  etouffe. Le groupe se lit dans la configuration, il n'y a rien a saisir.
- Les identifiants (`a3`) sont ceux des `voices`, derives de l'identifiant
  d'actionneur : stables d'un descripteur au suivant tant que le cablage ne
  bouge pas, ce dont depend le diff des surcharges cote GMB.

---

## 14. `configured` — capacite et sante ne sont pas la meme chose

`"configured": true` ne signifie pas « une entree existe en base ». Il signifie
« cet instrument est utilisable tel quel » : au moins une note jouable, avec un
mapping MIDI valide, un pipeline valide et un actionneur disponible et active.

Un instrument sans note jouable est declare `"configured": false`, ce qui renvoie
GMB a la saisie manuelle **sans ecraser** ce que l'utilisateur y avait deja mis.

Un arret d'urgence ou un defaut passager ne reecrit **pas** les capacites : la
capacite (ce que la machine sait faire) et la sante d'execution (ce qu'elle peut
faire a cet instant) restent conceptuellement separees. La sante se lit sur
`/api/status`, pas dans le descripteur.

### Aucun instrument utilisable

Le validateur de l'hote refuse un tableau `instruments` vide. Le firmware declare
donc un instrument explicitement non configure sur le canal de percussion :

```json
{"gmb_descriptor":2,"revision":1,"device":{…},"instruments":[{"channel":9,"configured":false}]}
```

C'est exactement le cas prevu par `SYSEX_IDENTITY.md` §5.1.

---

## 15. Endpoint HTTP

```
GET /gmb/descriptor.json
```

Sert **les memes octets** que le bloc 0x10 — les deux transports lisent le meme
cache. Il ne peut donc pas exister une « version SysEx » et une « version HTTP »
du descripteur qui divergeraient ; la seule difference est le decoupage en
segments.

- Non authentifie, comme les autres `GET` de l'API : un descripteur de capacites
  n'est pas un secret, et GMB le recupere sans jeton.
- En-tete `ETag` porte la revision, pour qu'un hote qui a deja le descripteur ne
  le retelecharge pas.
- `503` tant que le descripteur n'a pas ete construit.
- Cette route est ce qui arme le bit HTTP du handshake.

---

## 16. Transports

| Transport | Reconnaissance auto | Etat dans ce firmware |
|---|---|---|
| **WiFi rtpMIDI (AppleMIDI)** | oui | **implemente** — seul transport du moteur, bidirectionnel |
| USB-MIDI | oui | absent du firmware |
| BLE-MIDI | oui | absent du firmware |
| DIN IN + OUT | oui | absent du firmware |
| DIN IN seul | **non** | pas de voie de retour, aucune decouverte possible |

Le service est transport-agnostique : `handleSysEx()` prend un message complet et
rend les octets a emettre. Brancher un second transport consiste a appeler ce
meme service. Rien n'annonce un transport qui ne saurait pas repondre — cote GMB,
`SYSEX_IDENTITY.md` §8 recommande d'ailleurs de preferer le drapeau HTTP en
rtpMIDI, ce que ce firmware permet.

Le trafic NoteOn / NoteOff / CC n'est pas affecte : il ne passe meme pas par le
service GMB.

---

## 17. Securite temps reel

Le scheduler a 1 kHz et la precision des frappes ne doivent rien perdre a la
decouverte. Les regles tenues :

- **Aucune generation de JSON sur un chemin temps reel.** Le descripteur est
  construit dans `rebuild()`, uniquement a l'activation d'une configuration.
- **Aucun acces au systeme de fichiers depuis le temps reel.** L'ecriture de
  `/gmb.json` se fait depuis Core 0 (`appCoreTask`), hors du verrou.
- **Aucun travail HTTP depuis le contexte du scheduler.**
- **Aucune allocation par evenement MIDI.** Le cache est un tampon statique, les
  reponses sont construites dans un tampon de pile borne (210 octets).
- La reponse a une requete SysEx se limite a analyser 6 a 8 octets et a recopier
  un segment deja calcule.
- La reconstruction du cache se fait sous le **meme `ReconfigLock`** que la table
  de pipelines : une requete SysEx arrivant sur le coeur temps reel ne peut pas
  lire un descripteur a moitie reecrit.
- Un SysEx malforme est ignore et compte, jamais interprete.
- La decouverte GMB ne declenche aucun actionneur, ne touche aucune note active
  et ne remet pas le scheduler a zero. `test_gmb_service` le verifie en faisant
  passer un deluge de trafic GMB devant un `EventProcessor` a l'ecoute : zero
  commande planifiee, notes actives inchangees, table de routage identique octet
  pour octet.

### Limiteur de debit

Seau a jetons sur le plan de controle : rafale de `RATE_BURST` (40) requetes,
rechargement a `RATE_PER_SEC` (20) jetons par seconde. Il protege contre un flot
de blocs 0x01, un flot de blocs 0x10, un flot de trames invalides et des
demandes repetees d'index hors bornes.

Un transfert complet de descripteur (handshake + une dizaine de segments) tient
dans la rafale — c'est teste, pour que la protection ne genante pas le cas
legitime. Le limiteur ne voit que du SysEx GMB : il n'intervient jamais sur le
traitement des notes.

---

## 18. Diagnostique

`GET /api/gmb/status` expose l'etat complet, et l'onglet **Traitement MIDI** de
l'interface web l'affiche :

- identifiant d'exemplaire, version de firmware, version de protocole ;
- revision, taille du descripteur, nombre de segments, niveau de detail ;
- nombre d'instruments logiques, avec pour chacun : canal (0-based et 1-based),
  notes, CC, polyphonie, velocite, timing, voix et roles ;
- disponibilite du descripteur HTTP, notifications push, transports ;
- compteurs : requetes d'identite, segments demandes, lectures HTTP, SysEx
  invalides, requetes limitees, index hors bornes, notifications emises,
  recalculs ;
- horodatages : derniere requete GMB, dernier transfert, derniere notification,
  dernier recalcul (avec `now`, pour que l'interface calcule les ages).

Un bouton **Voir le descripteur** ouvre `/gmb/descriptor.json`.

Rien n'est a saisir dans cette section : tout y est derive de la configuration.
En fonctionnement normal, le firmware ne journalise sur Serial que les
changements de revision et les anomalies (canal abandonne, pool de mecanismes
plein, echec de persistance) — pas le trafic.

---

## 19. Limites connues

- **Un seul transport.** Seul rtpMIDI est implemente dans ce firmware ; USB, BLE
  et DIN ne sont pas cables, donc pas annonces.
- **Instruments OMNI.** Un pipeline OMNI est routable sur les 16 canaux mais
  n'est declare que sur les canaux explicitement configures (ou, a defaut, sur le
  canal de percussion GM). Declarer seize entrees identiques serait exact mais
  inutilisable, et saturerait le plafond de l'hote.
- **Pas de nom par instrument logique.** Un instrument logique agrege plusieurs
  instruments Drums-Engine ; la table compilee ne porte pas leurs noms, et GMB
  n'utilise pas `instruments[].name`. Le nom d'appareil (`device.name`) et le
  canal suffisent a l'affichage.
- **`gm_program` absent.** Un kit de percussion sur le canal 10 n'a pas de
  programme GM significatif ; le champ est omis plutot qu'invente.
- **`pitch_bend.range_semitones`, `min_note_ms`, transit mecanique du frappeur :
  non derivables**, donc absents. Champ absent = inconnu, et l'utilisateur garde
  la main dessus cote GMB.
- **Taille du descripteur.** `GMB_DESCRIPTOR_MAX` (2560 octets) borne le cache.
  Une configuration qui ne tiendrait pas degrade les sections optionnelles
  (`physical`, puis `voices`, puis le detail) avant de renoncer a un canal, et
  toute perte de canal est signalee (`instrumentsDropped`, `ErrorLog`). Le
  serialiseur ne peut structurellement pas emettre de JSON tronque.
- **`GMB_MAX_MECHANISMS`** (32) borne le nombre d'actionneurs decrits par
  instantane ; un depassement est compte (`mechanismsDropped`) et signale.

## 20. Incompatibilites relevees cote General-Midi-Boop

Aucune, sur le chemin implemente. Deux ecarts internes a GMB, verifies mais sans
consequence pour ce firmware :

1. `decode7BitTo32Bit()` (codec generique) masque le 5ᵉ octet a `0x07` alors que
   `parseGmbHandshake()` utilise `0x0f`. Le firmware suit le parseur v2, qui est
   celui qui lit ses trames.
2. `DescriptorService` ecrit `capabilities_source = 'auto'` et non `'descriptor'`,
   parce que la contrainte `CHECK` de `instruments_latency` n'est pas encore
   elargie cote GMB. C'est un detail de persistance de l'hote ; il ne change rien
   aux octets echanges.

Etat cote GMB au moment de l'ecriture : le parseur du handshake v2, le transfert
0x10, la notification 0x11, le validateur de descripteur et le diff des
surcharges existent (`DeviceManager.js`, `DescriptorProtocol.js`,
`DescriptorService.js`).

---

## 21. Depannage

| Symptome | Piste |
|---|---|
| GMB ne reconnait pas l'appareil | La liaison a-t-elle une voie de retour ? Une entree DIN seule ne peut pas repondre. En rtpMIDI, verifier que la session est etablie (`GET /api/status`). |
| `descriptor_size == 0` cote GMB | Le descripteur n'a pas ete construit : verifier `GET /api/gmb/status` (`rebuilds`, `lastRebuildMs`). |
| GMB retombe en saisie manuelle | Descripteur invalide ou tronque : recuperer `/gmb/descriptor.json` et le passer dans un validateur JSON. `detail` different de `full` indique une degradation par manque de place. |
| Notes manquantes dans GMB | Elles ne sont pas routables : instrument desactive, actionneur desactive, canal filtre (`GET /api/midi/channels`), ou note masquee par un doublon `(canal, note)`. `GET /api/gmb/status` liste exactement ce qui est annonce. |
| CC absent | Aucune route CC compilee ne le vise sur ce canal. Verifier `GET /api/cc-routes` — une table de routes pleine laisse tomber les liaisons excedentaires. |
| Le mauvais canal est configure cote GMB | Verifier le decalage 0-based/1-based : `userChannel` 10 doit correspondre a `channel` 9 dans `GET /api/gmb/status`. |
| Revision qui n'augmente jamais | La modification ne change aucune capacite (c'est voulu). Comparer les descripteurs avant/apres. |
| Revision qui augmente a chaque boot | L'ecriture de `/gmb.json` echoue : verifier l'espace LittleFS (`GET /api/status`) et les logs. |
| `rateLimited` qui grimpe | Un hote interroge trop souvent. La rafale couvre un transfert complet ; au-dela, c'est un flot anormal. |
| `invalidSysEx` qui grimpe | Des trames `F0 7D 00 …` malformees arrivent. Un SysEx d'un autre fabricant n'est **pas** compte ici. |
| Deux cartes recoivent la meme configuration GMB | Leurs `instanceId` sont identiques : impossible avec la MAC eFuse, sauf materiel cloné. Comparer `GET /api/gmb/status`. |

---

## 22. Tests

| Suite | Couverture |
|---|---|
| `engine/test/test_gmb_sysex` | Encodage 7 bits (dont le bit 31), handshake 24 octets et placement de chaque champ, segments 0x10, notification 12 octets, robustesse de l'analyse (SysEx etranger ignore, echo non retraite, trames malformees comptees), identite : non nulle, stable, sans collision de lot, diffusee, aller-retour 7 bits |
| `engine/test/test_gmb_capabilities` | Kit GM, canal 10 → 9, canal 1 → 0, instrument/actionneur desactive, pipeline muet, aucune configuration, note en double (deux actionneurs, deux pipelines), plusieurs canaux, meme note sur deux canaux, OMNI, canal filtre, CC reellement routes, fuite entre canaux, CC OMNI, CC virtuels, velocite (durees egales, `FIXED`, `SOLENOID_HOLD`), polyphonie (mecanismes sonores, plafond en nombre, courant de crete, courant non declare), timing (frappe directe, fenetre de preparation, latence pure, rearticulation, relachement, gigue), roles, choke partage, frappeur partage, determinisme |
| `engine/test/test_gmb_service` | Descripteur (version, notes, vocabulaire, ASCII, equilibrage, cas sans configuration, taille = champ du handshake), transfert 0x10 (un segment, plusieurs segments, premier/milieu/dernier, ordre quelconque, repetition, hors bornes, malforme, aucune reconstruction pendant le transfert), revision et 0x11 (premier build, changement de note, changement de timing, changement sans effet, redemarrage inchange, redemarrage apres edition hors ligne, desactivation), limiteur (flot coupe, transfert complet epargne), non-regression temps reel (rien planifie, moteur intact, notes toujours traitees) |

```bash
cd engine && pio test -e native
```
