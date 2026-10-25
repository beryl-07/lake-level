# Station embarquée de mesure et de surveillance du niveau d’eau d’un lac

## Contexte
La surveillance du niveau d’eau des lacs, barrages et réservoirs est essentielle pour la gestion des ressources hydriques, la prévention des inondations et la protection de l’environnement.
Ce projet consiste à concevoir une station embarquée autonome, capable de mesurer le niveau d’eau à l’aide d’un capteur à ultrasons, d’analyser les données localement et de générer des alertes.

## Objectifs du projet
- Utiliser un télémètre à ultrasons pour mesurer une distance
- Déduire le niveau d’eau à partir de cette distance
- Mettre en place un traitement embarqué fiable
- Concevoir une architecture prête pour une future intégration IoT

## Matériel imposé
- Arduino Uno
- Capteur ultrason HC-SR04
- Bouton poussoir
- LED (verte / orange / rouge)
- Buzzer
- Photorésistance (optionnelle – mode jour/nuit)
- Afficheur 7 segments 4 en 1

## Fonctionnalités attendues
1. Mesure périodique de la distance entre le capteur et la surface de l’eau
2. Calcul du niveau d’eau réel (référence à une hauteur fixe)
3. Définition de trois états :
- Niveau normal
- Niveau de vigilance
- Niveau critique
4. Signalisation locale :
- LED verte / orange / rouge
- alarme sonore en niveau critique
5. Bouton poussoir pour :
- activer/désactiver le système,
- ou recalibrer la hauteur de référence
6. Filtrage des mesures (moyenne glissante ou médiane)

## Livrables
- Code Arduino commenté
- Schéma électronique détaillé
- Rapport technique (10 à 15 pages) comprenant :
- Démonstration fonctionnelle

## Tableau de bord Web (nouveau)
Le firmware expose maintenant un tableau de bord Web embarqué (style éditorial) via le serveur HTTP de l’ESP32.

### Accès
1. Démarrer la carte et attendre la connexion Wi-Fi.
2. Récupérer l’adresse IP affichée dans le moniteur série.
3. Ouvrir cette IP dans un navigateur (port `80`).

### Informations affichées
- État global du lac (`NIVEAU BAS`, `VIGILANCE`, `CRITIQUE`)
- Niveau d’eau actuel (cm)
- Distance capteur-surface (cm)
- Taux de remplissage (%)
- Seuils configurés (bas, vigilance, référence)
- Statut de calibration, état alarme, uptime, fraîcheur de la dernière mesure

### API de statut
Une API JSON est disponible sur:
- `GET /api/status`

Elle retourne les mesures en direct pour un usage frontend ou intégration IoT future.