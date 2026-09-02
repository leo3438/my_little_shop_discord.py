/**
 * Reconnaissance des noms de consoles à partir des dossiers Magic Monkei, dont
 * la structure n'est pas uniforme (on y trouve parfois des titres de jeux ou des
 * balises, ex. « ! TOV (NSP eShop) », « [Nintendo Switch] »).
 *
 * Principe : on ne garde QUE les plateformes d'une liste reconnue (allow-list),
 * après normalisation (minuscules, sans accents ni ponctuation). Tout ce qui ne
 * correspond pas exactement à une console connue est ignoré (chaîne vide), ce
 * qui évite les faux positifs dans le menu déroulant.
 */

// Canonique -> alias (formes acceptées). Les alias sont normalisés au chargement.
const CONSOLES = {
  'Nintendo Entertainment System': ['nes', 'famicom', 'nintendofamicom'],
  'Super Nintendo': ['snes', 'superfamicom', 'supernes', 'supernintendoentertainmentsystem'],
  'Nintendo 64': ['n64'],
  'GameCube': ['gamecube', 'ngc', 'gcn', 'nintendogamecube'],
  Wii: ['nintendowii'],
  'Wii U': ['wiiu', 'nintendowiiu'],
  'Nintendo Switch': ['switch', 'nsw'],
  'Game Boy': ['gameboy', 'gb'],
  'Game Boy Color': ['gameboycolor', 'gbc'],
  'Game Boy Advance': ['gameboyadvance', 'gba'],
  'Nintendo DS': ['nintendods', 'nds', 'ds'],
  'Nintendo 3DS': ['nintendo3ds', '3ds'],
  'Virtual Boy': ['virtualboy'],
  'Sega Master System': ['mastersystem', 'sms', 'segamastersystem'],
  'Sega Mega Drive': ['megadrive', 'genesis', 'megadrivegenesis', 'segamegadrive', 'segagenesis'],
  'Sega Game Gear': ['gamegear', 'segagamegear'],
  'Sega Saturn': ['saturn', 'segasaturn'],
  'Sega Dreamcast': ['dreamcast', 'segadreamcast'],
  'Sega CD': ['segacd', 'megacd'],
  'Sega 32X': ['sega32x', '32x'],
  'SG-1000': ['sg1000'],
  PlayStation: ['playstation', 'ps1', 'psx', 'psone'],
  'PlayStation 2': ['playstation2', 'ps2'],
  'PlayStation 3': ['playstation3', 'ps3'],
  'PlayStation Portable': ['psp', 'playstationportable'],
  'PlayStation Vita': ['psvita', 'playstationvita'],
  Xbox: ['xbox'],
  'Xbox 360': ['xbox360'],
  'Atari 2600': ['atari2600', '2600'],
  'Atari 5200': ['atari5200', '5200'],
  'Atari 7800': ['atari7800', '7800'],
  'Atari Lynx': ['atarilynx', 'lynx'],
  'Atari Jaguar': ['atarijaguar', 'jaguar'],
  'Atari ST': ['atarist'],
  'TurboGrafx-16': ['turbografx16', 'turbografx', 'pcengine', 'tg16'],
  'PC-FX': ['pcfx'],
  'Neo Geo': ['neogeo'],
  'Neo Geo Pocket': ['neogeopocket', 'ngp'],
  'Neo Geo Pocket Color': ['neogeopocketcolor', 'ngpc'],
  '3DO': ['3do'],
  Amiga: ['amiga', 'commodoreamiga'],
  'Commodore 64': ['commodore64', 'c64'],
  MSX: ['msx'],
  MSX2: ['msx2'],
  ColecoVision: ['colecovision'],
  Intellivision: ['intellivision'],
  Vectrex: ['vectrex'],
  WonderSwan: ['wonderswan'],
  'WonderSwan Color': ['wonderswancolor'],
  Arcade: ['arcade', 'mame', 'fbneo', 'fba'],
  ScummVM: ['scummvm'],
  DOS: ['dos', 'msdos'],
};

/** Normalise : sans accents, minuscules, uniquement [a-z0-9]. */
export const normalizeConsole = (s) =>
  String(s || '')
    .normalize('NFD')
    .replace(/[\u0300-\u036f]/g, '')
    .toLowerCase()
    .replace(/[^a-z0-9]/g, '');

// Table de correspondance forme normalisée -> nom canonique.
const LOOKUP = (() => {
  const map = new Map();
  const add = (canonical, alias) => {
    const key = normalizeConsole(alias);
    if (key) map.set(key, canonical);
  };
  for (const [canonical, aliases] of Object.entries(CONSOLES)) {
    add(canonical, canonical);
    aliases.forEach((a) => add(canonical, a));
  }
  // Consoles supplémentaires fournies par l'utilisateur : EXTRA_CONSOLES="X,Y".
  (process.env.EXTRA_CONSOLES || '')
    .split(',')
    .map((s) => s.trim())
    .filter(Boolean)
    .forEach((extra) => add(extra, extra));
  return map;
})();

/**
 * Reconnaît une console à partir d'un libellé de dossier. Teste plusieurs formes
 * (le libellé brut, sans les balises (…)/[…], et le contenu des balises) et
 * renvoie le nom canonique si l'une correspond, sinon '' (ignoré).
 */
export function recognizeConsole(label) {
  const raw = String(label || '');
  if (!raw) return '';
  const candidates = new Set();
  candidates.add(normalizeConsole(raw));
  candidates.add(normalizeConsole(raw.replace(/\[[^\]]*\]/g, ' ').replace(/\([^)]*\)/g, ' ')));
  for (const m of raw.matchAll(/[[(]([^)\]]*)[)\]]/g)) candidates.add(normalizeConsole(m[1]));
  for (const key of candidates) {
    if (key && LOOKUP.has(key)) return LOOKUP.get(key);
  }
  return '';
}
