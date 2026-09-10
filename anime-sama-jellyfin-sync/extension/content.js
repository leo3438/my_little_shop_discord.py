/*
 * content.js
 * -----------
 * S'exécute sur les pages anime-sama. Rôle : extraire les métadonnées de
 * l'épisode couramment sélectionné (titre, saison, numéro d'épisode, langue)
 * et les transmettre au service worker, qui les associera au flux vidéo
 * capturé sur le réseau.
 */

(() => {
  "use strict";

  /** Nettoie un slug d'URL en un titre lisible : "one-piece" -> "One Piece". */
  function titleFromSlug(slug) {
    return slug
      .replace(/[-_]+/g, " ")
      .replace(/\s+/g, " ")
      .trim()
      .replace(/\b\w/g, (c) => c.toUpperCase());
  }

  /** Analyse l'URL : /catalogue/<slug>/saison<N>/<langue>/ */
  function parseUrl() {
    const parts = location.pathname.split("/").filter(Boolean);
    const idx = parts.indexOf("catalogue");
    const info = { slug: null, season: 1, language: "VOSTFR" };

    if (idx !== -1 && parts[idx + 1]) {
      info.slug = decodeURIComponent(parts[idx + 1]);
    }

    for (const part of parts) {
      const p = part.toLowerCase();
      const seasonMatch = p.match(/^saison(\d+)/);
      if (seasonMatch) {
        info.season = parseInt(seasonMatch[1], 10);
      } else if (p === "film" || p.startsWith("film")) {
        info.season = 1;
      }
      if (p === "vf" || p === "vf1" || p === "vf2") {
        info.language = "VF";
      } else if (p === "vostfr" || p === "va" || p === "vostfr1") {
        info.language = "VOSTFR";
      }
    }
    return info;
  }

  /** Récupère le titre affiché de l'anime, avec plusieurs solutions de repli. */
  function getAnimeTitle(urlInfo) {
    const selectors = ["#titreOeuvre", "h1#titreOeuvre", "h1", "#analytics-title"];
    for (const sel of selectors) {
      const el = document.querySelector(sel);
      if (el && el.textContent && el.textContent.trim()) {
        return el.textContent.trim();
      }
    }
    if (urlInfo.slug) {
      return titleFromSlug(urlInfo.slug);
    }
    return (document.title || "Anime").replace(/\s*[-|].*$/, "").trim();
  }

  /** Numéro d'épisode à partir du sélecteur #selectEpisodes. */
  function getEpisodeNumber() {
    const select = document.querySelector("#selectEpisodes");
    if (select) {
      // Le texte "Episode 12" est le plus fiable ; sinon on prend l'index + 1.
      const opt = select.options[select.selectedIndex];
      if (opt) {
        const m = opt.textContent.match(/(\d+)/);
        if (m) return parseInt(m[1], 10);
      }
      return select.selectedIndex + 1;
    }
    // Repli : chercher un numéro dans l'URL (ex: ?ep=5 ou #episode-5).
    const fromHash = (location.hash || "").match(/(\d+)/);
    if (fromHash) return parseInt(fromHash[1], 10);
    return 1;
  }

  /** Assemble les métadonnées courantes. */
  function collectMetadata() {
    const urlInfo = parseUrl();
    return {
      anime_title: getAnimeTitle(urlInfo),
      season: urlInfo.season,
      episode_number: getEpisodeNumber(),
      language: urlInfo.language,
      page_url: location.href,
    };
  }

  let lastSent = "";

  /** Envoie les métadonnées au service worker (dédoublonnées). */
  function pushMetadata(reason) {
    const meta = collectMetadata();
    const key = JSON.stringify(meta);
    if (key === lastSent) return;
    lastSent = key;

    chrome.runtime.sendMessage(
      { type: "EPISODE_METADATA", reason, metadata: meta },
      () => void chrome.runtime.lastError // avale les erreurs si le worker dort
    );
  }

  /** Branche les écouteurs sur le sélecteur d'épisode dès qu'il apparaît. */
  function attachListeners() {
    const select = document.querySelector("#selectEpisodes");
    if (select && !select.dataset.asJfBound) {
      select.dataset.asJfBound = "1";
      select.addEventListener("change", () => pushMetadata("episode-change"));
    }
    // Les liens de lecteurs (Sibnet, etc.) déclenchent aussi une capture.
    document.querySelectorAll("#selectLecteurs, .bouton-lecteur").forEach((el) => {
      if (!el.dataset.asJfBound) {
        el.dataset.asJfBound = "1";
        el.addEventListener("change", () => pushMetadata("player-change"));
        el.addEventListener("click", () => pushMetadata("player-click"));
      }
    });
  }

  // Le contenu d'anime-sama est injecté dynamiquement : on observe le DOM.
  const observer = new MutationObserver(() => attachListeners());
  observer.observe(document.documentElement, { childList: true, subtree: true });

  // Premier passage + envoi initial.
  attachListeners();
  pushMetadata("initial");

  // Certaines navigations restent sur la même page (SPA-like) : on suit l'URL.
  let lastUrl = location.href;
  setInterval(() => {
    if (location.href !== lastUrl) {
      lastUrl = location.href;
      attachListeners();
      pushMetadata("url-change");
    }
  }, 1000);
})();
