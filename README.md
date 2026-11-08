# Horizon Noir

Reverb ambient sombre (VST3 / AU / Standalone, Windows et macOS) construite avec JUCE.
Pensée pour de longues nappes façon dark ambient : 10 à 60 secondes de queue, puis l'infini.

## Installer

Télécharge le dernier build dans les **Releases** du dépôt (pré-release `latest`, mise à jour
à chaque push sur `main`) ou dans les artefacts de l'onglet **Actions**.

| Système | Fichier | Où le copier |
|---|---|---|
| Windows | `Horizon Noir.vst3` | `C:\Program Files\Common Files\VST3\` |
| macOS | `Horizon Noir.vst3` | `~/Library/Audio/Plug-Ins/VST3/` |
| macOS | `Horizon Noir.component` (AU) | `~/Library/Audio/Plug-Ins/Components/` |

Dans FL Studio : *Options → Manage plugins → Find more plugins*, puis l'ajouter en effet sur
une piste du mixer. Dans Ableton : *Préférences → Plug-ins → VST3 personnalisés*, puis
« Rescan ».

Le build macOS n'est signé qu'en ad hoc : si macOS bloque le plugin, lance
`xattr -cr ~/Library/Audio/Plug-Ins/VST3/"Horizon Noir.vst3"` (et la même chose pour le `.component`).

## Commandes

| Potard | Effet |
|---|---|
| **Taille** | Longueur des 16 lignes de retard (de la crypte à la cathédrale). |
| **Durée** | Temps de décroissance (RT60) de 1 à 60 s. |
| **Sombre / Clair** | Vitesse à laquelle les aigus meurent dans la queue. En sombre, la queue s'assombrit très vite. |
| **Shimmer** | Transposition de −12 à +12 demi-tons réinjectée dans la boucle (Off à 0). |
| **Ducking** | Baisse la reverb pendant que tu joues ; elle remonte dans les silences. |
| **Dry / Wet** | Mélange à puissance constante. |
| **INFINI** | Gain de boucle à 1 : la nappe est tenue indéfiniment. Les nouveaux sons ne rentrent plus dans la reverb (ils passent seulement en direct). Un limiteur doux et une régulation lente gardent le niveau constant. |

Un coupe-bas fixe (100 Hz en entrée, 70 Hz en sortie, 12 dB/oct) garde la queue propre.

## Export WAV par glisser-déposer

Le plugin enregistre en continu la sortie de la reverb. La zone en haut à droite affiche la
prise en cours et sa durée : **attrape-la et glisse-la dans une piste** de FL Studio ou
d'Ableton pour créer un WAV 24 bits.

* Mode normal : de l'attaque du son jusqu'à ce que la queue devienne silencieuse
  (−70 dBFS), limité à 60 s (fondu de sortie si la limite est atteinte).
* Mode INFINI : les 30 dernières secondes au maximum, avec un fondu de sortie de 4 s.

Les fichiers sont gardés dans `Documents/Horizon Noir/Exports` (double-clic sur la zone pour
ouvrir le dossier). Ils ne sont jamais supprimés automatiquement, ton projet ne perd donc
pas ses samples.

## Visuel

Fond OpenGL temps réel : une nébuleuse qui réagit au son (densité selon le niveau, cœur qui
pulse avec les graves, étoiles qui scintillent avec les aigus, couleurs selon Sombre/Clair,
étincelles qui montent ou tombent selon le shimmer). **INFINI** effondre la nébuleuse en trou
noir (lentille gravitationnelle, disque d'accrétion, anneau de photons). Clique dans le vide
pour lancer des ondes de choc ; le curseur attire légèrement le gaz. La fenêtre se
redimensionne (proportions fixes) pour les captures d'écran.

## Algorithme

```
entrée → coupe-bas → 4 diffuseurs passe-tout (plate de Dattorro) par canal
       → FDN 16 lignes (longueurs premières entre elles, matrice de Hadamard orthonormée, Jot)
         · modulation lente de chaque retard (LFO 0,07–0,19 Hz, interpolation Hermite)
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
