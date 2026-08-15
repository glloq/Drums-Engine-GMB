# Page "Cablage" — branchements electriques generes

L'onglet **Cablage** de l'interface web produit un dossier de cablage complet a
partir de la configuration reelle du controleur : rien n'y est saisi a la main,
tout est deduit des actionneurs, des fins de course et des strips LED declares.

## Ce que la page genere

| Section | Contenu |
|---|---|
| Synthese | Compte des solenoides / servos / moteurs / modules, detection des conflits de broches et des broches sensibles (strapping, flash SPI) |
| Nomenclature (BOM) | Liste des composants a acheter avec quantites : MOSFET, diodes de roue libre, resistances de gate et de pull-down, condensateurs de reservoir et de decouplage, fusibles, adaptateur de niveau LED, pull-ups I2C |
| Affectation des GPIO ESP32 | Table broche par broche : fonction, sens, cablage attendu — y compris les broches reservees par le firmware (I2C, LED de statut, micro I2S) |
| Modules I2C | Un tableau par MCP23017 / PCA9685 reellement utilise : position des cavaliers d'adresse, canaux occupes, alimentation logique et de puissance |
| Alimentations | Un rail par famille d'actionneurs : courant simultane, pire cas, alimentation conseillee, calibre de fusible, condensateur de reservoir, section de fil |
| Schemas de principe | Schemas ASCII par type d'etage : solenoide sur MOSFET / ULN2803 / module, servo sur PCA9685, moteur simple, moteur sur pont en H, fin de course, capteur optique, strip LED |
| Cablage detaille | Pour chaque actionneur, la marche a suivre fil par fil, avec ses parametres de configuration (angles, durees d'impulsion, plage de PWM, temps mort) |
| Rappels de securite | Ordre de mise sous tension, verifications thermiques, limites du firmware |

Deux exports sont disponibles :

- **Imprimer** — feuille imprimable (l'en-tete, les onglets et les controles
  sont masques a l'impression).
- **Exporter .md** — telecharge `drums-engine-cablage.md`, le meme dossier au
  format Markdown, a joindre au projet ou a imprimer plus tard.

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
| Etage de puissance solenoides | MOSFET | MOSFET discret, ULN2803A, ou module pre-cable — change la BOM, les schemas et les etapes de cablage |

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

La page signale, avant tout cablage :

- **Conflit de broches** — deux fonctions sur le meme GPIO (erreur bloquante).
- **Broches de la flash SPI** (GPIO 6 a 11) — la carte ne demarrerait pas.
- **Broches de strapping** (GPIO 0, 2, 5, 12, 15) — leur niveau au demarrage
  change le mode de boot ; le pull-down de gate devient indispensable.

## Source des donnees

| Donnee | Origine |
|---|---|
| Actionneurs, bus, adresses, broches, fins de course | `GET /api/actuators` |
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

L'interface est servie depuis la flash du programme et non depuis LittleFS :
apres toute modification de `engine/data/index.html`, regenerez l'en-tete
embarque, sinon le firmware continue de servir l'ancienne page.

```bash
python3 tools/gen_index_gz.py
```
