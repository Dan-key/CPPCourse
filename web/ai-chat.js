/* ai-chat.js — ИИ-ментор на базе Ollama */

const AI_HISTORY_KEY = "track.ai.history.v1";

/* Сохранить историю одного модуля */
function aiSaveHistory(moduleId, messages) {
  try {
    const all = JSON.parse(localStorage.getItem(AI_HISTORY_KEY) || "{}");
    // Храним не больше 40 сообщений на модуль
    all[moduleId] = messages.slice(-40);
    localStorage.setItem(AI_HISTORY_KEY, JSON.stringify(all));
  } catch {}
}

function aiLoadHistory(moduleId) {
  try {
    const all = JSON.parse(localStorage.getItem(AI_HISTORY_KEY) || "{}");
    return all[moduleId] || [];
  } catch { return []; }
}

/*
 * renderAIChatPanel — рендерим панель ментора в el.
 * moduleId:    id модуля ("f1", "k1", ...)
 * moduleTitle: заголовок модуля для системного промпта
 */
async function renderAIChatPanel(moduleId, moduleTitle, el) {
  el.innerHTML = "";

  /* Проверяем доступность Ollama */
  let aiStatus = null;
  try {
    const r = await fetch("/api/ai/status");
    aiStatus = await r.json();
  } catch {}

  const root = document.createElement("div");
  root.className = "ai-root";

  /* ---- Шапка ---- */
  const header = document.createElement("div");
  header.className = "ai-header";

  const titleEl = document.createElement("div");
  titleEl.className = "ai-title";
  titleEl.innerHTML = `<span class="ai-icon">✦</span> ИИ-ментор`;
  header.appendChild(titleEl);

  /* Выбор модели */
  const modelRow = document.createElement("div");
  modelRow.className = "ai-model-row";

  const modelLabel = document.createElement("label");
  modelLabel.className = "ai-model-label";
  modelLabel.textContent = "Модель:";

  const modelSelect = document.createElement("select");
  modelSelect.className = "ai-model-select";

  /* Заполним модели */
  const defaultModels = ["qwen2.5-coder:7b", "llama3.1:8b", "qwen2.5-coder:1.5b-base"];
  if (aiStatus && aiStatus.models) {
    aiStatus.models.forEach(m => {
      const opt = document.createElement("option");
      opt.value = m;
      opt.textContent = m;
      if (m === "qwen2.5-coder:7b") opt.selected = true;
      modelSelect.appendChild(opt);
    });
  } else {
    defaultModels.forEach(m => {
      const opt = document.createElement("option");
      opt.value = m; opt.textContent = m;
      modelSelect.appendChild(opt);
    });
  }

  const clearBtn = document.createElement("button");
  clearBtn.className = "ai-clear-btn";
  clearBtn.textContent = "Очистить чат";

  modelRow.appendChild(modelLabel);
  modelRow.appendChild(modelSelect);
  modelRow.appendChild(clearBtn);
  header.appendChild(modelRow);

  /* Статус Ollama */
  if (aiStatus && !aiStatus.available) {
    const warn = document.createElement("div");
    warn.className = "ai-unavailable";
    warn.innerHTML = `⚠ ${aiStatus.reason}`;
    header.appendChild(warn);
  }

  root.appendChild(header);

  /* ---- История сообщений ---- */
  const messagesEl = document.createElement("div");
  messagesEl.className = "ai-messages";
  root.appendChild(messagesEl);

  /* ---- Поле ввода ---- */
  const inputRow = document.createElement("div");
  inputRow.className = "ai-input-row";

  const textarea = document.createElement("textarea");
  textarea.className = "ai-input";
  textarea.placeholder = "Задай вопрос по материалу модуля…";
  textarea.rows = 2;

  const sendBtn = document.createElement("button");
  sendBtn.className = "ai-send-btn";
  sendBtn.textContent = "Отправить";
  sendBtn.disabled = !aiStatus?.available;

  inputRow.appendChild(textarea);
  inputRow.appendChild(sendBtn);
  root.appendChild(inputRow);

  const hint = document.createElement("div");
  hint.className = "ai-hint";
  hint.textContent = "Enter — новая строка, Ctrl+Enter — отправить";
  root.appendChild(hint);

  el.appendChild(root);

  /* ---- Состояние ---- */
  let messages = aiLoadHistory(moduleId);   // [{role, content}]
  let isStreaming = false;

  /* Восстановить историю из localStorage */
  messages.forEach(m => appendBubble(messagesEl, m.role, m.content, false));
  if (messages.length) scrollToBottom(messagesEl);

  /* Очистить историю */
  clearBtn.addEventListener("click", () => {
    if (!confirm("Очистить историю этого модуля?")) return;
    messages = [];
    aiSaveHistory(moduleId, []);
    messagesEl.innerHTML = "";
  });

  /* Отправить сообщение */
  async function send() {
    const text = textarea.value.trim();
    if (!text || isStreaming) return;

    textarea.value = "";
    isStreaming = true;
    sendBtn.disabled = true;
    sendBtn.textContent = "…";

    /* Показать сообщение пользователя */
    appendBubble(messagesEl, "user", text, false);
    messages.push({ role: "user", content: text });
    aiSaveHistory(moduleId, messages);
    scrollToBottom(messagesEl);

    /* Placeholder для ответа ассистента */
    const { bubble, contentEl } = appendBubble(messagesEl, "assistant", "", true);
    scrollToBottom(messagesEl);

    /* SSE-стриминг */
    let fullContent = "";
    try {
      const resp = await fetch("/api/ai/chat", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          module_id: moduleId,
          module_title: moduleTitle,
          model: modelSelect.value,
          messages    // история без системного промпта
        })
      });

      if (!resp.ok) {
        setError(contentEl, `HTTP ${resp.status}`);
      } else {
        const reader = resp.body.getReader();
        const decoder = new TextDecoder();
        let buf = "";

        while (true) {
          const { done, value } = await reader.read();
          if (done) break;

          buf += decoder.decode(value, { stream: true });
          const lines = buf.split("\n");
          buf = lines.pop();   // неполная строка — обратно в буфер

          for (const line of lines) {
            if (!line.startsWith("data: ")) continue;
            const payload = line.slice(6).trim();
            if (payload === "[DONE]") { reader.cancel(); break; }
            try {
              const ev = JSON.parse(payload);
              if (ev.error) { setError(contentEl, ev.error); break; }
              if (ev.done)  { break; }
              if (ev.chunk) {
                fullContent += ev.chunk;
                renderMarkdownStream(contentEl, fullContent);
                scrollToBottom(messagesEl);
              }
            } catch {}
          }
        }
      }
    } catch (err) {
      setError(contentEl, err.message);
    }

    /* Сохранить ответ */
    if (fullContent) {
      messages.push({ role: "assistant", content: fullContent });
      aiSaveHistory(moduleId, messages);
    }

    bubble.classList.remove("ai-bubble-streaming");
    isStreaming = false;
    sendBtn.disabled = false;
    sendBtn.textContent = "Отправить";
    textarea.focus();
  }

  sendBtn.addEventListener("click", send);

  textarea.addEventListener("keydown", (e) => {
    if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) {
      e.preventDefault();
      send();
    }
  });
}

/* ---- Вспомогательные функции ---- */

function appendBubble(container, role, content, streaming) {
  const wrap = document.createElement("div");
  wrap.className = "ai-bubble-wrap ai-bubble-" + role;
  if (streaming) wrap.classList.add("ai-bubble-streaming");

  const bubble = document.createElement("div");
  bubble.className = "ai-bubble";

  const contentEl = document.createElement("div");
  contentEl.className = "ai-bubble-content";

  if (content) renderMarkdownStream(contentEl, content);
  else if (streaming) contentEl.innerHTML = '<span class="ai-typing">…</span>';

  bubble.appendChild(contentEl);
  wrap.appendChild(bubble);
  container.appendChild(wrap);
  return { bubble: wrap, contentEl };
}

function setError(contentEl, msg) {
  contentEl.innerHTML = `<span class="ai-error">Ошибка: ${msg}</span>`;
}

function scrollToBottom(el) {
  el.scrollTop = el.scrollHeight;
}

/*
 * Рендерим markdown-текст во время стриминга.
 * Используем marked.js (уже подключён на странице).
 */
function renderMarkdownStream(el, text) {
  if (window.marked) {
    el.innerHTML = marked.parse(text);
    /* Подсветка кода — только в законченных блоках */
    el.querySelectorAll("pre code:not(.hljs)").forEach(b => {
      if (window.hljs) hljs.highlightElement(b);
    });
  } else {
    /* Фолбэк: просто текст с переносами */
    el.textContent = text;
  }
}

window.renderAIChatPanel = renderAIChatPanel;
