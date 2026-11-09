# D33P

Reverb ambient sombre (VST3 / AU / Standalone, Windows et macOS) construite avec JUCE.
Pensée pour de longues nappes façon dark ambient : 10 à 60 secondes de queue, puis l'infini.

## Installer

Télécharge le dernier build dans les **Releases** du dépôt (pré-release `latest`, mise à jour
à chaque push sur `main`) ou dans les artefacts de l'onglet **Actions**.

| Système | Fichier | Où le copier |
|---|---|---|
| Windows | `D33P.vst3` | `C:\Program Files\Common Files\VST3\` |
| macOS | `D33P.vst3` | `~/Library/Audio/Plug-Ins/VST3/` |
| macOS | `D33P.component` (AU) | `~/Library/Audio/Plug-Ins/Components/` |

Dans FL Studio : *Options → Manage plugins → Find more plugins*, puis l'ajouter en effet sur
une piste du mixer. Dans Ableton : *Préférences → Plug-ins → VST3 personnalisés*, puis
« Rescan ».

Le build macOS n'est signé qu'en ad hoc : si macOS bloque le plugin, lance
`xattr -cr ~/Library/Audio/Plug-Ins/VST3/"D33P.vst3"` (et la même chose pour le `.component`).

## Commandes

| Potard | Effet |
|---|---|
| **SIZE** | Taille de l'espace (longueur des 16 lignes de retard), de la crypte à la cathédrale. La queue en cours garde sa taille, les nouveaux sons prennent la nouvelle : tourner le potard ne fait jamais glisser le pitch. |
| **DECAY** | Temps de décroissance (RT60), de 1 à 60 s. |
| **DAMPING** | Absorption des aigus : plus c'est haut, plus la queue s'assombrit vite. |
| **SHIMMER** | Halo transposé réinjecté dans la boucle. 7 crans : −12, −7, −5, Off, +5, +7, +12 demi-tons. |
| **DUCKING** | Baisse la reverb pendant que tu joues ; elle remonte dans les silences. |
| **MIX** | Mélange dry/wet à puissance constante. |
| **FREEZE** | Gel total : gain de boucle à 1, plus d'absorption, modulation quasi arrêtée et shimmer suspendu, donc la nappe garde le timbre de ce qui jouait. Les nouveaux sons passent en direct sans s'y ajouter. Limiteur doux en sécurité. |

Un coupe-bas fixe (100 Hz en entrée, 70 Hz en sortie, 12 dB/oct) garde la queue propre.

## Export WAV par glisser-déposer

Le plugin enregistre en continu la sortie de la reverb. La zone en haut à droite affiche la
prise en cours et sa durée : **attrape-la et glisse-la dans une piste** de FL Studio ou
d'Ableton pour créer un WAV 24 bits.

* Mode normal : de l'attaque du son jusqu'à ce que la queue devienne silencieuse
  (−70 dBFS), limité à 60 s (fondu de sortie si la limite est atteinte).
* Mode FREEZE : les 30 dernières secondes au maximum, avec un fondu de sortie de 4 s.

Les fichiers sont gardés dans `Documents/D33P/Exports` (double-clic sur la zone pour
ouvrir le dossier). Ils ne sont jamais supprimés automatiquement, ton projet ne perd donc
pas ses samples.

## Visuel

Fenêtre de 720 × 440 par défaut (redimensionnable de 540 à 1440 px de large).
Fond OpenGL temps réel : une nébuleuse qui réagit au son (densité selon le niveau, cœur qui
pulse avec les graves, étoiles qui scintillent avec les aigus, couleurs selon le Damping,
étincelles qui montent ou tombent selon le shimmer). **FREEZE** effondre la nébuleuse en trou
noir : le gaz est aspiré en spirale, éclair d'effondrement, l'horizon s'ouvre, puis le disque
d'accrétion s'allume ; à la sortie, le trou s'évapore dans un éclair et une onde de choc. Clique dans le vide
pour lancer des ondes de choc ; le curseur attire légèrement le gaz. La fenêtre se
redimensionne (proportions fixes) pour les captures d'écran.

## Algorithme

```
entrée → coupe-bas → 4 diffuseurs passe-tout (plate de Dattorro) par canal
       → 2 réservoirs FDN de 16 lignes (longueurs premières entre elles, Hadamard orthonormée, Jot)
         · changement de Taille : les nouveaux sons partent dans l'autre réservoir
         · modulation lente et légère de chaque retard (LFO 0,07–0,19 Hz, interpolation Hermite)
         · absorption de Jot (shelf du 1er ordre) : les aigus décroissent plus vite
         · shimmer : pitch-shifter granulaire réinjecté, gain de boucle compensé en énergie
       → coupe-bas de sortie → limiteur doux → ducking → dry/wet
```

Le moteur (`src/dsp`) est en C++ pur, sans JUCE, et testé indépendamment.

## Compiler

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release      # tests du traitement du son
```

Seulement les tests DSP (sans télécharger JUCE) : `cmake -B build -DHN_BUILD_PLUGIN=OFF`.

La CI GitHub Actions compile Windows (VST3 + Standalone) et macOS universel (VST3 + AU +
Standalone), lance les tests DSP, `auval`, puis **pluginval** (niveau de rigueur 10) sur
chaque format.

## Licences

JUCE 8 (AGPLv3 / licence commerciale JUCE). Polices : Syncopate (Apache 2.0), Rajdhani
(SIL OFL 1.1), voir `assets/fonts`.
