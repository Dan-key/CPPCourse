const CONTENT_BASE = "../content/";
const PROGRESS_KEY = "track.progress.v1";

let manifest = null;

function loadProgress() {
  try {
    return JSON.parse(localStorage.getItem(PROGRESS_KEY)) || {};
  } catch {
    return {};
  }
}

function saveProgress(progress) {
  localStorage.setItem(PROGRESS_KEY, JSON.stringify(progress));
}

function allModules() {
  return manifest.stages.flatMap((s) => s.modules);
}

function findModule(id) {
  return allModules().find((m) => m.id === id);
}

function findStageOf(id) {
  return manifest.stages.find((s) => s.modules.some((m) => m.id === id));
}

function renderSidebar() {
  const progress = loadProgress();
  const root = document.getElementById("sidebar-stages");
  root.innerHTML = "";

  for (const stage of manifest.stages) {
    const stageEl = document.createElement("div");
    stageEl.className = "stage";

    const title = document.createElement("div");
    title.className = "stage-title";
    title.textContent = stage.title;
    stageEl.appendChild(title);

    for (const mod of stage.modules) {
      const link = document.createElement("div");
      const done = !!progress[mod.id];
      link.className = "module-link " + (mod.status === "ready" ? "ready" : "planned") + (done ? " done" : "");
      link.dataset.id = mod.id;

      const dot = document.createElement("span");
      dot.className = "dot";
      link.appendChild(dot);

      const label = document.createElement("span");
      label.textContent = mod.title;
      link.appendChild(label);

      if (mod.tag) {
        const tag = document.createElement("span");
        tag.className = "tag";
        tag.textContent = mod.tag;
        link.appendChild(tag);
      }

      link.addEventListener("click", () => {
        location.hash = mod.id;
      });

      stageEl.appendChild(link);
    }

    root.appendChild(stageEl);
  }

  updateProgressBar();
}

function updateProgressBar() {
  const progress = loadProgress();
  const mods = allModules();
  const total = mods.length;
  const done = mods.filter((m) => progress[m.id]).length;
  const pct = total ? Math.round((done / total) * 100) : 0;

  document.getElementById("progress-line").textContent = `${done} / ${total} модулей · ${pct}%`;
  document.getElementById("progress-fill").style.width = pct + "%";
}

function markActive(id) {
  document.querySelectorAll(".module-link").forEach((el) => {
    el.classList.toggle("active", el.dataset.id === id);
  });
}

function buildTOC(contentEl) {
  const headings = contentEl.querySelectorAll("h2");
  if (headings.length < 2) return null;

  const toc = document.createElement("div");
  toc.className = "toc";

  const title = document.createElement("div");
  title.className = "toc-title";
  title.textContent = "В этом модуле";
  toc.appendChild(title);

  const ul = document.createElement("ul");
  headings.forEach((h, i) => {
    if (!h.id) h.id = "sec-" + i;
    const li = document.createElement("li");
    const a = document.createElement("a");
    a.href = "#" + h.id;
    a.textContent = h.textContent;
    li.appendChild(a);
    ul.appendChild(li);
  });
  toc.appendChild(ul);
  return toc;
}

function renderToolbar(mod, stage) {
  const bar = document.getElementById("module-toolbar");
  bar.style.display = "flex";

  document.getElementById("crumbs").textContent = `${stage.title} / ${mod.title}`;

  const btn = document.getElementById("mark-done-btn");
  const progress = loadProgress();
  const isDone = !!progress[mod.id];
  btn.textContent = isDone ? "✓ Пройдено" : "Отметить пройденным";
  btn.classList.toggle("is-done", isDone);

  btn.onclick = () => {
    const p = loadProgress();
    p[mod.id] = !p[mod.id];
    saveProgress(p);
    renderSidebar();
    markActive(mod.id);
    renderToolbar(mod, stage);
  };
}

function showPlaceholder(mod, stage) {
  document.getElementById("module-toolbar").style.display = "flex";
  document.getElementById("crumbs").textContent = `${stage.title} / ${mod.title}`;
  document.getElementById("mark-done-btn").style.display = "none";

  const content = document.getElementById("content");
  content.innerHTML = "";
  const ph = document.createElement("div");
  ph.className = "placeholder";
  ph.innerHTML = `
    <h2>${mod.title}</h2>
    <p>Контент этого модуля пока не написан.</p>
    <span class="badge">метка трека: ${mod.tag ?? "—"}</span>
  `;
  content.appendChild(ph);
}

async function loadModule(id) {
  const mod = findModule(id);
  const stage = findStageOf(id);
  if (!mod || !stage) return;

  document.getElementById("mark-done-btn").style.display = "inline-block";

  if (mod.status !== "ready" || !mod.file) {
    showPlaceholder(mod, stage);
    markActive(id);
    return;
  }

  const content = document.getElementById("content");
  content.innerHTML = '<p style="color:var(--text-faint)">Загрузка…</p>';

  const res = await fetch(CONTENT_BASE + mod.file);
  const md = await res.text();
  content.innerHTML = marked.parse(md);

  // syntax highlight
  content.querySelectorAll("pre code").forEach((block) => {
    hljs.highlightElement(block);
  });

  // insert TOC right after the first <h1>
  const toc = buildTOC(content);
  if (toc) {
    const h1 = content.querySelector("h1");
    h1.after(toc);
  }

  renderToolbar(mod, stage);
  markActive(id);
  content.scrollTop = 0;
  document.getElementById("main").scrollTop = 0;
}

function firstReadyId() {
  for (const stage of manifest.stages) {
    for (const mod of stage.modules) {
      if (mod.status === "ready") return mod.id;
    }
  }
  return manifest.stages[0].modules[0].id;
}

async function init() {
  marked.setOptions({ gfm: true, breaks: false });

  manifest = await fetch(CONTENT_BASE + "manifest.json").then((r) => r.json());
  document.getElementById("course-title").textContent = manifest.title;

  renderSidebar();

  const initial = (location.hash || "").replace("#", "") || firstReadyId();
  loadModule(initial);

  window.addEventListener("hashchange", () => {
    loadModule((location.hash || "").replace("#", ""));
  });
}

init();
