/* quiz.js — интерактивный компонент квиза */

const QUIZ_BASE   = "../content/quizzes/";
const QUIZ_PROG_KEY = "track.quiz.v1";

function quizLoadProgress() {
  try { return JSON.parse(localStorage.getItem(QUIZ_PROG_KEY)) || {}; }
  catch { return {}; }
}

function quizSaveProgress(p) {
  localStorage.setItem(QUIZ_PROG_KEY, JSON.stringify(p));
}

/* Загрузить данные квиза и вернуть Promise<quizData|null> */
async function loadQuizData(moduleId) {
  try {
    const r = await fetch(QUIZ_BASE + moduleId + ".json");
    if (!r.ok) return null;
    return await r.json();
  } catch { return null; }
}

/* Рендерим всю панель квиза в контейнер el */
async function renderQuizPanel(moduleId, el) {
  el.innerHTML = '<p style="color:var(--text-faint)">Загрузка квиза…</p>';
  const quiz = await loadQuizData(moduleId);
  if (!quiz) {
    el.innerHTML = '<p style="color:var(--text-faint)">Квиз для этого модуля пока не написан.</p>';
    return;
  }

  const prog = quizLoadProgress();
  const key  = (qId) => moduleId + "." + qId;

  /* ---- Верхний уровень: выбор секции ---- */
  const root = document.createElement("div");
  root.className = "quiz-root";

  const header = document.createElement("div");
  header.className = "quiz-header";
  header.innerHTML = `<h2 class="quiz-title">${quiz.title}</h2>`;
  root.appendChild(header);

  /* Счётчик прогресса */
  const allQ  = quiz.sections.flatMap(s => s.questions);
  const doneQ = allQ.filter(q => prog[key(q.id)]);
  const pct   = allQ.length ? Math.round(doneQ.length / allQ.length * 100) : 0;
  const counter = document.createElement("div");
  counter.className = "quiz-counter";
  counter.textContent = `${doneQ.length} / ${allQ.length} вопросов · ${pct}%`;
  header.appendChild(counter);

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

  let firstTab = null;

  quiz.sections.forEach((section, si) => {
    const tabId   = "qlvl-" + si;
    const panelId = "qpnl-" + si;

    /* Кнопка уровня */
    const btn = document.createElement("button");
    btn.className = "quiz-level-btn" + (si === 0 ? " active" : "");
    btn.dataset.panel = panelId;
    const doneInSection = section.questions.filter(q => prog[key(q.id)]).length;
    btn.innerHTML = `<span style="color:${levelColors[section.level] || 'var(--text)'}">${section.level}</span>
                     <span class="qlvl-count">${doneInSection}/${section.questions.length}</span>`;
    btn.addEventListener("click", () => {
      tabs.querySelectorAll(".quiz-level-btn").forEach(b => b.classList.remove("active"));
      panels.querySelectorAll(".quiz-panel").forEach(p => p.classList.remove("active"));
      btn.classList.add("active");
      document.getElementById(panelId).classList.add("active");
    });
    tabs.appendChild(btn);
    if (si === 0) firstTab = btn;

    /* Панель вопросов */
    const panel = document.createElement("div");
    panel.className = "quiz-panel" + (si === 0 ? " active" : "");
    panel.id = panelId;
    panel.innerHTML = `<p class="quiz-section-desc">${section.description}</p>`;

    /* Рендер каждого вопроса */
    section.questions.forEach((q, qi) => {
      const qEl = buildQuestion(q, key(q.id), prog, moduleId, (qid) => {
        /* callback: вопрос отвечен */
        const p2 = quizLoadProgress();
        p2[key(qid)] = true;
        quizSaveProgress(p2);
        /* Обновить счётчики */
        refreshQuizCounters(quiz, root, moduleId);
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
  wrap.id = "qq-" + q.id;

  const qText = document.createElement("div");
  qText.className = "quiz-q-text";
  /* Рендерим код в тексте вопроса */
  qText.innerHTML = formatQuizText(q.text);
  wrap.appendChild(qText);

  if (isDone) {
    const doneLabel = document.createElement("div");
    doneLabel.className = "quiz-done-label";
    doneLabel.textContent = "✓ зачтено";
    wrap.appendChild(doneLabel);

    if (q.type === "mcq") {
      const opts = buildMcqOptions(q, true, null, onDone);
      wrap.appendChild(opts);
    }
    return wrap;
  }

  if (q.type === "mcq") {
    const opts = buildMcqOptions(q, false, progKey, onDone);
    wrap.appendChild(opts);
  } else if (q.type === "self_grade") {
    const sg = buildSelfGrade(q, progKey, onDone);
    wrap.appendChild(sg);
  } else if (q.type === "exercise") {
    const exLink = document.createElement("div");
    exLink.className = "quiz-exercise-link";
    exLink.innerHTML = `<span>Задание: откройте вкладку <strong>Практика</strong> и найдите упражнение </span>
                        <button class="quiz-goto-ex" data-ex="${q.exercise_id}" data-mod="${moduleId}">
                          ${q.exercise_id}
                        </button>`;
    exLink.querySelector(".quiz-goto-ex").addEventListener("click", (e) => {
      /* Переключить на вкладку Практика и открыть нужное упражнение */
      const mod  = e.target.dataset.mod;
      const exId = e.target.dataset.ex;
      if (window.switchToTab) window.switchToTab("practice", mod, exId);
    });
    wrap.appendChild(exLink);
  }

  return wrap;
}

function buildMcqOptions(q, alreadyDone, progKey, onDone) {
  const container = document.createElement("div");
  container.className = "mcq-options";

  q.options.forEach(opt => {
    const btn = document.createElement("button");
    btn.className = "mcq-opt";
    btn.dataset.id = opt.id;
    btn.textContent = opt.text;

    if (alreadyDone) {
      if (opt.id === q.correct) btn.classList.add("mcq-correct");
      btn.disabled = true;
    } else {
      btn.addEventListener("click", () => {
        if (container.dataset.answered) return;
        container.dataset.answered = "1";

        container.querySelectorAll(".mcq-opt").forEach(b => {
          b.disabled = true;
          if (b.dataset.id === q.correct) b.classList.add("mcq-correct");
          if (b.dataset.id === btn.dataset.id && btn.dataset.id !== q.correct)
            b.classList.add("mcq-wrong");
        });

        /* Объяснение */
        if (q.explanation) {
          const exp = document.createElement("div");
          exp.className = "mcq-explanation";
          exp.innerHTML = "<strong>Разбор:</strong> " + formatQuizText(q.explanation);
          container.appendChild(exp);
        }

        if (btn.dataset.id === q.correct) {
          /* Анимация + зачёт */
          setTimeout(() => {
            const wrap = document.getElementById("qq-" + q.id);
            if (wrap) wrap.classList.add("quiz-q-done");
            const dl = document.createElement("div");
            dl.className = "quiz-done-label";
            dl.textContent = "✓ зачтено";
            if (wrap) wrap.appendChild(dl);
          }, 600);
          onDone(q.id);
        }
      });
    }

    container.appendChild(btn);
  });

  return container;
}

function buildSelfGrade(q, progKey, onDone) {
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
  refDiv.className = "sg-reference hidden";
  refDiv.innerHTML = "<strong>Эталон:</strong> " + formatQuizText(q.reference);

  const gradeDiv = document.createElement("div");
  gradeDiv.className = "sg-grade hidden";

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
    const wrap = document.getElementById("qq-" + q.id);
    if (wrap) wrap.classList.add("quiz-q-done");
    const dl = document.createElement("div");
    dl.className = "quiz-done-label";
    dl.textContent = "✓ зачтено";
    if (wrap) wrap.appendChild(dl);
    onDone(q.id);
  });

  noBtn.addEventListener("click", () => {
    gradeDiv.innerHTML = '<span class="sg-graded-no">Отложено — вернись позже</span>';
  });

  container.appendChild(showBtn);
  container.appendChild(refDiv);
  container.appendChild(gradeDiv);
  return container;
}

function refreshQuizCounters(quiz, root, moduleId) {
  const prog = quizLoadProgress();
  const key  = (qId) => moduleId + "." + qId;
  const allQ = quiz.sections.flatMap(s => s.questions);
  const doneQ = allQ.filter(q => prog[key(q.id)]);
  const pct  = allQ.length ? Math.round(doneQ.length / allQ.length * 100) : 0;

  const counter = root.querySelector(".quiz-counter");
  if (counter) counter.textContent = `${doneQ.length} / ${allQ.length} вопросов · ${pct}%`;

  quiz.sections.forEach((section, si) => {
    const btn = root.querySelectorAll(".quiz-level-btn")[si];
    if (!btn) return;
    const doneInSection = section.questions.filter(q => prog[key(q.id)]).length;
    const countEl = btn.querySelector(".qlvl-count");
    if (countEl) countEl.textContent = `${doneInSection}/${section.questions.length}`;
  });
}

/* Простой форматтер: `код` → <code>, **жирный** → <strong> */
function formatQuizText(text) {
  if (!text) return "";
  return text
    .replace(/`([^`]+)`/g, "<code>$1</code>")
    .replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>")
    .replace(/\n/g, "<br>");
}

window.renderQuizPanel = renderQuizPanel;
