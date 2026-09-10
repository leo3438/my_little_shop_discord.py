"use strict";

const api = globalThis.browser || globalThis.chrome;
const DEFAULT_BACKEND = "http://localhost:8000/api/episode";

const input = document.getElementById("backend");
const status = document.getElementById("status");

async function load() {
  const stored = await api.storage.local.get("backendUrl");
  input.value = stored.backendUrl || DEFAULT_BACKEND;
}

document.getElementById("save").addEventListener("click", async () => {
  const url = input.value.trim() || DEFAULT_BACKEND;
  await api.storage.local.set({ backendUrl: url });
  status.textContent = "✓ Enregistré";
  status.style.color = "#69f0ae";
  setTimeout(() => (status.textContent = ""), 2000);
});

load();
