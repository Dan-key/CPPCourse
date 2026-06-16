/* editor.js — панель практики: список упражнений + редактор кода + запуск */

const EX_PROG_KEY = "track.exercises.v1";
const API_BASE    = "/api";

function exLoadProgress() {
  try { return JSON.parse(localStorage.getItem(EX_PROG_KEY)) || {}; }
  catch { return {}; }
}

function exSaveProgress(p) {
  localStorage.setItem(EX_PROG_KEY, JSON.stringify(p));
}

/*
 * Рендерим панель практики.
 * exercises — массив из manifest: [{id, title, type?}, ...]
 * openId    — если задан, сразу открыть это упражнение
 */
async function renderPracticePanel(moduleId, exercises, el, openId) {
  if (!exercises || exercises.length === 0) {
    el.innerHTML = '<p style="color:var(--text-faint)">Упражнений для этого модуля пока нет.</p>';
    return;
  }

  const prog = exLoadProgress();

  const root = document.createElement("div");
  root.className = "practice-root";

  /* ---- Список упражнений (слева) ---- */
  const sidebar = document.createElement("div");
  sidebar.className = "practice-sidebar";

  const listTitle = document.createElement("div");
  listTitle.className = "practice-list-title";
  listTitle.textContent = "Упражнения";
  sidebar.appendChild(listTitle);

  /* ---- Детальная панель (справа) ---- */
  const detail = document.createElement("div");
  detail.className = "practice-detail";
  detail.innerHTML = '<p class="practice-placeholder">Выберите упражнение из списка.</p>';

  const items = [];

  exercises.forEach((ex) => {
    const item = document.createElement("div");
    item.className = "practice-item" + (prog[moduleId + "." + ex.id] ? " practice-done" : "");
    item.dataset.id = ex.id;

    const dot = document.createElement("span");
    dot.className = "practice-dot";
    item.appendChild(dot);

    const label = document.createElement("span");
    label.className = "practice-label";
    label.textContent = ex.title;
    item.appendChild(label);

    if (ex.type === "lkm") {
      const badge = document.createElement("span");
      badge.className = "practice-badge practice-badge-lkm";
      badge.textContent = "QEMU";
      item.appendChild(badge);
    }

    item.addEventListener("click", async () => {
      items.forEach(i => i.classList.remove("practice-active"));
      item.classList.add("practice-active");
      await openExercise(moduleId, ex, detail, prog, item);
    });

    sidebar.appendChild(item);
    items.push(item);
  });

  root.appendChild(sidebar);
  root.appendChild(detail);
  el.innerHTML = "";
  el.appendChild(root);

  /* Открыть нужное упражнение сразу, если задан openId */
  const targetEx = exercises.find(e => e.id === openId) || exercises[0];
  if (targetEx) {
    const targetItem = items.find(i => i.dataset.id === targetEx.id);
    if (targetItem) targetItem.click();
  }
}

async function openExercise(moduleId, ex, detail, prog, itemEl) {
  detail.innerHTML = '<p style="color:var(--text-faint)">Загрузка…</p>';

  /* Загрузить проблему (problem.md) и стартер (starter.c) */
  const base = `../content/exercises/${moduleId}/${ex.id}/`;
  const [problemRes, starterRes] = await Promise.all([
    fetch(base + "problem.md").catch(() => null),
    fetch(base + "starter.c").catch(() => null)
  ]);

  const problemMd  = problemRes && problemRes.ok  ? await problemRes.text()  : null;
  const starterCode = starterRes && starterRes.ok ? await starterRes.text() : "";

  const progKey = moduleId + "." + ex.id;
  const isDone  = !!prog[progKey];

  detail.innerHTML = "";

  /* Описание задачи */
  if (problemMd) {
    const desc = document.createElement("div");
    desc.className = "practice-desc content";
    desc.innerHTML = window.marked ? marked.parse(problemMd) : "<pre>" + problemMd + "</pre>";
    desc.querySelectorAll("pre code").forEach(b => {
      if (window.hljs) hljs.highlightElement(b);
    });
    detail.appendChild(desc);
  }

  /* Редактор */
  const editorWrap = document.createElement("div");
  editorWrap.className = "editor-wrap";

  const editorLabel = document.createElement("div");
  editorLabel.className = "editor-label";
  editorLabel.innerHTML = `<span>solution.c</span>
    ${isDone ? '<span class="editor-done-badge">✓ Пройдено</span>' : ''}`;
  editorWrap.appendChild(editorLabel);

  const textarea = document.createElement("textarea");
  textarea.className = "code-editor";
  textarea.spellcheck = false;
  textarea.value = starterCode;
  textarea.rows  = Math.max(20, starterCode.split("\n").length + 2);
  editorWrap.appendChild(textarea);

  /* Кнопки */
  const btnRow = document.createElement("div");
  btnRow.className = "editor-btnrow";

  const runBtn = document.createElement("button");
  runBtn.className = "editor-run-btn";
  runBtn.textContent = ex.type === "lkm" ? "⚙ Собрать и запустить в QEMU" : "▶ Скомпилировать и запустить";

  const resetBtn = document.createElement("button");
  resetBtn.className = "editor-reset-btn";
  resetBtn.textContent = "↺ Сбросить";

  btnRow.appendChild(runBtn);
  btnRow.appendChild(resetBtn);
  editorWrap.appendChild(btnRow);

  /* Вывод */
  const outputWrap = document.createElement("div");
  outputWrap.className = "editor-output hidden";

  const outputHeader = document.createElement("div");
  outputHeader.className = "output-header";
  outputWrap.appendChild(outputHeader);

  const outputPre = document.createElement("pre");
  outputPre.className = "output-pre";
  outputWrap.appendChild(outputPre);

  editorWrap.appendChild(outputWrap);
  detail.appendChild(editorWrap);

  /* Обработчики */
  resetBtn.addEventListener("click", () => {
    if (confirm("Сбросить код к стартовой заготовке?")) {
      textarea.value = starterCode;
      outputWrap.classList.add("hidden");
    }
  });

  runBtn.addEventListener("click", async () => {
    const code = textarea.value;
    runBtn.disabled = true;
    runBtn.textContent = "…Компиляция…";
    outputWrap.classList.remove("hidden");
    outputHeader.textContent = "";
    outputPre.textContent = "";
    outputPre.className = "output-pre";

    try {
      const endpoint = ex.type === "lkm"
        ? `${API_BASE}/lkm/run`
        : `${API_BASE}/exercises/${moduleId}/${ex.id}/run`;

      const resp = await fetch(endpoint, {
        method: "POST",
        headers: {"Content-Type": "application/json"},
        body: JSON.stringify({ code })
      });

      const result = await resp.json();
      displayResult(result, outputHeader, outputPre, progKey, itemEl, editorLabel, prog);
    } catch (err) {
      outputHeader.textContent = "Ошибка соединения с сервером";
      outputHeader.style.color = "var(--red)";
      outputPre.textContent = err.message;
    }

    runBtn.disabled = false;
    runBtn.textContent = ex.type === "lkm" ? "⚙ Собрать и запустить в QEMU" : "▶ Скомпилировать и запустить";
  });

  /* Tab в textarea */
  textarea.addEventListener("keydown", (e) => {
    if (e.key === "Tab") {
      e.preventDefault();
      const s = textarea.selectionStart, end = textarea.selectionEnd;
      textarea.value = textarea.value.substring(0, s) + "    " + textarea.value.substring(end);
      textarea.selectionStart = textarea.selectionEnd = s + 4;
    }
  });
}

function displayResult(result, header, pre, progKey, itemEl, editorLabel, prog) {
  if (result.error === "compile") {
    header.textContent = "Ошибка компиляции";
    header.style.color = "var(--red)";
    pre.textContent    = result.stderr || result.output || "";
    pre.classList.add("output-fail");
    return;
  }

  if (result.error === "timeout") {
    header.textContent = "Превышено время выполнения (>10 сек)";
    header.style.color = "var(--yellow)";
    pre.textContent    = "";
    pre.classList.add("output-fail");
    return;
  }

  if (result.error === "qemu_unavailable") {
    header.textContent = "QEMU не настроен";
    header.style.color = "var(--yellow)";
    pre.textContent    = result.message || "Запустите scripts/qemu-setup.sh для первоначальной настройки.";
    return;
  }

  const passed = result.passed && result.exit_code === 0;
  header.textContent = passed ? "✓ Все тесты пройдены!" : "✗ Тесты не пройдены";
  header.style.color = passed ? "var(--green)" : "var(--red)";
  pre.textContent    = (result.stdout || "") + (result.stderr ? "\n--- stderr ---\n" + result.stderr : "");
  pre.classList.add(passed ? "output-pass" : "output-fail");

  if (passed) {
    prog[progKey] = true;
    exSaveProgress(prog);
    if (itemEl) itemEl.classList.add("practice-done");
    if (editorLabel) {
      const badge = editorLabel.querySelector(".editor-done-badge");
      if (!badge) {
        const b = document.createElement("span");
        b.className = "editor-done-badge";
        b.textContent = "✓ Пройдено";
        editorLabel.appendChild(b);
      }
    }
  }
}

window.renderPracticePanel = renderPracticePanel;
