On va définir le noyau minimal V1 : suffisamment puissant pour couvrir 95% des cas (hi-hat complexe, alternance thermique, moteurs, mute, etc.), mais sans complexité inutile.

Objectif :
✔ Modulaire
✔ Extensible
✔ Temps constant
✔ Simple à configurer

🎯 PRINCIPE

On réduit tout à 5 catégories :

Entrée

Condition

Transformation

Temps

Sortie

Chaque bloc doit être :

générique

indépendant du hardware

combinable

compilable en pipeline fixe

🧩 LISTE MINIMALE DES BLOCS V1
1️⃣ BLOCS D’ENTRÉE (Input)

Minimum indispensable :

✔ Note Trigger

Note On

Option : inclure Note Off

Vélocité disponible

✔ CC Input

Valeur 0–127

Peut être continu

👉 Suffisant pour tout le GM Drum.

2️⃣ BLOCS DE CONDITION (Logique)

Minimum :

✔ Condition Seuil
si valeur > X
si valeur < X


Sources possibles :

CC

Variable globale

Position servo

État interne

✔ Alternance (Round Robin)

N sorties

Option pondération

Indispensable pour :

Double solénoïde

Répartition thermique

✔ Split vélocité

Plage min/max

Permet layering

👉 Ces 3 blocs couvrent 90% des besoins logiques.

3️⃣ BLOCS DE TRANSFORMATION

Minimum vital :

✔ Courbe vélocité

linéaire

expo

log

LUT custom

✔ Gain / Scale

Multiplie ou réduit la valeur.

✔ Clamp (limiteur)

Fixe min / max.

👉 Pas besoin de plus en V1.

4️⃣ BLOCS TEMPORELS

Critique pour percussions.

✔ Impulsion

Durée paramétrable

Intensité dépend vélocité

✔ Delay

Délai fixe en ms

Option dépend vélocité

✔ Ramping simple

Pour servo / moteur :

montée progressive

descente progressive

👉 ADSR complet inutile en V1.

5️⃣ BLOCS DE SORTIE (Output)

Abstraction universelle.

✔ Sortie Impulsion

Pour :

solénoïde

relais

trigger court

Paramètres :

durée

intensité

✔ Sortie Position

Pour servo / stepper.

Paramètres :

position cible

vitesse

✔ Sortie PWM

Pour moteur DC.

Paramètres :

vitesse

accel simple

👉 Ces 3 couvrent tout.




À ajouter plus tard :

LFO

Random

Séquence

Compteur

Mode toggle

Envelope

Clock sync
