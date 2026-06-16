const CONTENT_BASE = "../content/";
const PROGRESS_KEY = "track.progress.v1";

let manifest = null;

/* ---- Progress ---- */
function loadProgress() {
  try { return JSON.parse(localStorage.getItem(PROGRESS_KEY)) || {}; }
  catch { return {}; }
}

function saveProgress(p) {
  localStorage.setItem(PROGRESS_KEY, JSON.stringify(p));
}

/* ---- Manifest helpers ---- */
function allModules() {
  return manifest.stages.flatMap((s) => s.modules);
}

function findModule(id) {
  return allModules().find((m) => m.id === id);
}

function findStageOf(id) {
  return manifest.stages.find((s) => s.modules.some((m) => m.id === id));
}

/* ---- Sidebar ---- */
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
      link.className =
        "module-link " +
        (mod.status === "ready" ? "ready" : "planned") +
        (done ? " done" : "");
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

      link.addEventListener("click", () => { location.hash = mod.id; });
      stageEl.appendChild(link);
    }

    root.appendChild(stageEl);
  }

  updateProgressBar();
}

function updateProgressBar() {
  const progress = loadProgress();
  const mods  = allModules();
  const total = mods.length;
  const done  = mods.filter((m) => progress[m.id]).length;
  const pct   = total ? Math.round((done / total) * 100) : 0;
  document.getElementById("progress-line").textContent = `${done} / ${total} модулей · ${pct}%`;
  document.getElementById("progress-fill").style.width = pct + "%";
}

function markActive(id) {
  document.querySelectorAll(".module-link").forEach((el) => {
    el.classList.toggle("active", el.dataset.id === id);
  });
}

/* ---- TOC ---- */
function buildTOC(contentEl) {
  const headings = contentEl.querySelectorAll("h2");
  if (headings.length < 2) return null;

  const toc = document.createElement("div");
  toc.className = "toc";

  const tocTitle = document.createElement("div");
  tocTitle.className = "toc-title";
  tocTitle.textContent = "В этом модуле";
  toc.appendChild(tocTitle);

  const ul = document.createElement("ul");
  headings.forEach((h, i) => {
    if (!h.id) h.id = "sec-" + i;
    const li = document.createElement("li");
    const a  = document.createElement("a");
    a.href = "#" + h.id;
    a.textContent = h.textContent;
    li.appendChild(a);
    ul.appendChild(li);
  });
  toc.appendChild(ul);
  return toc;
}

/* ---- Toolbar ---- */
function renderToolbar(mod, stage, activeTab) {
  const bar = document.getElementById("module-toolbar");
  bar.style.display = "flex";

  document.getElementById("crumbs").textContent = `${stage.title} / ${mod.title}`;

  /* Tab buttons */
  const tabsEl = document.getElementById("module-tabs");
  tabsEl.innerHTML = "";

  const tabs = [
    { id: "lecture",  label: "Лекция",   available: !!mod.file },
    { id: "quiz",     label: "Квиз",     available: !!mod.quiz },
    { id: "practice", label: "Практика", available: !!(mod.exercises && mod.exercises.length > 0) },
    { id: "mentor",   label: "✦ Ментор", available: true }
  ].filter(t => t.available);

  tabs.forEach(t => {
    const btn = document.createElement("button");
    btn.className = "tab-btn" + (t.id === activeTab ? " active" : "");
    btn.dataset.tab = t.id;
    btn.textContent = t.label;
    btn.addEventListener("click", () => switchTab(t.id, mod, stage));
    tabsEl.appendChild(btn);
  });

  /* Mark done button */
  const btn = document.getElementById("mark-done-btn");
  btn.style.display = "inline-block";
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
    renderToolbar(mod, stage, activeTab);
  };
}

/* ---- Tab switching ---- */
let currentMod   = null;
let currentStage = null;
let currentTab   = "lecture";

function switchTab(tabId, mod, stage) {
  currentTab = tabId;

  /* Update tab buttons */
  document.querySelectorAll(".tab-btn").forEach(b => {
    b.classList.toggle("active", b.dataset.tab === tabId);
  });

  /* Show/hide panels */
  const panels = ["lecture", "quiz", "practice", "mentor"];
  panels.forEach(p => {
    const el = document.getElementById("panel-" + p);
    if (el) el.classList.toggle("panel-hidden", p !== tabId);
  });

  /* Lazy-load panel content */
  if (tabId === "quiz" && mod.quiz) {
    const quizPanel = document.getElementById("panel-quiz");
    if (!quizPanel.dataset.loaded) {
      quizPanel.dataset.loaded = "1";
      window.renderQuizPanel(mod.id, quizPanel);
    }
  }

  if (tabId === "practice" && mod.exercises && mod.exercises.length > 0) {
    const practicePanel = document.getElementById("panel-practice");
    if (!practicePanel.dataset.loaded) {
      practicePanel.dataset.loaded = "1";
      window.renderPracticePanel(mod.id, mod.exercises, practicePanel, null);
    }
  }

  if (tabId === "mentor") {
    const mentorPanel = document.getElementById("panel-mentor");
    if (!mentorPanel.dataset.loaded) {
      mentorPanel.dataset.loaded = "1";
      window.renderAIChatPanel(mod.id, mod.title, mentorPanel);
    }
  }
}

/* Called from quiz.js when user clicks "go to exercise" */
window.switchToTab = function(tabId, modId, exerciseId) {
  const mod   = findModule(modId);
  const stage = findStageOf(modId);
  if (!mod || !stage) return;

  const practicePanel = document.getElementById("panel-practice");
  if (practicePanel) {
    practicePanel.dataset.loaded = ""; /* force reload to open specific exercise */
  }

  switchTab(tabId, mod, stage);

  /* After render, open the exercise */
  if (tabId === "practice" && mod.exercises) {
    const practicePanel2 = document.getElementById("panel-practice");
    practicePanel2.dataset.loaded = "1";
    window.renderPracticePanel(mod.id, mod.exercises, practicePanel2, exerciseId);
  }
};

/* ---- Placeholder for unwritten modules ---- */
function showPlaceholder(mod, stage) {
  document.getElementById("module-toolbar").style.display = "flex";
  document.getElementById("crumbs").textContent = `${stage.title} / ${mod.title}`;
  document.getElementById("module-tabs").innerHTML = "";
  document.getElementById("mark-done-btn").style.display = "none";

  const panels = ["lecture", "quiz", "practice", "mentor"];
  panels.forEach(p => {
    const el = document.getElementById("panel-" + p);
    if (el) { el.innerHTML = ""; el.classList.add("panel-hidden"); }
  });

  const lecture = document.getElementById("panel-lecture");
  if (lecture) {
    lecture.classList.remove("panel-hidden");
    lecture.innerHTML = `
      <div class="placeholder">
        <h2>${mod.title}</h2>
        <p>Контент этого модуля пока не написан.</p>
        ${mod.tag ? `<span class="badge">метка трека: ${mod.tag}</span>` : ""}
      </div>`;
  }
}

/* ---- Main load ---- */
async function loadModule(id, forceTab) {
  const mod   = findModule(id);
  const stage = findStageOf(id);
  if (!mod || !stage) return;

  currentMod   = mod;
  currentStage = stage;

  /* Reset panels */
  ["lecture", "quiz", "practice", "mentor"].forEach(p => {
    const el = document.getElementById("panel-" + p);
    if (el) {
      el.innerHTML = "";
      el.removeAttribute("data-loaded");
      el.classList.add("panel-hidden");
    }
  });

  if (mod.status !== "ready" || (!mod.file && !mod.quiz && !(mod.exercises && mod.exercises.length))) {
    showPlaceholder(mod, stage);
    markActive(id);
    return;
  }

  /* Decide which tab is first */
  const initialTab = forceTab || (mod.file ? "lecture" : mod.quiz ? "quiz" : "practice");
  currentTab = initialTab;

  renderToolbar(mod, stage, initialTab);

  /* Load lecture */
  if (mod.file) {
    const lecturePanel = document.getElementById("panel-lecture");
    const res = await fetch(CONTENT_BASE + mod.file);
    const md  = await res.text();
    const inner = document.createElement("div");
    inner.className = "content";
    inner.innerHTML = marked.parse(md);
    inner.querySelectorAll("pre code").forEach(b => hljs.highlightElement(b));
    const toc = buildTOC(inner);
    if (toc) { const h1 = inner.querySelector("h1"); if (h1) h1.after(toc); }
    lecturePanel.appendChild(inner);
  }

  /* Activate initial tab */
  switchTab(initialTab, mod, stage);
  markActive(id);
  document.getElementById("main").scrollTop = 0;
}

/* ---- Init ---- */
function firstReadyId() {
  for (const stage of manifest.stages)
    for (const mod of stage.modules)
      if (mod.status === "ready") return mod.id;
  return manifest.stages[0].modules[0].id;
}

async function init() {
  marked.setOptions({ gfm: true, breaks: false });
  manifest = await fetch(CONTENT_BASE + "manifest.json").then(r => r.json());
  document.getElementById("course-title").textContent = manifest.title;

  renderSidebar();

  const initial = (location.hash || "").replace("#", "") || firstReadyId();
  loadModule(initial);

  window.addEventListener("hashchange", () => {
    loadModule((location.hash || "").replace("#", ""));
  });
}

init();
