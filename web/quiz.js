/* quiz.js — интерактивный компонент квиза.
 *
 * Схема данных (content/quizzes/<id>.json):
 *   {
 *     "module": "c1",
 *     "title":  "...",
 *     "levels": {
 *       "БАЗА":      { "desc": "...", "questions": [ ...mcq... ] },
 *       "МЕХАНИЗМЫ": { "desc": "...", "questions": [ ...self_grade... ] },
 *       "ЭКСПЕРТ":   { "desc": "...", "questions": [ ...self_grade... ] },
 *       "ЗАДАНИЯ":   { "desc": "...", "questions": [ ...exercise... ] }
 *     }
 *   }
 * Типы вопросов:
 *   mcq        — { type, text, options:[строки], answer:<индекс>, explanation }
 *   self_grade — { type, text, hint, reference }
 *   exercise   — { type, exercise_id, title }
 * У вопросов нет собственных id — мы синтезируем стабильный id "s<si>q<qi>".
 */

const QUIZ_BASE   = "../content/quizzes/";
const QUIZ_PROG_KEY = "track.quiz.v1";

function quizLoadProgress() {
  try { return JSON.parse(localStorage.getItem(QUIZ_PROG_KEY)) || {}; }
  catch { return {}; }
}

function quizSaveProgress(p) {
  localStorage.setItem(QUIZ_PROG_KEY, JSON.stringify(p));
}

/* Удалить весь прогресс одного модуля (ключи вида "moduleId.xxx"). */
function quizResetModule(moduleId) {
  const prog = quizLoadProgress();
  const prefix = moduleId + ".";
  for (const k of Object.keys(prog)) {
    if (k.startsWith(prefix)) delete prog[k];
  }
  quizSaveProgress(prog);
}

/* Загрузить данные квиза и вернуть Promise<quizData|null> */
async function loadQuizData(moduleId) {
  try {
    const r = await fetch(QUIZ_BASE + moduleId + ".json");
    if (!r.ok) return null;
    return await r.json();
  } catch { return null; }
}

/* Нормализуем quiz.levels (объект) в массив секций со стабильными id вопросов. */
function quizSections(quiz) {
  const levels = quiz.levels || {};
  return Object.keys(levels).map((level, si) => {
    const body = levels[level] || {};
    const questions = (body.questions || []).map((q, qi) => ({
      ...q,
      _id: "s" + si + "q" + qi   // стабильный синтетический id
    }));
    return { level, desc: body.desc || "", questions };
  });
}

/* Рендерим всю панель квиза в контейнер el */
async function renderQuizPanel(moduleId, el) {
  el.innerHTML = '<p style="color:var(--text-faint)">Загрузка квиза…</p>';
  const quiz = await loadQuizData(moduleId);
  if (!quiz || !quiz.levels) {
    el.innerHTML = '<p style="color:var(--text-faint)">Квиз для этого модуля пока не написан.</p>';
    return;
  }

  const sections = quizSections(quiz);
  const prog = quizLoadProgress();
  const key  = (qId) => moduleId + "." + qId;

  const root = document.createElement("div");
  root.className = "quiz-root";

  const header = document.createElement("div");
  header.className = "quiz-header";

  /* Левая часть: заголовок + счётчик */
  const headerMain = document.createElement("div");
  headerMain.className = "quiz-header-main";
  headerMain.innerHTML = `<h2 class="quiz-title">${quiz.title || moduleId}</h2>`;
  header.appendChild(headerMain);

  /* Счётчик прогресса */
  const allQ  = sections.flatMap(s => s.questions);
  const doneQ = allQ.filter(q => prog[key(q._id)]);
  const pct   = allQ.length ? Math.round(doneQ.length / allQ.length * 100) : 0;
  const counter = document.createElement("div");
  counter.className = "quiz-counter";
  counter.textContent = `${doneQ.length} / ${allQ.length} вопросов · ${pct}%`;
  headerMain.appendChild(counter);

  /* Кнопка сброса прогресса по модулю */
  const resetBtn = document.createElement("button");
  resetBtn.className = "quiz-reset-btn";
  resetBtn.textContent = "↺ Сбросить квиз";
  resetBtn.title = "Очистить прогресс этого квиза и пройти заново";
  resetBtn.addEventListener("click", () => {
    if (!confirm("Сбросить весь прогресс этого квиза и пройти заново?")) return;
    quizResetModule(moduleId);
    renderQuizPanel(moduleId, el);   // перерисовать с чистого листа
  });
  header.appendChild(resetBtn);
  root.appendChild(header);          /* ← без этой строки шапка с кнопкой не попадала в DOM */

  /* Табы по уровням */
  const tabs    = document.createElement("div");
  tabs.className = "quiz-level-tabs";
  const panels  = document.createElement("div");
  panels.className = "quiz-panels";

  const levelColors = {
    "БАЗА":       "var(--green)",
    "МЕХАНИЗМЫ":  "var(--accent)",
    "ЭКСПЕРТ":    "var(--red)",
    "ЗАДАНИЯ":    "var(--yellow)"
  };

  sections.forEach((section, si) => {
    const panelId = "qpnl-" + si;

    /* Кнопка уровня */
    const btn = document.createElement("button");
    btn.className = "quiz-level-btn" + (si === 0 ? " active" : "");
    btn.dataset.panel = panelId;
    const doneInSection = section.questions.filter(q => prog[key(q._id)]).length;
    btn.innerHTML = `<span style="color:${levelColors[section.level] || 'var(--text)'}">${section.level}</span>
                     <span class="qlvl-count">${doneInSection}/${section.questions.length}</span>`;
    btn.addEventListener("click", () => {
      tabs.querySelectorAll(".quiz-level-btn").forEach(b => b.classList.remove("active"));
      panels.querySelectorAll(".quiz-panel").forEach(p => p.classList.remove("active"));
      btn.classList.add("active");
      document.getElementById(panelId).classList.add("active");
    });
    tabs.appendChild(btn);

    /* Панель вопросов */
    const panel = document.createElement("div");
    panel.className = "quiz-panel" + (si === 0 ? " active" : "");
    panel.id = panelId;
    panel.innerHTML = `<p class="quiz-section-desc">${section.desc}</p>`;

    section.questions.forEach((q) => {
      const qEl = buildQuestion(q, key(q._id), prog, moduleId, () => {
        const p2 = quizLoadProgress();
        p2[key(q._id)] = true;
        quizSaveProgress(p2);
        refreshQuizCounters(sections, root, moduleId);
      });
      panel.appendChild(qEl);
    });

    panels.appendChild(panel);
  });

  root.appendChild(tabs);
  root.appendChild(panels);
  el.innerHTML = "";
  el.appendChild(root);
}

function buildQuestion(q, progKey, prog, moduleId, onDone) {
  const isDone = !!prog[progKey];
  const wrap = document.createElement("div");
  wrap.className = "quiz-q" + (isDone ? " quiz-q-done" : "");
  wrap.id = "qq-" + q._id;

  if (q.text) {
    const qText = document.createElement("div");
    qText.className = "quiz-q-text";
    qText.innerHTML = formatQuizText(q.text);
    wrap.appendChild(qText);
  }

  if (isDone) {
    const doneLabel = document.createElement("div");
    doneLabel.className = "quiz-done-label";
    doneLabel.textContent = "✓ зачтено";
    wrap.appendChild(doneLabel);

    if (q.type === "mcq") {
      wrap.appendChild(buildMcqOptions(q, true, onDone));
    } else if (q.type === "self_grade") {
      wrap.appendChild(buildSelfGrade(q, onDone, true));
    } else if (q.type === "exercise") {
      wrap.appendChild(buildExerciseLink(q, moduleId));
    }
    return wrap;
  }

  if (q.type === "mcq") {
    wrap.appendChild(buildMcqOptions(q, false, onDone));
  } else if (q.type === "self_grade") {
    wrap.appendChild(buildSelfGrade(q, onDone, false));
  } else if (q.type === "exercise") {
    wrap.appendChild(buildExerciseLink(q, moduleId));
  }

  return wrap;
}

function buildExerciseLink(q, moduleId) {
  const exLink = document.createElement("div");
  exLink.className = "quiz-exercise-link";
  const title = q.title ? ` — ${q.title}` : "";
  exLink.innerHTML = `<span>Задание: откройте вкладку <strong>Практика</strong>, упражнение </span>
                      <button class="quiz-goto-ex" data-ex="${q.exercise_id}" data-mod="${moduleId}">
                        ${q.exercise_id}${title}
                      </button>`;
  exLink.querySelector(".quiz-goto-ex").addEventListener("click", (e) => {
    const mod  = e.currentTarget.dataset.mod;
    const exId = e.currentTarget.dataset.ex;
    if (window.switchToTab) window.switchToTab("practice", mod, exId);
  });
  return exLink;
}

function buildMcqOptions(q, alreadyDone, onDone) {
  const container = document.createElement("div");
  container.className = "mcq-options";
  const correctIdx = q.answer;   // индекс правильного варианта в q.options

  q.options.forEach((optText, idx) => {
    const btn = document.createElement("button");
    btn.className = "mcq-opt";
    btn.dataset.idx = String(idx);
    btn.textContent = optText;

    if (alreadyDone) {
      if (idx === correctIdx) btn.classList.add("mcq-correct");
      btn.disabled = true;
    } else {
      btn.addEventListener("click", () => {
        if (container.dataset.answered) return;
        container.dataset.answered = "1";

        container.querySelectorAll(".mcq-opt").forEach(b => {
          b.disabled = true;
          const bi = Number(b.dataset.idx);
          if (bi === correctIdx) b.classList.add("mcq-correct");
          if (bi === idx && idx !== correctIdx) b.classList.add("mcq-wrong");
        });

        if (q.explanation) {
          const exp = document.createElement("div");
          exp.className = "mcq-explanation";
          exp.innerHTML = "<strong>Разбор:</strong> " + formatQuizText(q.explanation);
          container.appendChild(exp);
        }

        if (idx === correctIdx) {
          setTimeout(() => {
            const wrap = document.getElementById("qq-" + q._id);
            if (wrap && !wrap.querySelector(".quiz-done-label")) {
              wrap.classList.add("quiz-q-done");
              const dl = document.createElement("div");
              dl.className = "quiz-done-label";
              dl.textContent = "✓ зачтено";
              wrap.appendChild(dl);
            }
          }, 600);
          onDone();
        }
      });
    }

    container.appendChild(btn);
  });

  return container;
}

function buildSelfGrade(q, onDone, alreadyDone) {
  const container = document.createElement("div");
  container.className = "sg-container";

  if (q.hint) {
    const hint = document.createElement("div");
    hint.className = "sg-hint";
    hint.innerHTML = "💡 <em>" + formatQuizText(q.hint) + "</em>";
    container.appendChild(hint);
  }

  const showBtn = document.createElement("button");
  showBtn.className = "sg-show-btn";
  showBtn.textContent = "Показать эталонный ответ";

  const refDiv = document.createElement("div");
  refDiv.className = "sg-reference" + (alreadyDone ? "" : " hidden");
  refDiv.innerHTML = "<strong>Эталон:</strong> " + formatQuizText(q.reference);

  const gradeDiv = document.createElement("div");
  gradeDiv.className = "sg-grade" + (alreadyDone ? "" : " hidden");

  if (alreadyDone) {
    showBtn.style.display = "none";
    gradeDiv.innerHTML = '<span class="sg-graded-yes">✓ Зачтено</span>';
    container.appendChild(showBtn);
    container.appendChild(refDiv);
    container.appendChild(gradeDiv);
    return container;
  }

  const yesBtn = document.createElement("button");
  yesBtn.className = "sg-grade-btn sg-yes";
  yesBtn.textContent = "✓ Зачёл";

  const noBtn = document.createElement("button");
  noBtn.className = "sg-grade-btn sg-no";
  noBtn.textContent = "✗ Не зачёл — повторю";

  gradeDiv.appendChild(yesBtn);
  gradeDiv.appendChild(noBtn);

  showBtn.addEventListener("click", () => {
    showBtn.style.display = "none";
    refDiv.classList.remove("hidden");
    gradeDiv.classList.remove("hidden");
  });

  yesBtn.addEventListener("click", () => {
    gradeDiv.innerHTML = '<span class="sg-graded-yes">✓ Зачтено</span>';
    const wrap = document.getElementById("qq-" + q._id);
    if (wrap && !wrap.querySelector(".quiz-done-label")) {
      wrap.classList.add("quiz-q-done");
      const dl = document.createElement("div");
      dl.className = "quiz-done-label";
      dl.textContent = "✓ зачтено";
      wrap.appendChild(dl);
    }
    onDone();
  });

  noBtn.addEventListener("click", () => {
    gradeDiv.innerHTML = '<span class="sg-graded-no">Отложено — вернись позже</span>';
  });

  container.appendChild(showBtn);
  container.appendChild(refDiv);
  container.appendChild(gradeDiv);
  return container;
}

function refreshQuizCounters(sections, root, moduleId) {
  const prog = quizLoadProgress();
  const key  = (qId) => moduleId + "." + qId;
  const allQ = sections.flatMap(s => s.questions);
  const doneQ = allQ.filter(q => prog[key(q._id)]);
  const pct  = allQ.length ? Math.round(doneQ.length / allQ.length * 100) : 0;

  const counter = root.querySelector(".quiz-counter");
  if (counter) counter.textContent = `${doneQ.length} / ${allQ.length} вопросов · ${pct}%`;

  sections.forEach((section, si) => {
    const btn = root.querySelectorAll(".quiz-level-btn")[si];
    if (!btn) return;
    const doneInSection = section.questions.filter(q => prog[key(q._id)]).length;
    const countEl = btn.querySelector(".qlvl-count");
    if (countEl) countEl.textContent = `${doneInSection}/${section.questions.length}`;
  });
}

/* Простой форматтер: `код` → <code>, **жирный** → <strong>, перевод строки → <br> */
function formatQuizText(text) {
  if (!text) return "";
  return text
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/`([^`]+)`/g, "<code>$1</code>")
    .replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>")
    .replace(/\n/g, "<br>");
}

window.renderQuizPanel = renderQuizPanel;
