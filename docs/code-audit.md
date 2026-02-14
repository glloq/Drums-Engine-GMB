# Audit du code actuel et plan de modifications

## Objectif de l'audit
Permettre la configuration rapide de systèmes de percussions variés avec différents actionneurs (servo, solénoïde, moteur DC), tout en gardant une UX simple et intuitive.

## Contrôle du code actuel

### Points solides
- Architecture modulaire claire (`HAL -> Actuator -> Scheduler -> Event -> MIDI -> Web`).
- API REST déjà orientée CRUD pour actionneurs/instruments/boucles.
- UI web embarquée déjà fonctionnelle avec test direct d'actionneur/instrument.
- Persistance LittleFS opérationnelle.

### Points bloquants identifiés
1. **Compatibilité type/bus peu guidée**
   - Le backend n'acceptait pas clairement les combinaisons incompatibles.
   - Exemple: servo configuré hors PCA9685 pouvait être créé puis échouer au rebuild.

2. **Validation de configuration incomplète**
   - Peu de garde-fous sur les plages `paramMin/paramMax/paramDefault`.

3. **UX de création d'actionneur perfectible**
   - L'utilisateur devait connaître les paramètres sans assistance.
   - Manque de presets et d'aides contextuelles.

4. **Documentation d'audit/action manquante**
   - Besoin d'un plan explicite “quoi améliorer ensuite”.

## Modifications implémentées dans cette itération

### Backend
- Ajout d'un endpoint `GET /api/capabilities` pour exposer les capacités moteur (types, bus recommandés, limites).
- Ajout de validation serveur de la configuration actionneur:
  - `paramMin <= paramMax`
  - `paramDefault` dans la plage
  - compatibilité type/bus (servo=PCA9685, etc.)

### Frontend (UI)
- Ajout de **presets rapides** d'actionneur:
  - Solénoïde Kick/Snare
  - Servo Hi-Hat
  - Moteur Shaker
- Ajout d'une **aide contextuelle visuelle** dans le modal actionneur.
- Ajout de validations côté client avant sauvegarde (plages, bus compatible pour servo, etc.).
- Auto-préremplissage des paramètres selon type d'actionneur.

## Modifications recommandées (prochaines étapes)

### Priorité haute
1. **Wizard complet de création instrument** (3 étapes guidées)
   - Choix instrument cible
   - Suggestion d'actionneur(s)
   - Mapping MIDI + test immédiat

2. **Compatibilité dynamique type/behavior**
   - Limiter les comportements listés selon le type réel.

3. **Templates de kits**
   - Kits prêts à l'emploi: Rock kit, Electro kit, Latin kit.

### Priorité moyenne
4. **Feedback runtime enrichi**
   - Affichage erreurs de validation backend directement dans les formulaires.
   - Monitoring live (latence, queue usage, actionneur saturé).

5. **Pipeline editor visuel**
   - Drag & drop blocs, preview de chaîne compilée, test local.

### Priorité technique
6. **Tests non-régression API**
   - Cas invalides type/bus
   - Cas limites paramètres
   - Charge multi-actionneurs

7. **Portabilité**
   - Isoler davantage les dépendances ESP32 si cible multi-plateforme.
