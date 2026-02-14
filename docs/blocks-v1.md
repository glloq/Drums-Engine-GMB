# Blocs pipeline V1 (minimum)

## Catégories
1. **Input**
2. **Condition**
3. **Transform**
4. **Time**
5. **Output**

## Catalogue minimal
### Input
- Note Trigger
- CC Input

### Condition
- Seuil
- Alternance (round-robin)
- Split vélocité

### Transform
- Courbe vélocité
- Gain / Scale
- Clamp

### Time
- Pulse (durée paramétrable)
- Delay
- Ramp simple

### Output
- Pulse (solénoïde/relais)
- Position (servo)
- PWM (moteur)

## Principes
- Blocs génériques et composables.
- Compile-time friendly pour exécution déterministe.
- Indépendance hardware (binding effectué côté actuator/HAL).

## Extensions prévues
- LFO
- Random/Humanize
- Compteur
- Toggle
- Sync clock externe
