# Analyse UX — Interface Drums Engine MIDI

> Analyse du point de vue d'un utilisateur debutant qui decouvre le systeme pour la premiere fois.

---

## 1. Premiere experience / Onboarding

### Probleme : Aucun guidage au premier lancement
- L'utilisateur arrive sur un onglet "Actionneurs" vide sans savoir par ou commencer.
- Le workflow necessaire (Actionneurs → Instruments → Jouer) n'est explique nulle part.

**Solution proposee :**
- Ajouter une banniere de bienvenue persistante quand il y a 0 instruments :
  ```
  "Bienvenue ! Pour commencer :
   1. Ajoutez vos actionneurs (solenoide, servo, moteur)
   2. Creez un instrument (associe une note MIDI a un actionneur)
   3. Envoyez du MIDI et jouez !"
  ```
- Ajouter un indicateur d'etapes dans la barre de navigation (etape 1/2/3).

### Probleme : Ordre des onglets pas explicite
- Les onglets sont : Actionneurs | Instruments | LEDs | Pipeline | Piano | Calibration
- Un debutant ne sait pas qu'il faut configurer les actionneurs AVANT de creer des instruments.

**Solution proposee :**
- Ajouter un badge numerote subtil sur les 2 premiers onglets : "1" et "2"
- Quand l'utilisateur ouvre le modal instrument sans actionneur, afficher : "Aucun actionneur trouve. Ajoutez d'abord un actionneur. [Aller aux actionneurs →]"

---

## 2. Configuration des actionneurs

### Probleme : Formulaire trop complexe d'emblee
- Le modal affiche 10+ champs immediatement (Type, Comportement, Bus, Pin, temps min/max, cooldown...)
- Un debutant ne sait pas quels champs sont importants.

**Solution proposee :**
- Mettre le selecteur de **preset en evidence** (bordure coloree, texte "Recommande") tout en haut du formulaire.
- Ajouter un texte : "Astuce : commencez par un preset, puis personnalisez."
- Regrouper les parametres avances dans une section repliable "Parametres avances".

### Probleme : Jargon technique sans explication
- "MCP23017 (I2C GPIO)", "PCA9685 (I2C PWM)", "ESP32 LEDC PWM" supposent des connaissances hardware.
- "PWM", "I2C", "GPIO", "CC#" ne sont jamais definis.

**Solution proposee :**
- Renommer les options du bus :
  - "MCP23017 (I2C GPIO)" → "Carte GPIO I2C (solenoides)" avec tooltip explicatif
  - "PCA9685 (I2C PWM)" → "Carte PWM I2C (servos)" avec tooltip
  - "ESP32 GPIO" → "Pin directe ESP32"
  - "ESP32 LEDC PWM" → "PWM ESP32 (moteurs/LEDs)"
- Ajouter des tooltips `title="..."` sur tous les termes techniques.

### Probleme : Champs obligatoires non identifies
- Aucune indication visuelle sur ce qui est requis vs optionnel.
- L'utilisateur remplit au hasard.

**Solution proposee :**
- Marquer les champs obligatoires avec une asterisque rouge *.
- Valider a la soumission avec un message clair.

### Probleme : Pas de detection de conflit de pin
- On peut assigner 2 actionneurs sur le meme pin sans avertissement.

**Solution proposee :**
- Verifier a la saisie si le pin est deja utilise par un autre actionneur.
- Afficher un warning : "Attention : ce pin est deja utilise par [nom actionneur]."

---

## 3. Creation d'instruments

### Probleme : 9 types d'instruments sans explication
- Les options "Frappe alternee", "Servo alterne", "Shaker", "Brosse double servo" ne sont pas claires pour un debutant.

**Solution proposee :**
- Ajouter une description dynamique sous le selecteur de type qui change selon la selection :
  ```
  Simple : "Un seul actionneur frappe a chaque note MIDI. Ideal pour kick, snare, tom."
  Frappe alternee : "2 actionneurs alternent a chaque frappe. Simule un roulement."
  Avec mute : "Frappe + actionneur pour etouffer le son apres la note."
  ...
  ```
- Grouper les types par difficulte :
  ```
  -- Basique --
  Simple
  -- Intermediaire --
  Frappe alternee, Avec mute
  -- Avance --
  Hi-Hat, Moteur, Personnalise
  ```

### Probleme : Retrigger non explique
- Les options "Ignore", "Reset", "Stack" sont affichees sans contexte.

**Solution proposee :**
- Ajouter un hint sous le champ :
  ```
  "Que faire si la meme note est rejouee rapidement ?
   - Ignore : la 2e frappe est ignoree
   - Reset : la 2e frappe relance l'actionneur
   - Stack : les 2 frappes jouent en parallele"
  ```

### Probleme : Le bouton "Learn" est cryptique
- Un debutant ne comprend pas que "Learn" signifie "jouer une note MIDI pour l'assigner".

**Solution proposee :**
- Renommer en "Auto-Learn" ou afficher l'icone avec un label plus explicite.
- Ajouter un tooltip : "Appuyez puis jouez une note sur votre controleur MIDI pour l'assigner automatiquement."

### Probleme : Dropdown MIDI Note = 128 elements
- Difficile a naviguer, les noms de notes standards (C1 = kick, D1 = snare...) ne sont pas mis en avant.

**Solution proposee :**
- Ajouter un groupe "Notes percussion GM" en debut de liste avec les noms standards.
- Ajouter un filtre texte au-dessus : "Tapez pour rechercher (ex: C1, 36, kick...)"

---

## 4. Terminologie et langue

### Probleme : Melange francais / anglais
L'interface est majoritairement francaise mais utilise beaucoup de termes anglais :
- "Strike", "Hold", "Mute", "Bus", "Pin", "PWM", "NoteOn", "NoteOff", "Learn"
- Melange dans les memes sections (ex: "Actionneur de frappe" a cote de "Retrigger Mode")

**Solution proposee :**
- Choisir une langue principale et s'y tenir.
- Suggestion : garder le francais pour l'interface, garder les termes techniques MIDI en anglais (NoteOn, CC, etc.), et toujours fournir la traduction entre parentheses pour les termes ambigus.

### Probleme : Noms inconsistants
- Le meme concept est nomme differemment : "Actionneur frappe" / "Strike Actuator" / "Solenoide frappe"

**Solution proposee :**
- Standardiser : toujours "Actionneur [fonction]" → "Actionneur de frappe", "Actionneur mute", "Actionneur moteur"

---

## 5. Feedback visuel

### Probleme : Pas de feedback apres creation reussie
- Apres sauvegarde d'un actionneur, le modal se ferme mais il n'y a pas toujours un message de confirmation visible.

**Solution proposee :**
- Toast de confirmation avec suggestion de l'etape suivante :
  ```
  "Actionneur 'Kick Solenoid' cree avec succes. Prochaine etape : Creez un instrument."
  ```

### Probleme : Statut "Actif" pas assez visible dans la liste
- Le badge "ACTIF" est petit et vert, pas tres visible quand l'actionneur est actif.
- Le badge "OFF" pour un actionneur desactive n'est pas assez visible.

**Solution proposee :**
- Un actionneur desactive devrait etre grise dans la liste.
- Ajouter un indicateur de couleur sur la bordure gauche (vert = actif, rouge = desactive).

---

## 6. Pipeline (editeur avance)

### Probleme : L'onglet Pipeline est intimidant
- Le nom "Pipeline" est du jargon technique.
- L'editeur est puissant mais incomprehensible pour un debutant.

**Solution proposee :**
- Renommer l'onglet "Pipeline" en "Traitement MIDI" ou "Avance"
- Ajouter un texte d'introduction : "Optionnel : ajoutez des filtres, courbes, et conditions au traitement MIDI de vos instruments."
- Le bouton "Ajouter un bloc de traitement" (deja implemente) est la bonne approche.

---

## 7. Erreurs courantes du debutant

| Erreur | Cause | Solution proposee |
|--------|-------|-------------------|
| Aucun son | Actionneur desactive | Warning si instrument utilise un actionneur OFF |
| Pin en conflit | 2 actionneurs sur meme pin | Detection automatique + warning |
| Mauvais bus | Solenoide sur PCA9685 (servos) | Filtrer les options de bus selon le type |
| Instrument sans actionneur | Oubli de selectionner | Validation obligatoire |
| Mauvaise note MIDI | Ne connait pas le mapping | Presets GM avec noms |

---

## Resume : priorites d'implementation

| Priorite | Amelioration | Effort |
|----------|-------------|--------|
| **P0** | Banniere de bienvenue / workflow guide | Faible |
| **P0** | Preset en evidence dans le formulaire actionneur | Faible |
| **P1** | Description dynamique pour chaque type d'instrument | Faible |
| **P1** | Hint Retrigger | Faible |
| **P1** | Tooltips termes techniques | Moyen |
| **P2** | Detection conflit de pin | Faible |
| **P2** | Grouper les types d'instruments par difficulte | Faible |
| **P2** | Renommer onglet Pipeline | Faible |
| **P3** | Standardiser la langue (fr/en) | Eleve |
| **P3** | Formulaire actionneur en sections repliables | Moyen |
