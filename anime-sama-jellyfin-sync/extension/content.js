/*
 * content.js
 * -----------
 * S'exécute sur Anime-Sama et Afterdark. Rôle : extraire les métadonnées du
 * contenu couramment sélectionné et les transmettre au service worker, qui les
 * associera au flux vidéo capturé sur le réseau.
 *
 * Le payload de métadonnées contient toujours :
 *   { media_type, anime_title, season, episode_number, language,
 *     year, channel_name, page_url }
 *   media_type ∈ { "series", "movie", "manga", "live_tv" }
 *
 * Les sélecteurs DOM d'Afterdark sont heuristiques (structure susceptible de
 * changer selon le miroir) : ils s'appuient d'abord sur l'URL, puis sur des
 * repli DOM génériques. Adaptez `AFTERDARK_SELECTORS` si besoin.
 */

(() => {
  "use strict";

  // ---------------------------------------------------------------- helpers
  function titleFromSlug(slug) {
    return decodeURIComponent(slug || "")
      .replace(/[-_]+/g, " ")
      .replace(/\s+/g, " ")
      .trim()
      .replace(/\b\w/g, (c) => c.toUpperCase());
  }

  function firstText(selectors) {
    for (const sel of selectors) {
      const el = document.querySelector(sel);
      if (el) {
        const val =
          el.getAttribute?.("content") ||
          el.getAttribute?.("value") ||
          el.textContent;
        if (val && val.trim()) return val.trim();
      }
    }
    return null;
  }

  function extractYear(text) {
    const m = (text || "").match(/\b(19|20)\d{2}\b/);
    return m ? parseInt(m[0], 10) : null;
  }

  function cleanTitle(text) {
    return (text || "")
      .replace(/\s*[-|–—].*$/, "")
      .replace(/\(\s*(19|20)\d{2}\s*\)/, "")
      .trim();
  }

  // ============================================================ ANIME-SAMA
  function parseAnimeSama() {
    const parts = location.pathname.split("/").filter(Boolean);
    const idx = parts.indexOf("catalogue");
    let slug = null;
    let season = 1;
    let language = "VOSTFR";

    if (idx !== -1 && parts[idx + 1]) slug = parts[idx + 1];

    for (const part of parts) {
      const p = part.toLowerCase();
      const seasonMatch = p.match(/^saison(\d+)/);
      if (seasonMatch) season = parseInt(seasonMatch[1], 10);
      if (["vf", "vf1", "vf2"].includes(p)) language = "VF";
      else if (["vostfr", "va", "vostfr1"].includes(p)) language = "VOSTFR";
    }

    const title =
      firstText(["#titreOeuvre", "h1#titreOeuvre", "h1"]) ||
      titleFromSlug(slug) ||
      cleanTitle(document.title);

    // Numéro d'épisode depuis le sélecteur #selectEpisodes.
    let episode = 1;
    const select = document.querySelector("#selectEpisodes");
    if (select) {
      const opt = select.options[select.selectedIndex];
      const m = opt && opt.textContent.match(/(\d+)/);
      episode = m ? parseInt(m[1], 10) : select.selectedIndex + 1;
    }

    return {
      media_type: "series",
      anime_title: title,
      season,
      episode_number: episode,
      language,
      year: null,
      channel_name: null,
      page_url: location.href,
    };
  }

  function attachAnimeSamaListeners(push) {
    const select = document.querySelector("#selectEpisodes");
    if (select && !select.dataset.asJfBound) {
      select.dataset.asJfBound = "1";
      select.addEventListener("change", () => push("episode-change"));
    }
    document.querySelectorAll("#selectLecteurs, .bouton-lecteur").forEach((el) => {
      if (!el.dataset.asJfBound) {
        el.dataset.asJfBound = "1";
        el.addEventListener("change", () => push("player-change"));
        el.addEventListener("click", () => push("player-click"));
      }
    });
  }

  // ============================================================== AFTERDARK
  const AFTERDARK_SELECTORS = {
    title: [
      'meta[property="og:title"]',
      "h1.title",
      "h1.entry-title",
      ".media-title",
      "h1",
    ],
    year: [".year", ".release-year", ".date", 'meta[property="video:release_date"]'],
    channel: [".channel-name.active", ".channel.active", ".live-channel.active", ".now-playing"],
    seasonSelect: ["#season", "select.season", '[name="season"]'],
    episodeSelect: ["#episode", "select.episode", '[name="episode"]'],
  };

  /** Détermine le type de média sur Afterdark à partir de l'URL puis du DOM. */
  function afterdarkMediaType() {
    const path = location.pathname.toLowerCase();
    const hay = path + " " + location.search.toLowerCase();
    if (/\b(live|direct|chaine|channel|tv-live|livetv)\b/.test(hay)) return "live_tv";
    if (/\b(movie|movies|film|films)\b/.test(hay)) return "movie";
    if (/\b(serie|series|show|shows|episode|saison|season|manga|anime)\b/.test(hay))
      return "series";
    // Repli DOM : présence d'un sélecteur de saison => série.
    if (document.querySelector(AFTERDARK_SELECTORS.seasonSelect.join(","))) return "series";
    if (document.querySelector(AFTERDARK_SELECTORS.channel.join(","))) return "live_tv";
    return "movie";
  }

  /** Extrait un numéro (saison/épisode) depuis un <select> ou l'URL. */
  function numberFrom(selectors, urlPatterns, fallback) {
    const sel = document.querySelector(selectors.join(","));
    if (sel && sel.tagName === "SELECT") {
      const opt = sel.options[sel.selectedIndex];
      const m = opt && opt.textContent.match(/(\d+)/);
      if (m) return parseInt(m[1], 10);
      return sel.selectedIndex + 1;
    }
    const hay = location.pathname + location.search + location.hash;
    for (const re of urlPatterns) {
      const m = hay.match(re);
      if (m) return parseInt(m[1], 10);
    }
    return fallback;
  }

  function afterdarkLanguage() {
    const hay = (location.pathname + location.search).toLowerCase();
    if (/\b(vf|french|fr|truefrench|vff)\b/.test(hay)) return "VF";
    if (/\b(vostfr|vost|subfr)\b/.test(hay)) return "VOSTFR";
    if (/\b(vo|en|eng|english)\b/.test(hay)) return "VO";
    const domLang = firstText(['[data-lang].active', ".lang.active", ".audio.active"]);
    if (domLang) return domLang.toUpperCase();
    return "VO";
  }

  function parseAfterdark() {
    const mediaType = afterdarkMediaType();
    const rawTitle = firstText(AFTERDARK_SELECTORS.title) || cleanTitle(document.title);
    const language = afterdarkLanguage();

    if (mediaType === "live_tv") {
      const channel =
        firstText(AFTERDARK_SELECTORS.channel) ||
        cleanTitle(rawTitle) ||
        "Chaine";
      return {
        media_type: "live_tv",
        anime_title: channel,
        season: 0,
        episode_number: 0,
        language,
        year: null,
        channel_name: channel,
        page_url: location.href,
      };
    }

    if (mediaType === "movie") {
      const yearText =
        firstText(AFTERDARK_SELECTORS.year) || rawTitle || document.title;
      return {
        media_type: "movie",
        anime_title: cleanTitle(rawTitle),
        season: 0,
        episode_number: 0,
        language,
        year: extractYear(yearText) || extractYear(document.body?.innerText?.slice(0, 2000)),
        channel_name: null,
        page_url: location.href,
      };
    }

    // series / manga
    const season = numberFrom(
      AFTERDARK_SELECTORS.seasonSelect,
      [/[sS](?:aison|eason)?[ _-]?(\d{1,3})/, /season[=/](\d{1,3})/],
      1
    );
    const episode = numberFrom(
      AFTERDARK_SELECTORS.episodeSelect,
      [/[eE](?:p|pisode)?[ _-]?(\d{1,4})/, /episode[=/](\d{1,4})/],
      1
    );
    return {
      media_type: "series",
      anime_title: cleanTitle(rawTitle),
      season,
      episode_number: episode,
      language,
      year: null,
      channel_name: null,
      page_url: location.href,
    };
  }

  function attachAfterdarkListeners(push) {
    const selectors = [
      ...AFTERDARK_SELECTORS.seasonSelect,
      ...AFTERDARK_SELECTORS.episodeSelect,
      ".channel-name",
      ".live-channel",
      ".play-button",
      "button.play",
    ];
    document.querySelectorAll(selectors.join(",")).forEach((el) => {
      if (!el.dataset.asJfBound) {
        el.dataset.asJfBound = "1";
        el.addEventListener("change", () => push("afterdark-change"));
        el.addEventListener("click", () => push("afterdark-click"));
      }
    });
  }

  // ================================================================ DISPATCH
  const host = location.hostname.toLowerCase();
  const isAnimeSama = host.endsWith("anime-sama.fr");
  const isAfterdark = host.endsWith(".mom"); // afd926.mom et futurs miroirs

  if (!isAnimeSama && !isAfterdark) return;

  const collect = isAnimeSama ? parseAnimeSama : parseAfterdark;
  const attach = isAnimeSama ? attachAnimeSamaListeners : attachAfterdarkListeners;

  let lastSent = "";
  function push(reason) {
    let meta;
    try {
      meta = collect();
    } catch (e) {
      return;
    }
    if (!meta || !meta.anime_title) return;
    const key = JSON.stringify(meta);
    if (key === lastSent) return;
    lastSent = key;
    chrome.runtime.sendMessage(
      { type: "EPISODE_METADATA", reason, metadata: meta },
      () => void chrome.runtime.lastError
    );
  }

  const observer = new MutationObserver(() => attach(push));
  observer.observe(document.documentElement, { childList: true, subtree: true });

  attach(push);
  push("initial");

  let lastUrl = location.href;
  setInterval(() => {
    if (location.href !== lastUrl) {
      lastUrl = location.href;
      attach(push);
      push("url-change");
    }
  }, 1000);
})();
