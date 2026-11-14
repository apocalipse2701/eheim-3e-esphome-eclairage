# Protocole de la pompe EHEIM professionel 3e

Description établie par observation des échanges entre le logiciel EHEIM ControlCenter et la pompe, puis validée sur un **2076 (professionel 3e 450), firmware 3.80**. Elle devrait s'appliquer aux 2074 (350), 2076 (450) et 2078 (700).

## Liaison

| Paramètre | Valeur |
|---|---|
| Bus | RS485 half-duplex, jack 3,5 mm sur la tête de pompe |
| Brochage (projet Hackaday) | pointe = B, bague = A, manchon = GND |
| Vitesse | 115200 bauds, 8N1, sans contrôle de flux |
| Câble d'origine | EHEIM Interface 4020740 (puce FTDI, port COM virtuel) |

En half-duplex, l'adaptateur relit sa propre trame : il faut ignorer cet **écho** avant de lire la réponse de la pompe.

## Format des trames

```
0x55 | adresse | longueur totale | commande | données…
```

- `0x55` : octet de début
- adresse : `0x60` pour la pompe, `0xFF` pour la lecture du firmware ; les réponses viennent de `0x60`
- longueur : nombre total d'octets de la trame, en-tête compris
- **pas de checksum**

Les requêtes font 5 octets (un octet de données, souvent `0x00`).

## Commandes utilisées

| Commande | Requête | Réponse |
|---|---|---|
| Version firmware | `55 FF 05 01 00` | `55 60 06 FF <maj> <min>` → ex. 3.80 |
| Type de pompe | `55 60 05 16 00` | `55 60 06 90 16 <type-2000>` → ex. `4C` = 2076 |
| Données temps réel | `55 60 05 13 00` | `55 60 37 10` + 51 octets (voir ci-dessous) |
| Flux constant, palier n | `55 60 05 28 <n>` | `55 60 06 90 28 …` (accusé) |

### Commandes d'écriture (changement de mode)

Toutes sont envoyées à l'adresse `0x60` et acquittées par `55 60 06 90 <commande> …`.

| Commande | Trame | Données |
|---|---|---|
| Touche (mode manuel) | `55 60 05 22 <k>` | `k` = `02` débit +, `04` débit − |
| Mode | `55 60 06 24 <lo> <hi>` | mot de mode : `0004` Bio, `0008` Stream, `0010` manuel |
| Stream (pulsé) | `55 60 0E 25 …` | durée haute (int32 LE, s), durée basse (int32 LE, s), palier haut, palier bas ; durées 5 à 100 s |
| Bio 12 h | `55 60 08 26 <nuit> <jour> <lo> <hi>` | palier des 12 heures suivantes, palier des 12 premières heures, minutes avant le début (uint16 LE) |
| Heure | `55 60 44 4A <h> <m> <hd> <md> …` | heure actuelle, heure de début du cycle Bio, complété par des zéros (68 octets au total) |

Séquences reprises du logiciel ControlCenter :

| Mode visé | Séquence |
|---|---|
| Flux constant | `28 <palier>` |
| Bio 12 h | (si manuel : `28 <palier>`) → `24 0004` → `26 …` → `4A …` |
| Stream | (si manuel ou Bio : `28 <palier>`) → `24 0008` → `25 …` |
| Manuel | `28 00` → `24 0010` → `24 0010` |

## Données temps réel (51 octets, little-endian, sans alignement)

| Offset | Type | Champ | Utilisation |
|---|---|---|---|
| 0 | int16 | Frequenz | vitesse rotor tr/min = valeur / 100 × 60 |
| 2 | int16 | Frequenz_Soll | fréquence de consigne |
| 4 | int16 | Current | courant (unité non confirmée) |
| 6 | int16 | Peak_Current | |
| 8 | int16 | CalcCurrent | |
| 10 | int16 | DFSFaktor | |
| 12 | int16 | DFS | **débit mesuré L/h = valeur × 60 / 100** |
| 14 | uint8 | Drehzahl | |
| 15 | uint8 | Drehrichtung | |
| 16 | uint8 | Pulsbreite | |
| 17 | uint8 | PumpeOn | 1 = pompe en marche |
| 18 | uint8 | KeyState | |
| 19 | uint8 | Sensor | |
| 20 | int16 | LED_Data | |
| 22 | int16 | PumpeMode | mot de mode (voir ci-dessous) |
| 24 | uint8 | MenuMode | |
| 25 | uint8 | Status | |
| 26 | uint8 | SollStep | palier de débit 0 à 14 |
| 27 | uint8 | DFSCalcDiff | écart débit voulu / obtenu |
| 28 | int16 | Regler_DFSSoll | |
| 30 | int16 | Regler_FreqCalc | |
| 32 | int16 | Regler_FreqSet | consigne du régulateur |
| 34 | int16 | DFS_Stunde | |
| 36 | int16 | DFS_Tag | |
| 38 | int16 | Winkel | |
| 40 | int32 | RunTime | temps de fonctionnement en minutes |
| 44 | int32 | ActualTime | minutes depuis le dernier entretien |
| 48 | int16 | WartungHours | heures avant le prochain entretien |
| 50 | uint8 | WartungMonth | |

Encrassement affiché par ControlCenter = heures depuis entretien × 100 / (heures depuis + heures avant). C'est un simple compteur de temps.

## Mot de mode (PumpeMode)

| Bit | Signification |
|---|---|
| `0x0001` | fonctionnement normal (flux constant) |
| `0x0002` | consigne de débit non atteinte (observé) |
| `0x0004` | mode Bio 12 h |
| `0x0008` | mode Stream (pulsé) |
| `0x0010` | mode manuel (régulation désactivée) |
| `0x0100` | erreur rotor |
| `0x0200` | erreur air |
| `0x0400` | erreur température |

## Débit par palier (L/h)

Index = palier + 1 (la case 0 n'est pas utilisée par le flux constant).

| Type | Paliers 0 → 14 |
|---|---|
| 2074 | 400 440 480 515 550 585 620 650 680 710 740 770 800 830 860 |
| 2076 | 400 460 515 565 610 650 690 730 770 805 840 875 910 945 980 |
| 2078 | 400 470 540 600 650 700 745 785 825 865 905 945 985 1025 1065 |
