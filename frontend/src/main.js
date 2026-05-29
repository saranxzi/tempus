/* ═══════════════════════════════════════════════════════════════════════════
   Tempus — Frontend Application (main.js)
   All view rendering, state management, and Go backend communication.
   ═══════════════════════════════════════════════════════════════════════════ */

// ── Wails runtime bindings ───────────────────────────────────────────────
const Go = window.go?.main?.App || {};

// ── State ────────────────────────────────────────────────────────────────
let currentView = 'dashboard';

// Pomodoro state
let pomoTimer = null;
let pomoRunning = false;
let pomoSeconds = 25 * 60;
let pomoTotalSeconds = 25 * 60;
let pomoPhase = 'focus'; // 'focus' | 'short-break' | 'long-break'
let pomoSessionCount = 0;
let pomoSettings = { focusDuration: 25, shortBreakDuration: 5, longBreakDuration: 15, longBreakInterval: 4 };
let pomoSettingsOpen = false;

// Stopwatch state
let swTimer = null;
let swRunning = false;
let swSeconds = 0;
let swTaskId = null;
let swTaskName = '';
let swPinned = false;
let swMiniVisible = false;

// ── Utility helpers ──────────────────────────────────────────────────────

function fmtDuration(seconds) {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  if (h > 0) return `${h}h ${String(m).padStart(2,'0')}m`;
  if (m > 0) return `${m}m ${String(s).padStart(2,'0')}s`;
  return `${s}s`;
}

function fmtTime(seconds) {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  return `${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`;
}

function fmtDate(isoStr) {
  if (!isoStr) return '—';
  const d = new Date(isoStr);
  return d.toLocaleDateString('en-US', { month: 'short', day: 'numeric', year: 'numeric' });
}

function fmtDateTime(isoStr) {
  if (!isoStr) return '—';
  const d = new Date(isoStr);
  return d.toLocaleString('en-US', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' });
}

function fmtTimeOnly(isoStr) {
  if (!isoStr) return '—';
  const d = new Date(isoStr);
  return d.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit', hour12: true });
}

const COLORS = [
  '#6366f1','#8b5cf6','#a855f7','#d946ef','#ec4899',
  '#f43f5e','#ef4444','#f97316','#eab308','#22c55e',
  '#14b8a6','#06b6d4','#3b82f6','#2563eb','#7c3aed',
  '#64748b','#f59e0b','#10b981',
];

function $(sel) { return document.querySelector(sel); }
function $$(sel) { return document.querySelectorAll(sel); }
function el(tag, attrs = {}, children = []) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'className') e.className = v;
    else if (k === 'style' && typeof v === 'object') Object.assign(e.style, v);
    else if (k.startsWith('on')) e.addEventListener(k.slice(2).toLowerCase(), v);
    else if (k === 'innerHTML') e.innerHTML = v;
    else if (k === 'textContent') e.textContent = v;
    else e.setAttribute(k, v);
  }
  for (const c of children) {
    if (typeof c === 'string') e.appendChild(document.createTextNode(c));
    else if (c) e.appendChild(c);
  }
  return e;
}

// ── Title-bar window controls ────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {
  $('#btn-minimize')?.addEventListener('click', () => window.runtime?.WindowMinimise());
  $('#btn-maximize')?.addEventListener('click', () => window.runtime?.WindowToggleMaximise());
  $('#btn-close')?.addEventListener('click', () => window.runtime?.Quit());

  // Navigation
  $$('.nav-item').forEach(item => {
    item.addEventListener('click', () => {
      const view = item.dataset.view;
      if (view) navigateTo(view);
    });
  });

  // Mini stopwatch controls
  $('#mini-pause')?.addEventListener('click', toggleStopwatch);
  $('#mini-stop')?.addEventListener('click', stopAndSaveStopwatch);
  $('#mini-pin')?.addEventListener('click', togglePin);

  // Load pomodoro settings then render
  loadPomoSettings().then(() => navigateTo('dashboard'));
});

function navigateTo(view) {
  currentView = view;
  $$('.nav-item').forEach(n => n.classList.toggle('active', n.dataset.view === view));
  renderView(view);
}

// ══════════════════════════════════════════════════════════════════════════
// VIEW ROUTER
// ══════════════════════════════════════════════════════════════════════════

async function renderView(view) {
  const main = $('#main-content');
  main.innerHTML = '';
  main.scrollTop = 0;

  switch (view) {
    case 'dashboard':    await renderDashboard(main); break;
    case 'pomodoro':     renderPomodoro(main); break;
    case 'stopwatch':    renderStopwatch(main); break;
    case 'time-tracking': await renderTimeTracking(main); break;
    case 'analytics':    await renderAnalytics(main); break;
    case 'tasks':        await renderTasks(main); break;
    case 'settings':     await renderSettings(main); break;
  }
}

// ══════════════════════════════════════════════════════════════════════════
// DASHBOARD VIEW
// ══════════════════════════════════════════════════════════════════════════

async function renderDashboard(container) {
  container.innerHTML = `
    <div class="view-header animate-in">
      <h1>Dashboard</h1>
      <p>Your time tracking overview at a glance</p>
    </div>
    <div class="stats-grid stagger" id="dash-stats"></div>
    <div class="grid-2 stagger" style="animation-delay:200ms">
      <div class="card" id="dash-chart-card">
        <div class="card-header">
          <span class="card-title">This Week</span>
        </div>
        <div class="chart-container"><canvas id="dash-bar-chart"></canvas></div>
      </div>
      <div class="card" id="dash-recent-card">
        <div class="card-header">
          <span class="card-title">Recent Activity</span>
          <button class="btn btn-sm btn-ghost" onclick="navigateTo('time-tracking')">View all →</button>
        </div>
        <div id="dash-recent-list"></div>
      </div>
    </div>
  `;

  try {
    const data = await Go.GetDashboardData();
    // Stats
    const statsEl = $('#dash-stats');
    statsEl.innerHTML = `
      <div class="stat-card" style="--stat-accent:var(--accent);--stat-bg:var(--accent-dim)">
        <div class="stat-icon">⏱</div>
        <div class="stat-value">${fmtDuration(data.todaySeconds || 0)}</div>
        <div class="stat-label">Today</div>
      </div>
      <div class="stat-card" style="--stat-accent:var(--info);--stat-bg:var(--info-dim)">
        <div class="stat-icon">📅</div>
        <div class="stat-value">${fmtDuration(data.weekSeconds || 0)}</div>
        <div class="stat-label">This Week</div>
      </div>
      <div class="stat-card" style="--stat-accent:var(--success);--stat-bg:var(--success-dim)">
        <div class="stat-icon">📊</div>
        <div class="stat-value">${fmtDuration(data.totalSeconds || 0)}</div>
        <div class="stat-label">All Time</div>
      </div>
      <div class="stat-card" style="--stat-accent:var(--warning);--stat-bg:var(--warning-dim)">
        <div class="stat-icon">🗂️</div>
        <div class="stat-value">${data.taskCount || 0}</div>
        <div class="stat-label">Active Tasks</div>
      </div>
    `;

    // Bar chart
    drawWeekBarChart($('#dash-bar-chart'), data.weekDaySummary || []);

    // Recent activity
    const recentEl = $('#dash-recent-list');
    if (!data.recentEntries || data.recentEntries.length === 0) {
      recentEl.innerHTML = `<div class="empty-state"><div class="empty-icon">📭</div><p>No activity yet. Start tracking!</p></div>`;
    } else {
      data.recentEntries.slice(0, 6).forEach(entry => {
        const item = el('div', { className: 'activity-item', onClick: () => navigateTo('time-tracking') }, [
          el('div', { className: 'activity-dot', style: { background: entry.taskColor } }),
          el('div', { className: 'activity-info' }, [
            el('div', { className: 'activity-name', textContent: entry.taskName }),
            el('div', { className: 'activity-meta', textContent: fmtDateTime(entry.startTime) }),
          ]),
          el('div', { className: 'activity-duration', textContent: fmtDuration(entry.duration) }),
        ]);
        recentEl.appendChild(item);
      });
    }
  } catch (err) {
    container.innerHTML += `<div class="card"><p class="text-danger">Failed to load dashboard: ${err}</p></div>`;
  }
}

// ── Simple bar chart (Canvas) ────────────────────────────────────────────

function drawWeekBarChart(canvas, daySummary) {
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.parentElement.getBoundingClientRect();
  canvas.width = rect.width * dpr;
  canvas.height = rect.height * dpr;
  ctx.scale(dpr, dpr);
  const W = rect.width;
  const H = rect.height;

  const dayLabels = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
  const now = new Date();
  const weekday = now.getDay() === 0 ? 7 : now.getDay();
  const monday = new Date(now);
  monday.setDate(now.getDate() - (weekday - 1));

  // Build per-day totals
  const values = new Array(7).fill(0);
  for (const s of daySummary) {
    const d = new Date(s.date + 'T00:00:00');
    const idx = (d.getDay() === 0 ? 6 : d.getDay() - 1);
    values[idx] = s.totalSeconds / 3600; // hours
  }
  const maxVal = Math.max(...values, 1);

  const barW = (W - 60) / 7;
  const padX = 40;
  const padY = 20;
  const chartH = H - padY * 2;

  // Draw grid lines
  ctx.strokeStyle = 'hsla(230, 30%, 40%, 0.15)';
  ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const y = padY + (chartH / 4) * i;
    ctx.beginPath();
    ctx.moveTo(padX, y);
    ctx.lineTo(W, y);
    ctx.stroke();
  }

  // Draw bars
  const accentColor = getComputedStyle(document.documentElement).getPropertyValue('--accent').trim() || '#6366f1';
  for (let i = 0; i < 7; i++) {
    const x = padX + i * barW + barW * 0.15;
    const bw = barW * 0.7;
    const bh = (values[i] / maxVal) * chartH;
    const y = padY + chartH - bh;

    // Bar
    ctx.fillStyle = i === weekday - 1 ? accentColor : 'hsla(245, 80%, 65%, 0.35)';
    roundRect(ctx, x, y, bw, bh, 4);
    ctx.fill();

    // Label
    ctx.fillStyle = 'hsl(220, 12%, 45%)';
    ctx.font = '11px Inter, sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText(dayLabels[i], x + bw / 2, H - 4);

    // Value
    if (values[i] > 0) {
      ctx.fillStyle = 'hsl(220, 20%, 93%)';
      ctx.font = '10px Inter, sans-serif';
      ctx.fillText(values[i].toFixed(1) + 'h', x + bw / 2, y - 6);
    }
  }

  // Y-axis labels
  ctx.fillStyle = 'hsl(220, 12%, 45%)';
  ctx.font = '10px Inter, sans-serif';
  ctx.textAlign = 'right';
  for (let i = 0; i <= 4; i++) {
    const y = padY + (chartH / 4) * i;
    const v = ((4 - i) / 4 * maxVal).toFixed(1);
    ctx.fillText(v + 'h', padX - 6, y + 3);
  }
}

function roundRect(ctx, x, y, w, h, r) {
  if (h <= 0) return;
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.lineTo(x + w - r, y);
  ctx.quadraticCurveTo(x + w, y, x + w, y + r);
  ctx.lineTo(x + w, y + h);
  ctx.lineTo(x, y + h);
  ctx.lineTo(x, y + r);
  ctx.quadraticCurveTo(x, y, x + r, y);
  ctx.closePath();
}

// ══════════════════════════════════════════════════════════════════════════
// POMODORO VIEW
// ══════════════════════════════════════════════════════════════════════════

function renderPomodoro(container) {
  const circumference = 2 * Math.PI * 115;
  const progress = pomoTotalSeconds > 0 ? (1 - pomoSeconds / pomoTotalSeconds) : 0;
  const dashOffset = circumference * (1 - progress);

  const isBreak = pomoPhase !== 'focus';
  const breakClass = isBreak ? ' break-mode' : '';

  container.innerHTML = `
    <div class="view-header animate-in">
      <h1>Pomodoro Timer</h1>
      <p>Focus deeply, then take restorative breaks</p>
    </div>
    <div class="card animate-in" style="max-width:500px;margin:0 auto">
      <div class="timer-display">
        <div class="timer-phase${breakClass}" id="pomo-phase">${phaseLabel()}</div>
        <div class="timer-ring-container">
          <svg class="timer-ring" viewBox="0 0 240 240">
            <circle class="timer-ring-bg" cx="120" cy="120" r="115"/>
            <circle class="timer-ring-progress${breakClass}" cx="120" cy="120" r="115"
              stroke-dasharray="${circumference}"
              stroke-dashoffset="${dashOffset}"
              id="pomo-ring"/>
          </svg>
          <div class="timer-time" id="pomo-time">${fmtTime(pomoSeconds)}</div>
        </div>
        <div class="timer-controls">
          <button class="timer-btn" onclick="resetPomodoro()" data-tooltip="Reset">↺</button>
          <button class="timer-btn primary" onclick="togglePomodoro()" id="pomo-play-btn">
            ${pomoRunning ? '⏸' : '▶'}
          </button>
          <button class="timer-btn" onclick="skipPomodoro()" data-tooltip="Skip">⏭</button>
        </div>
        <div style="margin-top:var(--space-md);display:flex;align-items:center;justify-content:center;gap:var(--space-sm)">
          <span class="badge badge-accent">Session ${pomoSessionCount + 1}</span>
          <span class="badge ${isBreak ? 'badge-success' : 'badge-accent'}">${pomoPhase.replace('-', ' ')}</span>
        </div>
      </div>

      <!-- Settings dropdown -->
      <div class="pomo-settings-dropdown">
        <div class="pomo-settings-toggle ${pomoSettingsOpen ? 'open' : ''}" onclick="togglePomoSettings()">
          ⚙️ Timer Settings <span class="chevron">▼</span>
        </div>
        <div id="pomo-settings-body" class="${pomoSettingsOpen ? '' : 'hidden'}">
          <div class="pomo-settings-body">
            <div class="form-group">
              <label class="form-label">Focus (min)</label>
              <input type="number" class="form-input" id="pomo-focus" value="${pomoSettings.focusDuration}" min="1" max="120" onchange="updatePomoSetting('focusDuration', this.value)">
            </div>
            <div class="form-group">
              <label class="form-label">Short Break</label>
              <input type="number" class="form-input" id="pomo-short" value="${pomoSettings.shortBreakDuration}" min="1" max="30" onchange="updatePomoSetting('shortBreakDuration', this.value)">
            </div>
            <div class="form-group">
              <label class="form-label">Long Break</label>
              <input type="number" class="form-input" id="pomo-long" value="${pomoSettings.longBreakDuration}" min="1" max="60" onchange="updatePomoSetting('longBreakDuration', this.value)">
            </div>
            <div class="form-group">
              <label class="form-label">Long Break After</label>
              <input type="number" class="form-input" id="pomo-interval" value="${pomoSettings.longBreakInterval}" min="1" max="10" onchange="updatePomoSetting('longBreakInterval', this.value)">
            </div>
          </div>
        </div>
      </div>
    </div>
  `;
}

function phaseLabel() {
  switch (pomoPhase) {
    case 'focus': return 'FOCUS';
    case 'short-break': return 'SHORT BREAK';
    case 'long-break': return 'LONG BREAK';
  }
}

async function loadPomoSettings() {
  try {
    const s = await Go.GetPomodoroSettings();
    if (s) pomoSettings = s;
    if (!pomoRunning) {
      pomoTotalSeconds = pomoSettings.focusDuration * 60;
      pomoSeconds = pomoTotalSeconds;
    }
  } catch (e) { console.error('Failed to load pomo settings', e); }
}

function togglePomoSettings() {
  pomoSettingsOpen = !pomoSettingsOpen;
  const body = $('#pomo-settings-body');
  const toggle = document.querySelector('.pomo-settings-toggle');
  if (body) body.classList.toggle('hidden', !pomoSettingsOpen);
  if (toggle) toggle.classList.toggle('open', pomoSettingsOpen);
}

async function updatePomoSetting(key, value) {
  const v = parseInt(value);
  if (isNaN(v) || v < 1) return;
  pomoSettings[key] = v;
  try {
    await Go.SavePomodoroSettings(pomoSettings);
  } catch (e) { console.error(e); }
  if (!pomoRunning) {
    if (pomoPhase === 'focus') { pomoTotalSeconds = pomoSettings.focusDuration * 60; }
    else if (pomoPhase === 'short-break') { pomoTotalSeconds = pomoSettings.shortBreakDuration * 60; }
    else { pomoTotalSeconds = pomoSettings.longBreakDuration * 60; }
    pomoSeconds = pomoTotalSeconds;
    updatePomoDisplay();
  }
}

function togglePomodoro() {
  if (pomoRunning) {
    clearInterval(pomoTimer);
    pomoRunning = false;
  } else {
    pomoRunning = true;
    pomoTimer = setInterval(tickPomodoro, 1000);
  }
  updatePomoDisplay();
  const btn = $('#pomo-play-btn');
  if (btn) btn.textContent = pomoRunning ? '⏸' : '▶';
}

function tickPomodoro() {
  if (pomoSeconds > 0) {
    pomoSeconds--;
    updatePomoDisplay();
  } else {
    clearInterval(pomoTimer);
    pomoRunning = false;
    // Auto-advance phase
    if (pomoPhase === 'focus') {
      pomoSessionCount++;
      if (pomoSessionCount % pomoSettings.longBreakInterval === 0) {
        pomoPhase = 'long-break';
        pomoTotalSeconds = pomoSettings.longBreakDuration * 60;
      } else {
        pomoPhase = 'short-break';
        pomoTotalSeconds = pomoSettings.shortBreakDuration * 60;
      }
    } else {
      pomoPhase = 'focus';
      pomoTotalSeconds = pomoSettings.focusDuration * 60;
    }
    pomoSeconds = pomoTotalSeconds;
    if (currentView === 'pomodoro') renderPomodoro($('#main-content'));
  }
}

function updatePomoDisplay() {
  const timeEl = $('#pomo-time');
  const ringEl = $('#pomo-ring');
  if (timeEl) timeEl.textContent = fmtTime(pomoSeconds);
  if (ringEl) {
    const circumference = 2 * Math.PI * 115;
    const progress = pomoTotalSeconds > 0 ? (1 - pomoSeconds / pomoTotalSeconds) : 0;
    ringEl.setAttribute('stroke-dashoffset', circumference * (1 - progress));
  }
}

function resetPomodoro() {
  clearInterval(pomoTimer);
  pomoRunning = false;
  pomoPhase = 'focus';
  pomoTotalSeconds = pomoSettings.focusDuration * 60;
  pomoSeconds = pomoTotalSeconds;
  pomoSessionCount = 0;
  if (currentView === 'pomodoro') renderPomodoro($('#main-content'));
}

function skipPomodoro() {
  clearInterval(pomoTimer);
  pomoRunning = false;
  pomoSeconds = 0;
  tickPomodoro(); // advance phase
}

// ══════════════════════════════════════════════════════════════════════════
// STOPWATCH VIEW
// ══════════════════════════════════════════════════════════════════════════

function renderStopwatch(container) {
  container.innerHTML = `
    <div class="view-header animate-in">
      <h1>Stopwatch</h1>
      <p>Track time in real-time for any task</p>
    </div>
    <div class="card animate-in" style="max-width:500px;margin:0 auto;text-align:center">
      <div class="form-group" style="max-width:300px;margin:0 auto var(--space-lg)">
        <label class="form-label">Select Task</label>
        <select class="form-select" id="sw-task-select">
          <option value="">Choose a task…</option>
        </select>
      </div>
      <div class="stopwatch-display" id="sw-display">${fmtTime(swSeconds)}</div>
      <div class="timer-controls" style="margin-top:var(--space-lg)">
        <button class="timer-btn" onclick="resetStopwatch()" data-tooltip="Reset">↺</button>
        <button class="timer-btn primary" onclick="toggleStopwatch()" id="sw-play-btn">
          ${swRunning ? '⏸' : '▶'}
        </button>
        <button class="timer-btn" onclick="stopAndSaveStopwatch()" data-tooltip="Save">💾</button>
      </div>
      <div style="margin-top:var(--space-md)">
        <label style="font-size:12px;color:var(--text-tertiary);cursor:pointer">
          <input type="checkbox" id="sw-mini-check" ${swMiniVisible ? 'checked' : ''} onchange="toggleMiniStopwatch(this.checked)">
          Show floating timer
        </label>
      </div>
    </div>
  `;

  // Populate tasks dropdown
  loadTasksDropdown('#sw-task-select', swTaskId);
}

async function loadTasksDropdown(selector, selectedId) {
  try {
    const tasks = await Go.GetTasks();
    const select = $(selector);
    if (!select) return;
    tasks.forEach(t => {
      const opt = document.createElement('option');
      opt.value = t.id;
      opt.textContent = t.name;
      if (t.id === selectedId) opt.selected = true;
      select.appendChild(opt);
    });
    select.addEventListener('change', () => {
      const id = parseInt(select.value);
      const task = tasks.find(t => t.id === id);
      if (selector === '#sw-task-select') {
        swTaskId = id || null;
        swTaskName = task?.name || '';
        const miniName = $('#mini-task-name');
        if (miniName) miniName.textContent = swTaskName || '—';
      }
    });
  } catch (e) { console.error(e); }
}

function toggleStopwatch() {
  if (swRunning) {
    clearInterval(swTimer);
    swRunning = false;
    const btn = $('#sw-play-btn');
    if (btn) btn.textContent = '▶';
    const miniPause = $('#mini-pause');
    if (miniPause) miniPause.textContent = '▶';
  } else {
    swRunning = true;
    swTimer = setInterval(tickStopwatch, 1000);
    const btn = $('#sw-play-btn');
    if (btn) btn.textContent = '⏸';
    const miniPause = $('#mini-pause');
    if (miniPause) miniPause.textContent = '⏸';
  }
}

function tickStopwatch() {
  swSeconds++;
  const display = $('#sw-display');
  if (display) display.textContent = fmtTime(swSeconds);
  const miniTime = $('#mini-time');
  if (miniTime) miniTime.textContent = fmtTime(swSeconds);
}

function resetStopwatch() {
  clearInterval(swTimer);
  swRunning = false;
  swSeconds = 0;
  const display = $('#sw-display');
  if (display) display.textContent = fmtTime(0);
  const btn = $('#sw-play-btn');
  if (btn) btn.textContent = '▶';
  const miniTime = $('#mini-time');
  if (miniTime) miniTime.textContent = fmtTime(0);
}

async function stopAndSaveStopwatch() {
  clearInterval(swTimer);
  swRunning = false;

  if (swSeconds < 1) { alert('Nothing to save — timer is at 0.'); return; }
  if (!swTaskId) { alert('Please select a task first.'); return; }

  try {
    await Go.SaveStopwatchEntry(swTaskId, swSeconds, 'Stopwatch session');
    swSeconds = 0;
    const display = $('#sw-display');
    if (display) display.textContent = fmtTime(0);
    const miniTime = $('#mini-time');
    if (miniTime) miniTime.textContent = fmtTime(0);
    const btn = $('#sw-play-btn');
    if (btn) btn.textContent = '▶';
    hideMiniStopwatch();
  } catch (e) {
    alert('Failed to save: ' + e);
  }
}

function toggleMiniStopwatch(show) {
  swMiniVisible = show;
  const mini = $('#mini-stopwatch');
  if (mini) mini.classList.toggle('hidden', !show);
}

function hideMiniStopwatch() {
  swMiniVisible = false;
  const mini = $('#mini-stopwatch');
  if (mini) mini.classList.add('hidden');
  const check = $('#sw-mini-check');
  if (check) check.checked = false;
}

function togglePin() {
  swPinned = !swPinned;
  Go.ToggleAlwaysOnTop(swPinned);
  const pin = $('#mini-pin');
  if (pin) pin.textContent = swPinned ? '📍' : '📌';
}

// ══════════════════════════════════════════════════════════════════════════
// TIME TRACKING VIEW
// ══════════════════════════════════════════════════════════════════════════

async function renderTimeTracking(container) {
  container.innerHTML = `
    <div class="view-header animate-in flex justify-between items-center">
      <div>
        <h1>Time Log</h1>
        <p>View and manage all tracked time entries</p>
      </div>
      <button class="btn btn-primary" onclick="showAddEntryModal()">+ Add Entry</button>
    </div>
    <div class="card animate-in">
      <div class="flex items-center gap-md mb-md" style="flex-wrap:wrap">
        <div class="form-group" style="margin-bottom:0;flex:1;min-width:140px">
          <label class="form-label">From</label>
          <input type="date" class="form-input" id="tt-start" onchange="reloadTimeEntries()">
        </div>
        <div class="form-group" style="margin-bottom:0;flex:1;min-width:140px">
          <label class="form-label">To</label>
          <input type="date" class="form-input" id="tt-end" onchange="reloadTimeEntries()">
        </div>
      </div>
      <div id="tt-summary" class="flex gap-lg mb-md" style="font-size:13px;color:var(--text-secondary)"></div>
      <div style="overflow-x:auto">
        <table class="data-table">
          <thead><tr>
            <th>Task</th><th>Date</th><th>Start</th><th>End</th><th>Duration</th><th>Note</th><th></th>
          </tr></thead>
          <tbody id="tt-body"></tbody>
        </table>
      </div>
      <div id="tt-empty" class="hidden">
        <div class="empty-state"><div class="empty-icon">📭</div><p>No time entries found for this period.</p></div>
      </div>
    </div>
  `;

  // Default date range: last 30 days
  const now = new Date();
  const start = new Date(now);
  start.setDate(start.getDate() - 30);
  const startInput = $('#tt-start');
  const endInput = $('#tt-end');
  if (startInput) startInput.value = start.toISOString().split('T')[0];
  if (endInput) endInput.value = now.toISOString().split('T')[0];

  await reloadTimeEntries();
}

async function reloadTimeEntries() {
  const startVal = $('#tt-start')?.value || '';
  const endVal = $('#tt-end')?.value || '';
  const tbody = $('#tt-body');
  const emptyEl = $('#tt-empty');
  const summaryEl = $('#tt-summary');
  if (!tbody) return;

  try {
    const entries = await Go.GetTimeEntries(
      startVal ? startVal + 'T00:00:00' : '',
      endVal ? endVal + 'T23:59:59' : ''
    );

    tbody.innerHTML = '';
    if (!entries || entries.length === 0) {
      emptyEl?.classList.remove('hidden');
      if (summaryEl) summaryEl.innerHTML = '';
      return;
    }
    emptyEl?.classList.add('hidden');

    // Summary
    const totalSec = entries.reduce((s, e) => s + e.duration, 0);
    if (summaryEl) {
      summaryEl.innerHTML = `
        <span>📊 ${entries.length} entries</span>
        <span>⏱ Total: <strong>${fmtDuration(totalSec)}</strong></span>
      `;
    }

    entries.forEach(entry => {
      const tr = document.createElement('tr');
      tr.innerHTML = `
        <td><span class="flex items-center gap-sm"><span class="color-dot" style="background:${entry.taskColor}"></span>${entry.taskName}</span></td>
        <td>${fmtDate(entry.startTime)}</td>
        <td>${fmtTimeOnly(entry.startTime)}</td>
        <td>${fmtTimeOnly(entry.endTime)}</td>
        <td><strong>${fmtDuration(entry.duration)}</strong></td>
        <td style="color:var(--text-tertiary);max-width:120px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">${entry.note || '—'}</td>
        <td>
          <div class="flex gap-sm">
            <button class="btn btn-sm btn-ghost" data-edit="${entry.id}">✏️</button>
            <button class="btn btn-sm btn-danger" data-del="${entry.id}">🗑</button>
          </div>
        </td>
      `;
      tr.querySelector('[data-edit]').addEventListener('click', () => showEditEntryModal(entry));
      tr.querySelector('[data-del]').addEventListener('click', () => deleteEntry(entry.id));
      tbody.appendChild(tr);
    });
  } catch (e) {
    tbody.innerHTML = `<tr><td colspan="7" class="text-danger">Error: ${e}</td></tr>`;
  }
}

async function deleteEntry(id) {
  if (!confirm('Delete this time entry?')) return;
  try {
    await Go.DeleteTimeEntry(id);
    await reloadTimeEntries();
  } catch (e) { alert('Failed: ' + e); }
}

// ── Add/Edit Entry Modal ─────────────────────────────────────────────────

function showAddEntryModal() {
  showEntryModal(null);
}

function showEditEntryModal(entry) {
  showEntryModal(entry);
}

async function showEntryModal(entry) {
  const isEdit = !!entry;
  const root = $('#modal-root');

  // Parse existing entry times
  let startVal = '', endVal = '', noteVal = '';
  let taskIdVal = null;
  if (entry) {
    startVal = entry.startTime?.replace(' ', 'T').substring(0, 16) || '';
    endVal = entry.endTime?.replace(' ', 'T').substring(0, 16) || '';
    noteVal = entry.note || '';
    taskIdVal = entry.taskId;
  } else {
    // Default to now
    const now = new Date();
    const end = now.toISOString().substring(0, 16);
    const start = new Date(now.getTime() - 3600000).toISOString().substring(0, 16);
    startVal = start;
    endVal = end;
  }

  root.innerHTML = `
    <div class="modal-overlay" id="entry-modal-overlay">
      <div class="modal">
        <div class="modal-title">${isEdit ? 'Edit Entry' : 'Add Time Entry'}</div>
        <div class="form-group">
          <label class="form-label">Task</label>
          <select class="form-select" id="modal-task"></select>
        </div>
        <div class="grid-2">
          <div class="form-group">
            <label class="form-label">Start Time</label>
            <input type="datetime-local" class="form-input" id="modal-start" value="${startVal}">
          </div>
          <div class="form-group">
            <label class="form-label">End Time</label>
            <input type="datetime-local" class="form-input" id="modal-end" value="${endVal}">
          </div>
        </div>
        <div id="modal-duration-display" class="text-muted" style="font-size:12px;margin-bottom:var(--space-sm)"></div>
        <div class="form-group">
          <label class="form-label">Note</label>
          <input type="text" class="form-input" id="modal-note" value="${noteVal}" placeholder="Optional note…">
        </div>
        <div class="modal-actions">
          <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
          <button class="btn btn-primary" id="modal-save-btn">${isEdit ? 'Update' : 'Add'}</button>
        </div>
      </div>
    </div>
  `;

  // Load tasks
  await loadTasksDropdown('#modal-task', taskIdVal);

  // Duration display
  function updateDuration() {
    const s = new Date($('#modal-start')?.value);
    const e = new Date($('#modal-end')?.value);
    const disp = $('#modal-duration-display');
    if (disp && s && e && e > s) {
      const diff = Math.floor((e - s) / 1000);
      disp.textContent = `Duration: ${fmtDuration(diff)}`;
    } else if (disp) {
      disp.textContent = '';
    }
  }
  updateDuration();
  $('#modal-start')?.addEventListener('change', updateDuration);
  $('#modal-end')?.addEventListener('change', updateDuration);

  // Overlay close
  $('#entry-modal-overlay')?.addEventListener('click', (e) => {
    if (e.target.id === 'entry-modal-overlay') closeModal();
  });

  // Save
  $('#modal-save-btn')?.addEventListener('click', async () => {
    const taskId = parseInt($('#modal-task')?.value);
    const startTime = $('#modal-start')?.value?.replace('T', ' ') + ':00';
    const endTime = $('#modal-end')?.value?.replace('T', ' ') + ':00';
    const note = $('#modal-note')?.value || '';

    if (!taskId) { alert('Please select a task.'); return; }
    const startD = new Date($('#modal-start')?.value);
    const endD = new Date($('#modal-end')?.value);
    if (endD <= startD) { alert('End time must be after start time.'); return; }
    const duration = Math.floor((endD - startD) / 1000);

    try {
      if (isEdit) {
        await Go.UpdateTimeEntry(entry.id, taskId, startTime, endTime, duration, note);
      } else {
        await Go.AddTimeEntry(taskId, startTime, endTime, duration, note);
      }
      closeModal();
      await reloadTimeEntries();
    } catch (e) { alert('Error: ' + e); }
  });
}

function closeModal() {
  const root = $('#modal-root');
  if (root) root.innerHTML = '';
}

// ══════════════════════════════════════════════════════════════════════════
// ANALYTICS VIEW
// ══════════════════════════════════════════════════════════════════════════

async function renderAnalytics(container) {
  container.innerHTML = `
    <div class="view-header animate-in">
      <h1>Analytics</h1>
      <p>Insights into how you spend your time</p>
    </div>
    <div class="flex items-center gap-md mb-lg animate-in">
      <div class="form-group" style="margin-bottom:0">
        <label class="form-label">From</label>
        <input type="date" class="form-input" id="an-start" onchange="reloadAnalytics()">
      </div>
      <div class="form-group" style="margin-bottom:0">
        <label class="form-label">To</label>
        <input type="date" class="form-input" id="an-end" onchange="reloadAnalytics()">
      </div>
    </div>
    <div class="stats-grid stagger" id="an-stats"></div>
    <div class="grid-2 stagger" style="animation-delay:150ms">
      <div class="card">
        <div class="card-header"><span class="card-title">Daily Trend</span></div>
        <div class="chart-container"><canvas id="an-daily-chart"></canvas></div>
      </div>
      <div class="card">
        <div class="card-header"><span class="card-title">Time by Task</span></div>
        <div id="an-task-breakdown"></div>
      </div>
    </div>
  `;

  // Default: last 14 days
  const now = new Date();
  const start = new Date(now);
  start.setDate(start.getDate() - 14);
  const startInput = $('#an-start');
  const endInput = $('#an-end');
  if (startInput) startInput.value = start.toISOString().split('T')[0];
  if (endInput) endInput.value = now.toISOString().split('T')[0];

  await reloadAnalytics();
}

async function reloadAnalytics() {
  const startVal = $('#an-start')?.value || '';
  const endVal = $('#an-end')?.value || '';

  try {
    const data = await Go.GetAnalyticsData(startVal, endVal);

    // Stats
    const avg = data.dailySummary.length > 0 ? data.totalSeconds / data.dailySummary.length : 0;
    const maxDay = data.dailySummary.reduce((m, d) => Math.max(m, d.totalSeconds), 0);
    const statsEl = $('#an-stats');
    if (statsEl) {
      statsEl.innerHTML = `
        <div class="stat-card" style="--stat-accent:var(--accent);--stat-bg:var(--accent-dim)">
          <div class="stat-icon">⏱</div>
          <div class="stat-value">${fmtDuration(data.totalSeconds || 0)}</div>
          <div class="stat-label">Total Time</div>
        </div>
        <div class="stat-card" style="--stat-accent:var(--info);--stat-bg:var(--info-dim)">
          <div class="stat-icon">📊</div>
          <div class="stat-value">${fmtDuration(Math.round(avg))}</div>
          <div class="stat-label">Daily Average</div>
        </div>
        <div class="stat-card" style="--stat-accent:var(--success);--stat-bg:var(--success-dim)">
          <div class="stat-icon">🏆</div>
          <div class="stat-value">${fmtDuration(maxDay)}</div>
          <div class="stat-label">Best Day</div>
        </div>
      `;
    }

    // Daily chart
    drawDailyChart($('#an-daily-chart'), data.dailySummary || []);

    // Task breakdown
    const breakdownEl = $('#an-task-breakdown');
    if (breakdownEl) {
      if (!data.taskSummary || data.taskSummary.length === 0) {
        breakdownEl.innerHTML = '<div class="empty-state"><p>No data in this range.</p></div>';
      } else {
        breakdownEl.innerHTML = '';
        const maxSec = data.taskSummary[0].totalSeconds;
        data.taskSummary.forEach(ts => {
          const pct = maxSec > 0 ? (ts.totalSeconds / maxSec * 100) : 0;
          const item = el('div', { style: { marginBottom: '12px' } }, [
            el('div', { className: 'flex justify-between items-center', style: { marginBottom: '4px' } }, [
              el('span', { className: 'flex items-center gap-sm', style: { fontSize: '13px' } }, [
                el('span', { className: 'color-dot', style: { background: ts.taskColor } }),
                document.createTextNode(ts.taskName),
              ]),
              el('span', { className: 'text-muted', style: { fontSize: '12px' }, textContent: fmtDuration(ts.totalSeconds) }),
            ]),
            el('div', { className: 'progress-bar' }, [
              el('div', { className: 'progress-fill', style: { width: pct + '%', background: ts.taskColor } }),
            ]),
          ]);
          breakdownEl.appendChild(item);
        });
      }
    }
  } catch (e) {
    console.error('Analytics error', e);
  }
}

function drawDailyChart(canvas, dailySummary) {
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.parentElement.getBoundingClientRect();
  canvas.width = rect.width * dpr;
  canvas.height = rect.height * dpr;
  ctx.scale(dpr, dpr);
  const W = rect.width;
  const H = rect.height;

  if (dailySummary.length === 0) {
    ctx.fillStyle = 'hsl(220, 12%, 45%)';
    ctx.font = '13px Inter, sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText('No data available', W / 2, H / 2);
    return;
  }

  const values = dailySummary.map(d => d.totalSeconds / 3600);
  const labels = dailySummary.map(d => {
    const dt = new Date(d.date + 'T00:00:00');
    return dt.toLocaleDateString('en-US', { month: 'short', day: 'numeric' });
  });
  const maxVal = Math.max(...values, 1);

  const padX = 45;
  const padY = 20;
  const chartW = W - padX - 10;
  const chartH = H - padY * 2;

  // Grid
  ctx.strokeStyle = 'hsla(230, 30%, 40%, 0.15)';
  ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const y = padY + (chartH / 4) * i;
    ctx.beginPath();
    ctx.moveTo(padX, y);
    ctx.lineTo(W - 10, y);
    ctx.stroke();
  }

  // Line
  const accentColor = getComputedStyle(document.documentElement).getPropertyValue('--accent').trim() || '#6366f1';
  ctx.strokeStyle = accentColor;
  ctx.lineWidth = 2;
  ctx.lineJoin = 'round';
  ctx.beginPath();
  values.forEach((v, i) => {
    const x = padX + (i / Math.max(values.length - 1, 1)) * chartW;
    const y = padY + chartH - (v / maxVal) * chartH;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
  ctx.stroke();

  // Area fill
  ctx.lineTo(padX + chartW, padY + chartH);
  ctx.lineTo(padX, padY + chartH);
  ctx.closePath();
  const grad = ctx.createLinearGradient(0, padY, 0, padY + chartH);
  grad.addColorStop(0, 'hsla(245, 80%, 65%, 0.2)');
  grad.addColorStop(1, 'hsla(245, 80%, 65%, 0.01)');
  ctx.fillStyle = grad;
  ctx.fill();

  // Dots
  ctx.fillStyle = accentColor;
  values.forEach((v, i) => {
    const x = padX + (i / Math.max(values.length - 1, 1)) * chartW;
    const y = padY + chartH - (v / maxVal) * chartH;
    ctx.beginPath();
    ctx.arc(x, y, 3, 0, Math.PI * 2);
    ctx.fill();
  });

  // X labels (show first, middle, last)
  ctx.fillStyle = 'hsl(220, 12%, 45%)';
  ctx.font = '10px Inter, sans-serif';
  ctx.textAlign = 'center';
  const showIndices = [0, Math.floor(labels.length / 2), labels.length - 1];
  showIndices.forEach(i => {
    if (i < labels.length) {
      const x = padX + (i / Math.max(labels.length - 1, 1)) * chartW;
      ctx.fillText(labels[i], x, H - 4);
    }
  });

  // Y labels
  ctx.textAlign = 'right';
  for (let i = 0; i <= 4; i++) {
    const y = padY + (chartH / 4) * i;
    const v = ((4 - i) / 4 * maxVal).toFixed(1);
    ctx.fillText(v + 'h', padX - 6, y + 3);
  }
}

// ══════════════════════════════════════════════════════════════════════════
// TASKS VIEW
// ══════════════════════════════════════════════════════════════════════════

async function renderTasks(container) {
  container.innerHTML = `
    <div class="view-header animate-in flex justify-between items-center">
      <div>
        <h1>Tasks</h1>
        <p>Manage your projects and task categories</p>
      </div>
      <button class="btn btn-primary" onclick="showAddTaskModal()">+ New Task</button>
    </div>
    <div id="tasks-grid" class="stagger" style="display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:var(--space-md)"></div>
    <div id="tasks-empty" class="hidden">
      <div class="empty-state"><div class="empty-icon">🗂️</div><p>No tasks yet. Create one to start tracking!</p></div>
    </div>
  `;

  await reloadTasks();
}

async function reloadTasks() {
  const grid = $('#tasks-grid');
  const emptyEl = $('#tasks-empty');
  if (!grid) return;

  try {
    const tasks = await Go.GetTasks();
    grid.innerHTML = '';
    if (!tasks || tasks.length === 0) {
      emptyEl?.classList.remove('hidden');
      return;
    }
    emptyEl?.classList.add('hidden');

    tasks.forEach(task => {
      const card = el('div', { className: 'card', style: { position: 'relative', overflow: 'hidden' } }, [
        el('div', { style: { position: 'absolute', top: 0, left: 0, right: 0, height: '3px', background: task.color } }),
        el('div', { className: 'flex items-center gap-md', style: { marginBottom: '8px' } }, [
          el('span', { className: 'color-dot', style: { background: task.color, width: '18px', height: '18px' } }),
          el('span', { style: { fontSize: '15px', fontWeight: '600' }, textContent: task.name }),
        ]),
        el('div', { className: 'text-muted', style: { fontSize: '11px', marginBottom: '12px' }, textContent: `Created ${fmtDate(task.createdAt)}` }),
        el('div', { className: 'flex gap-sm' }, [
          el('button', { className: 'btn btn-sm btn-secondary', textContent: '✏️ Edit', onClick: () => showEditTaskModal(task) }),
          el('button', { className: 'btn btn-sm btn-danger', textContent: '🗑 Delete', onClick: () => deleteTask(task.id) }),
        ]),
      ]);
      grid.appendChild(card);
    });
  } catch (e) {
    grid.innerHTML = `<div class="card text-danger">Error: ${e}</div>`;
  }
}

async function deleteTask(id) {
  if (!confirm('Delete this task and all its time entries?')) return;
  try {
    await Go.DeleteTask(id);
    await reloadTasks();
  } catch (e) { alert('Failed: ' + e); }
}

function showAddTaskModal() {
  showTaskModal(null);
}

function showEditTaskModal(task) {
  showTaskModal(task);
}

function showTaskModal(task) {
  const isEdit = !!task;
  const root = $('#modal-root');
  const selectedColor = task?.color || COLORS[0];

  root.innerHTML = `
    <div class="modal-overlay" id="task-modal-overlay">
      <div class="modal">
        <div class="modal-title">${isEdit ? 'Edit Task' : 'New Task'}</div>
        <div class="form-group">
          <label class="form-label">Task Name</label>
          <input type="text" class="form-input" id="modal-task-name" value="${task?.name || ''}" placeholder="e.g., Deep Work, Meetings…">
        </div>
        <div class="form-group">
          <label class="form-label">Color</label>
          <div class="color-picker-grid" id="modal-color-grid"></div>
        </div>
        <div class="modal-actions">
          <button class="btn btn-secondary" onclick="closeModal()">Cancel</button>
          <button class="btn btn-primary" id="modal-save-task">${isEdit ? 'Update' : 'Create'}</button>
        </div>
      </div>
    </div>
  `;

  // Color grid
  const colorGrid = $('#modal-color-grid');
  let currentColor = selectedColor;
  COLORS.forEach(c => {
    const swatch = el('div', {
      className: `color-swatch ${c === currentColor ? 'selected' : ''}`,
      style: { background: c },
      onClick: () => {
        currentColor = c;
        colorGrid.querySelectorAll('.color-swatch').forEach(s => s.classList.remove('selected'));
        swatch.classList.add('selected');
      }
    });
    colorGrid.appendChild(swatch);
  });

  // Overlay close
  $('#task-modal-overlay')?.addEventListener('click', (e) => {
    if (e.target.id === 'task-modal-overlay') closeModal();
  });

  // Save
  $('#modal-save-task')?.addEventListener('click', async () => {
    const name = $('#modal-task-name')?.value?.trim();
    if (!name) { alert('Task name is required.'); return; }
    try {
      if (isEdit) {
        await Go.UpdateTask(task.id, name, currentColor);
      } else {
        await Go.AddTask(name, currentColor);
      }
      closeModal();
      if (currentView === 'tasks') await reloadTasks();
    } catch (e) { alert('Error: ' + e); }
  });
}

// ══════════════════════════════════════════════════════════════════════════
// SETTINGS VIEW
// ══════════════════════════════════════════════════════════════════════════

async function renderSettings(container) {
  container.innerHTML = `
    <div class="view-header animate-in">
      <h1>Settings</h1>
      <p>Configure your Tempus experience</p>
    </div>
    <div class="card animate-in" style="max-width:600px">
      <div class="card-header"><span class="card-title">About</span></div>
      <div style="display:flex;align-items:center;gap:var(--space-md);margin-bottom:var(--space-md)">
        <div style="width:48px;height:48px;border-radius:var(--r-lg);background:var(--accent-dim);display:flex;align-items:center;justify-content:center;font-size:24px">⏱</div>
        <div>
          <div style="font-weight:600;font-size:16px">Tempus</div>
          <div class="text-muted" style="font-size:12px">v2.0.0 — Built with Go + Wails</div>
        </div>
      </div>
      <p class="text-muted" style="font-size:13px;line-height:1.7">
        Tempus is a modern time tracking application designed for focused productivity.
        Track your tasks, use the Pomodoro technique, and gain insights into how you spend your time.
      </p>
    </div>
    <div class="card animate-in mt-lg" style="max-width:600px">
      <div class="card-header"><span class="card-title">Data</span></div>
      <p class="text-muted" style="font-size:13px;margin-bottom:var(--space-md)">
        Your data is stored locally in a SQLite database. No data leaves your machine.
      </p>
      <div class="flex gap-sm">
        <button class="btn btn-secondary btn-sm" onclick="alert('Data is stored at: %APPDATA%/Tempus/tempus.db')">📁 Data Location</button>
      </div>
    </div>
    <div class="card animate-in mt-lg" style="max-width:600px">
      <div class="card-header"><span class="card-title">Keyboard Shortcuts</span></div>
      <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;font-size:12px">
        <div class="flex justify-between" style="padding:4px 0"><span class="text-muted">Dashboard</span><kbd style="background:var(--bg-elevated);padding:2px 6px;border-radius:4px;font-size:11px">1</kbd></div>
        <div class="flex justify-between" style="padding:4px 0"><span class="text-muted">Pomodoro</span><kbd style="background:var(--bg-elevated);padding:2px 6px;border-radius:4px;font-size:11px">2</kbd></div>
        <div class="flex justify-between" style="padding:4px 0"><span class="text-muted">Stopwatch</span><kbd style="background:var(--bg-elevated);padding:2px 6px;border-radius:4px;font-size:11px">3</kbd></div>
        <div class="flex justify-between" style="padding:4px 0"><span class="text-muted">Time Log</span><kbd style="background:var(--bg-elevated);padding:2px 6px;border-radius:4px;font-size:11px">4</kbd></div>
        <div class="flex justify-between" style="padding:4px 0"><span class="text-muted">Analytics</span><kbd style="background:var(--bg-elevated);padding:2px 6px;border-radius:4px;font-size:11px">5</kbd></div>
        <div class="flex justify-between" style="padding:4px 0"><span class="text-muted">Tasks</span><kbd style="background:var(--bg-elevated);padding:2px 6px;border-radius:4px;font-size:11px">6</kbd></div>
      </div>
    </div>
  `;
}

// ── Keyboard shortcuts ───────────────────────────────────────────────────
document.addEventListener('keydown', (e) => {
  // Don't trigger if user is typing in an input
  if (['INPUT', 'TEXTAREA', 'SELECT'].includes(e.target.tagName)) return;

  switch (e.key) {
    case '1': navigateTo('dashboard'); break;
    case '2': navigateTo('pomodoro'); break;
    case '3': navigateTo('stopwatch'); break;
    case '4': navigateTo('time-tracking'); break;
    case '5': navigateTo('analytics'); break;
    case '6': navigateTo('tasks'); break;
    case ' ':
      if (currentView === 'pomodoro') { e.preventDefault(); togglePomodoro(); }
      else if (currentView === 'stopwatch') { e.preventDefault(); toggleStopwatch(); }
      break;
  }
});

// ── Make functions globally accessible ───────────────────────────────────
window.navigateTo = navigateTo;
window.togglePomodoro = togglePomodoro;
window.resetPomodoro = resetPomodoro;
window.skipPomodoro = skipPomodoro;
window.togglePomoSettings = togglePomoSettings;
window.updatePomoSetting = updatePomoSetting;
window.toggleStopwatch = toggleStopwatch;
window.resetStopwatch = resetStopwatch;
window.stopAndSaveStopwatch = stopAndSaveStopwatch;
window.toggleMiniStopwatch = toggleMiniStopwatch;
window.togglePin = togglePin;
window.showAddEntryModal = showAddEntryModal;
window.showAddTaskModal = showAddTaskModal;
window.closeModal = closeModal;
window.reloadTimeEntries = reloadTimeEntries;
window.reloadAnalytics = reloadAnalytics;
