# Page "Cablage" — branchements electriques generes

L'onglet **Cablage** de l'interface web produit un dossier de cablage complet a
partir de la configuration reelle du controleur : rien n'y est saisi a la main,
tout est deduit des actionneurs, des fins de course et des strips LED declares.

## Ce que la page genere

| Section | Contenu |
|---|---|
| Vue d'ensemble | Tuiles de synthese (actionneurs, modules, GPIO, courant simultane, pire cas, etat des verifications) puis **schema synoptique SVG** : ESP32 et ses broches, blocs d'alimentation, modules I2C avec leurs 16 voies, sorties directes, entrees, strips LED, et les rails de cablage (SDA, SCL, 3.3 V, V+, GND) |
| Verifications avant cablage | Liste d'erreurs / avertissements / informations, chacune avec la correction a appliquer |
| Synthese | Compte des solenoides / servos / moteurs / modules |
| Affectation des sorties | Une fiche par voie physique : voie, actionneur, type et geste, **instrument** pilote (avec sa note MIDI) et **action MIDI** declenchee, plus ce qu'il faut brancher sur la voie ; les voies libres sont listees en fin de module |
| Nomenclature (BOM) | Liste des composants a acheter avec quantites : MOSFET, diodes de roue libre, resistances de gate et de pull-down, condensateurs de reservoir et de decouplage, fusibles, organe d'arret d'urgence, adaptateur de niveau LED, pull-ups I2C |
| Composants de protection | Un tableau par composant : valeur calculee, **ou le placer**, ce qu'il fait, et **ce qui casse s'il manque** (diode de roue libre, reservoir, decouplage, antiparasite, anti-rebond, pull-up, pull-down, resistance de gate, fusible, arret d'urgence, adaptateur de niveau, barre de masse) |
| Affectation des GPIO ESP32 | Table broche par broche : fonction, sens, cablage attendu — y compris les broches reservees par le firmware (I2C, LED de statut, micro I2S) |
| Modules I2C | Un tableau par MCP23017 / PCA9685 reellement utilise : position des cavaliers d'adresse, canaux occupes, alimentation logique et de puissance |
| Alimentations | Un rail par famille d'actionneurs : courant simultane, pire cas, alimentation conseillee, calibre de fusible, condensateur de reservoir, section de fil |
| Schemas de principe | Schemas **dessines** par type d'etage, avec les symboles reels (fusible, arret d'urgence, condensateur, diode, MOSFET, bobine, masse) et les valeurs calculees : chaine d'alimentation, solenoide sur MOSFET / ULN2803A / module / relais, servo sur PCA9685, moteur simple, moteur sur pont en H, fin de course, capteur optique, strip LED, pull-up I2C. L'export Markdown contient les memes schemas en ASCII |
| Cablage detaille | Pour chaque actionneur, l'instrument et l'action associes, puis la marche a suivre fil par fil, avec ses parametres de configuration (angles, durees d'impulsion, plage de PWM, temps mort) |
| Mise en service | Checklist en huit etapes, de la logique seule au rail complet, cochable et conservee dans le navigateur (`localStorage`, cle `wiringChecklist`) |
| Rappels de securite | Ordre de mise sous tension, verifications thermiques, limites du firmware |

Trois exports sont disponibles :

- **Imprimer** — feuille imprimable (l'en-tete, les onglets et les controles
  sont masques a l'impression).
- **Exporter .md** — telecharge `drums-engine-cablage.md`, le meme dossier au
  format Markdown, a joindre au projet ou a imprimer plus tard.
- **Exporter .svg** — telecharge `drums-engine-schema.svg`, le schema
  synoptique seul. Les couleurs y sont ecrites en dur : le fichier s'ouvre tel
  quel dans un navigateur ou un editeur vectoriel.

## Le schema synoptique

Le schema est redessine a chaque affichage a partir de la configuration ; il
n'y a rien a positionner a la main.

- Chaque module I2C est dessine avec ses **16 voies numerotees**, coloriees par
  type d'actionneur (servo, solenoide, moteur, capteur optique), grisees quand
  l'actionneur est desactive, rouges en cas de conflit, en pointilles quand la
  voie est libre.
- **Survoler une voie** affiche l'actionneur, son type et son geste,
  l'instrument pilote et l'action MIDI declenchee. **Cliquer** ouvre la fiche de
  cablage detaillee de cet actionneur plus bas dans la page.
- Les traits horizontaux sous chaque bloc sont les **rails** a tirer : un fil
  par rail, partage entre tous les modules du bloc (SDA, SCL, 3.3 V, V+, GND).
  Les couloirs verticaux entre l'ESP32 et les blocs ne se croisent jamais : un
  couloir par signal.
- Les **organes de protection** sont dessines sur le rail qui les porte :
  fusible (avec son calibre), arret d'urgence, condensateur de reservoir vers
  la masse, et resistances de pull-up du bus I2C.

## Instrument et action portes par chaque sortie

Le firmware repartit l'information sur trois objets : l'actionneur connait sa
broche, l'instrument connait ses actionneurs, et l'action MIDI (`noteOnActions`,
`noteOffActions`, `ccBindings`) connait la commande envoyee. La page recolle les
trois une seule fois (`wBuildRoles()`), puis reutilise le resultat dans le
schema, les fiches de sorties et les tableaux des modules I2C. Une sortie sans
instrument, ou liee a un instrument mais sans action MIDI, est signalee dans les
verifications : elle serait cablee sans jamais etre declenchee.

## Parametres electriques

Le firmware connait les broches et les types d'actionneurs, mais pas les
caracteristiques electriques du materiel reel. Les tensions et courants sont
donc saisis dans la carte **Parametres electriques** en haut de la page, et
conserves dans le `localStorage` du navigateur (cle `wiringParams`) :

| Parametre | Defaut | Usage |
|---|---|---|
| Rail solenoides (V) / courant crete (A) | 12 V / 1.5 A | Dimensionnement du rail solenoides, choix du MOSFET et de la diode |
| Rail servos (V) / courant crete + repos (A) | 6 V / 1.0 A + 0.1 A | Rail V+ du PCA9685 (les servos consomment aussi au repos) |
| Rail moteurs (V) / courant crete (A) | 12 V / 1.0 A | Rail moteurs, choix du MOSFET ou du pont en H |
| Rail LED (V) / courant par LED (mA) | 5 V / 60 mA | Consommation des strips, limitee par `maxBrightness` de chaque strip |
| Marge de securite (%) | 30 % | Marge ajoutee au courant simultane pour la colonne "alim conseillee" |
| Actionneurs simultanes | `MAX_CONCURRENT_ACTIVE` | Nombre d'actionneurs actifs en meme temps retenu pour le dimensionnement |
| Etage de puissance solenoides | MOSFET | MOSFET discret, ULN2803A, module pre-cable ou relais — change la BOM, les schemas et les etapes de cablage |
| Arret d'urgence | Interrupteur coup-de-poing | Aucun, interrupteur coup-de-poing sur le rail, ou contacteur sur boucle ARU — apparait sur chaque rail du schema, dans la BOM et dans la checklist |

## Regles de calcul

- **Courant simultane d'un rail** = courant de repos de toutes les charges +
  courant crete des `N` charges les plus consommatrices, `N` etant la limite
  d'actionneurs simultanes. L'hypothese est conservatrice : chaque rail est
  dimensionne comme s'il portait a lui seul les `N` actionneurs actifs.
- **Fusible** : premiere valeur de la serie standard superieure ou egale a
  1.5 x le courant simultane.
- **Condensateur de reservoir** : environ 1000 uF par ampere pulse, arrondi a la
  serie standard, avec une tension de service d'au moins 1.5 x celle du rail.
- **Diode de roue libre** et **MOSFET** : selectionnes par palier de courant
  (UF4007 / UF5408 / SB560 / MBR20100 ; IRLZ44N / IRLB3034).
- **Section de fil** : palier de courant, de 0.35 mm2 (AWG 22) a 4 mm2 (AWG 11).

Ce sont des regles du pouce destinees a degrossir le dimensionnement : les
valeurs doivent etre recoupees avec les datasheets des actionneurs reellement
utilises.

## Detection d'erreurs

La section **Verifications avant cablage** signale, chacune avec sa correction :

| Niveau | Controle |
|---|---|
| Erreur | Deux fonctions sur le meme GPIO |
| Erreur | Deux actionneurs sur la meme voie d'un MCP23017 / PCA9685 |
| Erreur | Adresse I2C hors de la plage du composant (0x20-0x27 / 0x40-0x7F) |
| Erreur | Plus de modules d'une famille que `MCP_MAX_MODULES` / `PCA_MAX_MODULES` |
| Erreur | Broches de la flash SPI (GPIO 6 a 11) : la carte ne demarrerait pas |
| Erreur | GPIO 34-39 utilises en sortie (entrees seules) |
| Erreur | Instrument pointant vers un actionneur inexistant |
| Erreur | ULN2803A choisi avec un courant declare superieur a 500 mA par voie |
| Attention | Entree sur GPIO 34-39 : pas de pull-up interne, resistance externe obligatoire |
| Attention | Broches de strapping (GPIO 0, 2, 5, 12, 15) : leur niveau au demarrage change le mode de boot |
| Attention | Actionneur cable mais relie a aucun instrument (jamais declenche) |
| Attention | Actionneur relie a un instrument mais sans action MIDI |
| Attention | Aucun arret d'urgence materiel declare alors que la machine porte des actionneurs de puissance |
| Attention | Etage a relais choisi pour des solenoides configures en frappe (collage lent, contacts qui s'usent) |
| Info | Actionneurs desactives, rappel des pull-ups I2C |

## Source des donnees

| Donnee | Origine |
|---|---|
| Actionneurs, bus, adresses, broches, fins de course | `GET /api/actuators` |
| Instruments, notes MIDI, actions note on / note off / CC | `GET /api/instruments` |
| Strips LED (GPIO data, nombre de LEDs, luminosite max) | `GET /api/led/strips` |
| Broches systeme (I2C, LED de statut, bouton BOOT, micro I2S), adresses de base I2C, limites de securite | `GET /api/capabilities` → objet `hardware` |
| Presence du micro INMP441 | `GET /api/mic/status` |

L'objet `hardware` de `/api/capabilities` reprend les constantes de
`engine/src/core/config.h` et `engine/src/core/status_led.h`, de facon que la
page de cablage ne redeclare aucun numero de broche cote interface :

```json
{
  "hardware": {
    "board": "ESP32 (WROOM-32)",
    "i2cSda": 21, "i2cScl": 22, "i2cFreq": 400000,
    "mcpBaseAddress": 32, "mcpMaxModules": 4,
    "pcaBaseAddress": 64, "pcaMaxModules": 4, "pcaFrequency": 50,
    "statusLedPin": 2, "bootButtonPin": 0,
    "micSck": 26, "micWs": 25, "micSd": 33,
    "maxConcurrentActive": 8,
    "solenoidMaxOnMs": 500, "motorMaxContinuousMs": 5000,
    "ledMaxStrips": 4
  }
}
```

## Maintenance

Toute la generation vit dans `engine/data/index.html` (section
`Cablage / Branchements electriques`). Le rapport est construit une seule fois
sous forme de sections/blocs (`buildWiringModel()`), puis rendu soit en HTML
(`wiringBlockHtml()`), soit en Markdown (`wiringToMarkdown()`) : ajouter une
section la rend disponible dans les deux sorties sans duplication.

Types de blocs disponibles : `h`, `p`, `note`, `pre`, `list`, `table`, `diag`
(schema synoptique SVG), `schem` (schema de principe : SVG en HTML, ASCII en
Markdown), `tiles`, `legend`, `checks`, `chans` (fiches de sorties), `todo`
(checklist). Un nouveau type doit etre gere dans les deux fonctions de rendu.

Les schemas de principe sont composes a partir d'une bibliotheque de symboles
(`wSymbols()` : fusible, arret d'urgence, condensateur, diode, resistance,
MOSFET, bobine, moteur, contact, masse). Les valeurs affichees viennent des
memes fonctions de calcul que la nomenclature (`wFuseFor()`, `wCapFor()`,
`wDiodeFor()`, `wMosfetFor()`), donc un schema ne peut pas diverger du reste
de la page.

Le schema est produit par `wBuildDiagram()`, qui recoit un modele deja calcule
(modules, sorties directes, entrees, strips, lignes de l'ESP32 et des
alimentations) et ne fait que du placement. Les couleurs viennent de
`wPalette()`, resolues en valeurs litterales selon le theme clair ou sombre :
c'est ce qui permet a l'export `.svg` d'etre autonome.

L'interface est servie depuis la flash du programme et non depuis LittleFS :
apres toute modification de `engine/data/index.html`, regenerez l'en-tete
embarque, sinon le firmware continue de servir l'ancienne page.

```bash
python3 tools/gen_index_gz.py
```
