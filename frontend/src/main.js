/* ═══════════════════════════════════════════════════════════════════════════
   Tempus — Frontend Application (main.js)
   All view rendering, state management, and Go backend communication.
   ═══════════════════════════════════════════════════════════════════════════ */

// ── Wails runtime bindings ───────────────────────────────────────────────
const Go = window.go?.main?.App || {};

// ── State ────────────────────────────────────────────────────────────────
let currentView = 'dashboard';
let redrawChart = null;

// Pomodoro state
const PHASES = {
  'focus':       { label: 'Focus',       setting: 'focusDuration' },
  'short-break': { label: 'Short Break', setting: 'shortBreakDuration' },
  'long-break':  { label: 'Long Break',  setting: 'longBreakDuration' },
};
const POMO_FIELDS = [
  ['focusDuration', 'Focus (min)', 120],
  ['shortBreakDuration', 'Short Break (min)', 30],
  ['longBreakDuration', 'Long Break (min)', 60],
  ['longBreakInterval', 'Long Break Every', 10],
];
const RING = 2 * Math.PI * 115;
let pomoSettings = { focusDuration: 25, shortBreakDuration: 5, longBreakDuration: 15, longBreakInterval: 4 };
let pomoTimer = null;
let pomoRunning = false;
let pomoPhase = 'focus';
let pomoTotalSeconds = 25 * 60;
let pomoSeconds = pomoTotalSeconds;
let pomoEndsAt = 0; // wall-clock ms when the running phase ends
let pomoSessionCount = 0;
let pomoSettingsOpen = false;
let pomoTaskId = null;

// Stopwatch state. Elapsed time is read from the wall clock, so timers that the
// webview throttles while minimized never lose seconds.
let swTimer = null;
let swRunning = false;
let swBaseMs = 0; // elapsed before the current run
let swSince = 0;  // wall-clock ms when the current run started
let swTaskId = null;
let swTaskName = '';
let swMini = false;
let swPinned = false;

// ── Utility helpers ──────────────────────────────────────────────────────

const $ = sel => document.querySelector(sel);
const $$ = sel => document.querySelectorAll(sel);
const pad = n => String(n).padStart(2, '0');
const ymd = (d = new Date()) => `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`;
const localDateTime = d => `${ymd(d)}T${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
const daysAgo = n => { const d = new Date(); d.setDate(d.getDate() - n); return d; };
const esc = str => String(str ?? '').replace(/[&<>"']/g, c => `&#${c.charCodeAt(0)};`);

function fmtDuration(seconds) {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  if (h > 0) return `${h}h ${pad(m)}m`;
  if (m > 0) return `${m}m ${pad(s)}s`;
  return `${s}s`;
}

const fmtTime = seconds => `${pad(Math.floor(seconds / 3600))}:${pad(Math.floor(seconds % 3600 / 60))}:${pad(seconds % 60)}`;
const fmtDate = s => s ? new Date(s).toLocaleDateString('en-US', { month: 'short', day: 'numeric', year: 'numeric' }) : '—';
const fmtDateTime = s => s ? new Date(s).toLocaleString('en-US', { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' }) : '—';
const fmtTimeOnly = s => s ? new Date(s).toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' }) : '—';

const COLORS = [
  '#6366f1','#8b5cf6','#a855f7','#d946ef','#ec4899',
  '#f43f5e','#ef4444','#f97316','#eab308','#22c55e',
  '#14b8a6','#06b6d4','#3b82f6','#2563eb','#7c3aed',
  '#64748b','#f59e0b','#10b981',
];

// ── Shared markup ────────────────────────────────────────────────────────

const emptyState = (icon, text) => `<div class="empty-state"><div class="empty-icon">${icon}</div><p>${text}</p></div>`;

const statCard = (icon, value, label, tone = 'accent') => `
  <div class="stat-card" style="--stat-accent:var(--${tone});--stat-bg:var(--${tone}-dim)">
    <div class="stat-icon">${icon}</div>
    <div class="stat-value">${value}</div>
    <div class="stat-label">${label}</div>
  </div>`;

const activityHTML = (e, meta) => `
  <div class="activity-item">
    <span class="activity-dot" style="background:${e.taskColor}"></span>
    <div class="activity-info">
      <div class="activity-name">${esc(e.taskName)}</div>
      <div class="activity-meta">${meta}</div>
    </div>
    <div class="activity-duration">${fmtDuration(e.duration)}</div>
  </div>`;

const dateRangeHTML = (prefix, days, onchange) => `
  <div class="date-range">
    <div class="form-group">
      <label class="form-label">From</label>
      <input type="date" class="form-input" id="${prefix}-start" value="${ymd(daysAgo(days))}" onchange="${onchange}">
    </div>
    <div class="form-group">
      <label class="form-label">To</label>
      <input type="date" class="form-input" id="${prefix}-end" value="${ymd()}" onchange="${onchange}">
    </div>
  </div>`;

// Routes clicks on [data-edit] / [data-del] buttons to the item at that index.
function bindItemActions(container, items, onEdit, onDelete) {
  container.onclick = e => {
    const btn = e.target.closest('[data-edit], [data-del]');
    if (btn) btn.dataset.edit ? onEdit(items[btn.dataset.edit]) : onDelete(items[btn.dataset.del].id);
  };
}

// ── App shell ────────────────────────────────────────────────────────────

document.addEventListener('DOMContentLoaded', () => {
  $('#btn-minimize').addEventListener('click', () => window.runtime?.WindowMinimise());
  $('#btn-maximize').addEventListener('click', () => window.runtime?.WindowToggleMaximise());
  $('#btn-close').addEventListener('click', () => window.runtime?.Quit());
  $$('.nav-item').forEach(item => item.addEventListener('click', () => navigateTo(item.dataset.view)));
  $('#mini-pause').addEventListener('click', toggleStopwatch);
  $('#mini-stop').addEventListener('click', stopAndSaveStopwatch);
  $('#mini-pin').addEventListener('click', togglePin);
  $('#mini-expand').addEventListener('click', () => setMiniMode(false));
  loadPomoSettings().then(() => navigateTo('dashboard'));
});

window.addEventListener('resize', () => redrawChart?.());

// Each view writes its markup synchronously, then fills data into its own
// element ids, so a slow request can never overwrite a newer view.
function navigateTo(view) {
  currentView = view;
  $$('.nav-item').forEach(n => n.classList.toggle('active', n.dataset.view === view));
  const main = $('#main-content');
  main.scrollTop = 0;
  VIEWS[view][1](main);
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
    <div class="grid-2 stagger">
      <div class="card">
        <div class="card-header"><span class="card-title">This Week</span></div>
        <div class="chart-container"><canvas id="dash-bar-chart"></canvas></div>
      </div>
      <div class="card">
        <div class="card-header">
          <span class="card-title">Recent Activity</span>
          <button class="btn btn-sm btn-ghost" onclick="navigateTo('time-tracking')">View all →</button>
        </div>
        <div id="dash-recent" class="clickable" onclick="navigateTo('time-tracking')"></div>
      </div>
    </div>`;

  try {
    const data = await Go.GetDashboardData();
    const stats = $('#dash-stats');
    if (!stats) return;
    stats.innerHTML =
      statCard('⏱', fmtDuration(data.todaySeconds), 'Today') +
      statCard('📅', fmtDuration(data.weekSeconds), 'This Week', 'info') +
      statCard('📊', fmtDuration(data.totalSeconds), 'All Time', 'success') +
      statCard('🗂️', data.taskCount, 'Tasks', 'warning');
    drawWeekBarChart($('#dash-bar-chart'), data.weekDaySummary);
    $('#dash-recent').innerHTML = data.recentEntries.length
      ? data.recentEntries.map(e => activityHTML(e, fmtDateTime(e.startTime))).join('')
      : emptyState('📭', 'No activity yet. Start tracking!');
  } catch (err) {
    $('#dash-stats')?.insertAdjacentHTML('afterend', `<div class="card text-danger mb-lg">Failed to load dashboard: ${esc(err)}</div>`);
  }
}

// ── Canvas charts ────────────────────────────────────────────────────────

// Sizes the canvas to its container at device resolution, then calls
// draw(ctx, width, height, cssVar). Re-runs on window resize.
function drawChart(canvas, draw) {
  redrawChart = () => {
    if (!canvas.isConnected) return;
    const { width, height } = canvas.parentElement.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    canvas.width = width * dpr;
    canvas.height = height * dpr;
    const ctx = canvas.getContext('2d');
    const css = getComputedStyle(document.body);
    ctx.scale(dpr, dpr);
    ctx.font = `10px ${css.fontFamily}`;
    draw(ctx, width, height, name => css.getPropertyValue(name).trim());
  };
  redrawChart();
}

// Draws the hour grid with y-axis labels and returns the plot area.
function drawGrid(ctx, W, H, max, color) {
  const x0 = 40, y0 = 20, pw = W - x0 - 10, ph = H - 2 * y0;
  ctx.textAlign = 'right';
  for (let i = 0; i <= 4; i++) {
    const y = y0 + ph * i / 4;
    ctx.fillStyle = color('--border-subtle');
    ctx.fillRect(x0, y, pw, 1);
    ctx.fillStyle = color('--text-tertiary');
    ctx.fillText(`${(max * (4 - i) / 4).toFixed(1)}h`, x0 - 6, y + 3);
  }
  ctx.textAlign = 'center';
  return { x0, y0, pw, ph };
}

function drawWeekBarChart(canvas, daySummary) {
  const weekday = d => (d.getDay() + 6) % 7; // Monday = 0
  const values = Array(7).fill(0);
  daySummary.forEach(s => { values[weekday(new Date(`${s.date}T00:00:00`))] = s.totalSeconds / 3600; });
  const max = Math.max(...values, 1), today = weekday(new Date());

  drawChart(canvas, (ctx, W, H, color) => {
    const { x0, y0, pw, ph } = drawGrid(ctx, W, H, max, color);
    const slot = pw / 7;
    ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'].forEach((day, i) => {
      const cx = x0 + slot * (i + 0.5), bh = values[i] / max * ph, top = y0 + ph - bh;
      ctx.fillStyle = color('--accent');
      ctx.globalAlpha = i === today ? 1 : 0.4;
      ctx.beginPath();
      ctx.roundRect(cx - slot * 0.35, top, slot * 0.7, bh, [4, 4, 0, 0]);
      ctx.fill();
      ctx.globalAlpha = 1;
      ctx.fillStyle = color('--text-tertiary');
      ctx.fillText(day, cx, H - 4);
      if (values[i] > 0) {
        ctx.fillStyle = color('--text-primary');
        ctx.fillText(`${values[i].toFixed(1)}h`, cx, top - 6);
      }
    });
  });
}

function drawDailyChart(canvas, days) {
  const values = days.map(d => d.totalSeconds / 3600), max = Math.max(...values, 1);

  drawChart(canvas, (ctx, W, H, color) => {
    if (!values.some(v => v > 0)) {
      ctx.fillStyle = color('--text-tertiary');
      ctx.textAlign = 'center';
      return ctx.fillText('No data available', W / 2, H / 2);
    }
    const { x0, y0, pw, ph } = drawGrid(ctx, W, H, max, color);
    const pts = values.map((v, i) => [x0 + pw * i / Math.max(values.length - 1, 1), y0 + ph - v / max * ph]);

    ctx.fillStyle = ctx.strokeStyle = color('--accent');
    ctx.lineWidth = 2;
    ctx.lineJoin = 'round';
    ctx.beginPath();
    pts.forEach(([x, y]) => ctx.lineTo(x, y));
    ctx.stroke();
    ctx.lineTo(pts.at(-1)[0], y0 + ph);
    ctx.lineTo(x0, y0 + ph);
    ctx.globalAlpha = 0.12;
    ctx.fill();
    ctx.globalAlpha = 1;
    if (pts.length <= 31) pts.forEach(([x, y]) => { ctx.beginPath(); ctx.arc(x, y, 3, 0, 2 * Math.PI); ctx.fill(); });

    ctx.fillStyle = color('--text-tertiary');
    new Set([0, pts.length >> 1, pts.length - 1]).forEach(i => {
      const label = new Date(`${days[i].date}T00:00:00`).toLocaleDateString('en-US', { month: 'short', day: 'numeric' });
      ctx.textAlign = i === 0 ? 'left' : i === pts.length - 1 ? 'right' : 'center';
      ctx.fillText(label, pts[i][0], H - 4);
    });
  });
}

// ══════════════════════════════════════════════════════════════════════════
// TASK PICKER (shared by Stopwatch, Pomodoro and the entry modal)
// ══════════════════════════════════════════════════════════════════════════

const taskPickerHTML = id => `
  <div class="flex gap-sm">
    <select class="form-select" id="${id}"></select>
    <button class="btn btn-secondary" id="${id}-new" data-tooltip="New task">+</button>
  </div>
  <div class="quick-add hidden" id="${id}-add">
    <input type="text" class="form-input" placeholder="New task name…" maxlength="100">
    <button class="btn btn-primary btn-sm">Save</button>
    <button class="btn btn-secondary btn-sm">✕</button>
  </div>`;

// Fills the task <select> and wires its inline "new task" form.
// onChange(id, name) fires whenever the selection is (re)established.
async function initTaskPicker(id, placeholder, selectedId, onChange = () => {}) {
  const select = $(`#${id}`), box = $(`#${id}-add`), input = box.querySelector('input');
  const [saveBtn, cancelBtn] = box.querySelectorAll('button');
  let tasks = [];

  const emit = () => onChange(+select.value || null, tasks.find(t => t.id === +select.value)?.name || '');
  const fill = async selected => {
    tasks = await Go.GetTasks();
    select.innerHTML = (placeholder ? `<option value="">${placeholder}</option>` : '') +
      tasks.map(t => `<option value="${t.id}" ${t.id === selected ? 'selected' : ''}>${esc(t.name)}</option>`).join('');
    emit();
  };
  const hideBox = () => { box.classList.add('hidden'); input.value = ''; };
  const save = async () => {
    const name = input.value.trim();
    if (!name) return;
    try {
      const newId = await Go.AddTask(name, COLORS[Math.floor(Math.random() * COLORS.length)]);
      hideBox();
      await fill(newId);
    } catch (e) { showAlertModal('Error', `Failed to add task: ${e}`); }
  };

  select.addEventListener('change', emit);
  $(`#${id}-new`).addEventListener('click', () => { box.classList.remove('hidden'); input.focus(); });
  cancelBtn.addEventListener('click', hideBox);
  saveBtn.addEventListener('click', save);
  input.addEventListener('keydown', e => e.key === 'Enter' && save());
  try { await fill(selectedId); } catch (e) { console.error(e); }
}

// Lists today's sessions whose note contains the given label.
async function loadTodaySessions(kind) {
  const label = kind === 'pomodoro' ? 'Pomodoro' : 'Stopwatch';
  try {
    const entries = (await Go.GetTimeEntries(`${ymd()}T00:00:00`, `${ymd()}T23:59:59`)).filter(e => e.note.includes(label));
    const box = $(`#${kind}-sessions`);
    if (!box) return;
    box.innerHTML = entries.length
      ? `<div class="card-header"><span class="card-title">Today's ${label} Sessions</span></div>
         <div class="scroll-list">${entries.map(e => activityHTML(e, `${fmtTimeOnly(e.startTime)} – ${fmtTimeOnly(e.endTime)}`)).join('')}</div>`
      : `<p class="text-muted text-center">No ${label.toLowerCase()} sessions logged today yet.</p>`;
  } catch (e) { console.error(e); }
}

// ══════════════════════════════════════════════════════════════════════════
// POMODORO VIEW
// ══════════════════════════════════════════════════════════════════════════

async function renderPomodoro(container) {
  container.innerHTML = `
    <div class="view-header animate-in">
      <h1>Pomodoro Timer</h1>
      <p>Focus deeply, then take restorative breaks</p>
    </div>
    <div class="card narrow animate-in">
      <div class="form-group task-picker">
        <label class="form-label">Associate Task</label>
        ${taskPickerHTML('pomo-task')}
      </div>
      <div class="timer-display">
        <div class="timer-phase" id="pomo-phase"></div>
        <div class="timer-ring-container">
          <svg class="timer-ring" viewBox="0 0 240 240">
            <circle class="timer-ring-bg" cx="120" cy="120" r="115"/>
            <circle class="timer-ring-progress" id="pomo-ring" cx="120" cy="120" r="115"
              stroke-dasharray="${RING}" stroke-dashoffset="${RING * pomoSeconds / pomoTotalSeconds}"/>
          </svg>
          <div class="timer-time" id="pomo-time"></div>
        </div>
        <div class="timer-controls">
          <button class="timer-btn" onclick="resetPomodoro()" data-tooltip="Reset">↺</button>
          <button class="timer-btn primary" onclick="togglePomodoro()" id="pomo-play-btn"></button>
          <button class="timer-btn" onclick="skipPomodoro()" data-tooltip="Skip">⏭</button>
        </div>
        <div class="mt-md"><span class="badge badge-accent" id="pomo-session"></span></div>
      </div>
      <details class="pomo-settings" ${pomoSettingsOpen ? 'open' : ''} ontoggle="pomoSettingsOpen = this.open">
        <summary>⚙️ Timer Settings</summary>
        <div class="pomo-settings-body">
          ${POMO_FIELDS.map(([key, label, max]) => `
            <div class="form-group">
              <label class="form-label">${label}</label>
              <input type="number" class="form-input" min="1" max="${max}" value="${pomoSettings[key]}" onchange="updatePomoSetting('${key}', this)">
            </div>`).join('')}
        </div>
      </details>
    </div>
    <div class="card narrow mt-lg animate-in" id="pomodoro-sessions"></div>`;

  updatePomoDisplay();
  loadTodaySessions('pomodoro');
  await initTaskPicker('pomo-task', 'Choose a task (optional)…', pomoTaskId, id => { pomoTaskId = id; });
}

async function loadPomoSettings() {
  try { pomoSettings = await Go.GetPomodoroSettings(); } catch (e) { console.error('Failed to load pomo settings', e); }
  setPomoPhase('focus');
}

async function updatePomoSetting(key, input) {
  const value = Math.min(+input.max, Math.max(1, parseInt(input.value) || pomoSettings[key]));
  input.value = pomoSettings[key] = value;
  try { await Go.SavePomodoroSettings(pomoSettings); } catch (e) { console.error(e); }
  if (!pomoRunning && PHASES[pomoPhase].setting === key) setPomoPhase(pomoPhase);
}

function setPomoPhase(phase) {
  pomoPhase = phase;
  pomoTotalSeconds = pomoSeconds = pomoSettings[PHASES[phase].setting] * 60;
  updatePomoDisplay();
}

const pomoRemaining = () => Math.max(0, Math.ceil((pomoEndsAt - Date.now()) / 1000));

function togglePomodoro() {
  clearInterval(pomoTimer);
  pomoRunning = !pomoRunning;
  if (pomoRunning) {
    pomoEndsAt = Date.now() + pomoSeconds * 1000;
    pomoTimer = setInterval(tickPomodoro, 250);
  }
  updatePomoDisplay();
}

function tickPomodoro() {
  pomoSeconds = pomoRemaining();
  if (pomoSeconds === 0) finishPomoPhase(true);
  else updatePomoDisplay();
}

// Ends the current phase. A focus phase logs the time actually focused, so
// skipping early never records a full session.
function finishPomoPhase(completed) {
  if (pomoRunning) pomoSeconds = pomoRemaining();
  clearInterval(pomoTimer);
  pomoRunning = false;
  if (pomoPhase !== 'focus') return setPomoPhase('focus');

  const focused = pomoTotalSeconds - pomoSeconds;
  if (pomoTaskId && focused >= 60) {
    Go.SaveStopwatchEntry(pomoTaskId, focused, 'Pomodoro focus session')
      .then(() => loadTodaySessions('pomodoro'))
      .catch(e => console.error('Failed to save Pomodoro session:', e));
  }
  if (completed) pomoSessionCount++;
  setPomoPhase(completed && pomoSessionCount % pomoSettings.longBreakInterval === 0 ? 'long-break' : 'short-break');
}

function skipPomodoro() {
  finishPomoPhase(false);
}

function resetPomodoro() {
  clearInterval(pomoTimer);
  pomoRunning = false;
  pomoSessionCount = 0;
  setPomoPhase('focus');
}

function updatePomoDisplay() {
  const ring = $('#pomo-ring');
  if (!ring) return;
  const isBreak = pomoPhase !== 'focus';
  $('#pomo-phase').textContent = PHASES[pomoPhase].label;
  $('#pomo-phase').classList.toggle('break-mode', isBreak);
  ring.classList.toggle('break-mode', isBreak);
  ring.setAttribute('stroke-dashoffset', RING * pomoSeconds / pomoTotalSeconds);
  $('#pomo-time').textContent = fmtTime(pomoSeconds);
  $('#pomo-play-btn').textContent = pomoRunning ? '⏸' : '▶';
  $('#pomo-session').textContent = `Session ${pomoSessionCount + 1}`;
}

// ══════════════════════════════════════════════════════════════════════════
// STOPWATCH VIEW
// ══════════════════════════════════════════════════════════════════════════

async function renderStopwatch(container) {
  container.innerHTML = `
    <div class="view-header animate-in">
      <h1>Stopwatch</h1>
      <p>Track time in real-time for any task</p>
    </div>
    <div class="card narrow text-center animate-in">
      <div class="form-group task-picker">
        <label class="form-label">Select Task</label>
        ${taskPickerHTML('sw-task')}
      </div>
      <div class="stopwatch-display" id="sw-display"></div>
      <div class="timer-controls mt-lg">
        <button class="timer-btn" onclick="resetStopwatch()" data-tooltip="Reset">↺</button>
        <button class="timer-btn primary" onclick="toggleStopwatch()" id="sw-play-btn"></button>
        <button class="timer-btn" onclick="stopAndSaveStopwatch()" data-tooltip="Save">💾</button>
      </div>
      <label class="checkbox-label mt-md">
        <input type="checkbox" id="sw-mini-check" onchange="setMiniMode(this.checked)"> Show floating timer
      </label>
    </div>
    <div class="card narrow mt-lg animate-in" id="stopwatch-sessions"></div>`;

  syncStopwatch();
  loadTodaySessions('stopwatch');
  await initTaskPicker('sw-task', 'Choose a task…', swTaskId, (id, name) => {
    swTaskId = id;
    swTaskName = name;
    syncStopwatch();
  });
}

const swElapsed = () => Math.floor((swBaseMs + (swRunning ? Date.now() - swSince : 0)) / 1000);

// Pushes stopwatch state to both the main view and the floating timer.
function syncStopwatch() {
  const time = fmtTime(swElapsed()), icon = swRunning ? '⏸' : '▶';
  $$('#sw-display, #mini-time').forEach(n => { n.textContent = time; });
  $$('#sw-play-btn, #mini-pause').forEach(n => { n.textContent = icon; });
  $('#mini-task-name').textContent = swTaskName || '—';
}

function toggleStopwatch() {
  clearInterval(swTimer);
  if (swRunning) swBaseMs += Date.now() - swSince;
  else {
    swSince = Date.now();
    swTimer = setInterval(syncStopwatch, 250);
  }
  swRunning = !swRunning;
  syncStopwatch();
}

function resetStopwatch() {
  clearInterval(swTimer);
  swRunning = false;
  swBaseMs = 0;
  syncStopwatch();
}

async function stopAndSaveStopwatch() {
  await setMiniMode(false); // dialogs don't fit in the floating window
  const seconds = swElapsed();
  if (seconds < 1) return showAlertModal('Validation Warning', 'Nothing to save — timer is at 0.');
  if (!swTaskId) return showAlertModal('Validation Warning', 'Please select a task first.');
  try {
    await Go.SaveStopwatchEntry(swTaskId, seconds, 'Stopwatch session');
    resetStopwatch();
    loadTodaySessions('stopwatch');
  } catch (e) { showAlertModal('Error', `Failed to save: ${e}`); }
}

async function setMiniMode(on) {
  if (swMini === on) return;
  swMini = swPinned = on;
  document.body.classList.toggle('mini-mode', on);
  const check = $('#sw-mini-check');
  if (check) check.checked = on;
  syncPin();
  try { await (on ? Go.EnterMiniMode() : Go.ExitMiniMode()); } catch (e) { console.error(e); }
}

async function togglePin() {
  swPinned = !swPinned;
  syncPin();
  try { await Go.ToggleAlwaysOnTop(swPinned); } catch (e) { console.error(e); }
}

function syncPin() {
  const pin = $('#mini-pin');
  pin.textContent = swPinned ? '📍' : '📌';
  pin.title = swPinned ? 'Unpin from top' : 'Keep on top';
}

// ══════════════════════════════════════════════════════════════════════════
// TIME LOG VIEW
// ══════════════════════════════════════════════════════════════════════════

async function renderTimeTracking(container) {
  container.innerHTML = `
    <div class="view-header animate-in flex justify-between items-center">
      <div>
        <h1>Time Log</h1>
        <p>View and manage all tracked time entries</p>
      </div>
      <button class="btn btn-primary" onclick="showEntryModal()">+ Add Entry</button>
    </div>
    <div class="card animate-in">
      ${dateRangeHTML('tt', 30, 'reloadTimeEntries()')}
      <div id="tt-summary" class="flex gap-lg mb-md text-secondary"></div>
      <div class="table-wrap">
        <table class="data-table">
          <thead><tr><th>Task</th><th>Date</th><th>Start</th><th>End</th><th>Duration</th><th>Note</th><th></th></tr></thead>
          <tbody id="tt-body"></tbody>
        </table>
      </div>
      <div id="tt-empty" class="hidden">${emptyState('📭', 'No time entries found for this period.')}</div>
    </div>`;
  await reloadTimeEntries();
}

async function reloadTimeEntries() {
  const from = $('#tt-start')?.value, to = $('#tt-end')?.value;
  try {
    const entries = await Go.GetTimeEntries(from ? `${from}T00:00:00` : '', to ? `${to}T23:59:59` : '');
    const tbody = $('#tt-body');
    if (!tbody) return;
    const total = entries.reduce((s, e) => s + e.duration, 0);
    $('#tt-empty').classList.toggle('hidden', entries.length > 0);
    $('#tt-summary').innerHTML = entries.length
      ? `<span>📊 ${entries.length} entries</span><span>⏱ Total: <strong>${fmtDuration(total)}</strong></span>`
      : '';
    tbody.innerHTML = entries.map((e, i) => `
      <tr>
        <td><span class="flex items-center gap-sm"><span class="color-dot" style="background:${e.taskColor}"></span>${esc(e.taskName)}</span></td>
        <td>${fmtDate(e.startTime)}</td>
        <td>${fmtTimeOnly(e.startTime)}</td>
        <td>${fmtTimeOnly(e.endTime)}</td>
        <td><strong>${fmtDuration(e.duration)}</strong></td>
        <td class="note-cell" title="${esc(e.note)}">${esc(e.note) || '—'}</td>
        <td><div class="flex gap-sm">
          <button class="btn btn-sm btn-ghost" data-edit="${i}" title="Edit">✏️</button>
          <button class="btn btn-sm btn-danger" data-del="${i}" title="Delete">🗑</button>
        </div></td>
      </tr>`).join('');
    bindItemActions(tbody, entries, showEntryModal, deleteEntry);
  } catch (e) {
    const tbody = $('#tt-body');
    if (tbody) tbody.innerHTML = `<tr><td colspan="7" class="text-danger">Error: ${esc(e)}</td></tr>`;
  }
}

function deleteEntry(id) {
  showConfirmModal('Delete Entry', 'Are you sure you want to delete this time entry? This action cannot be undone.', async () => {
    try {
      await Go.DeleteTimeEntry(id);
      await reloadTimeEntries();
    } catch (e) { showAlertModal('Error', `Failed to delete entry: ${e}`); }
  });
}

async function showEntryModal(entry = null) {
  const now = new Date();
  const m = openModal(`
    <div class="modal-title">${entry ? 'Edit Entry' : 'Add Time Entry'}</div>
    <div class="form-group">
      <label class="form-label">Task</label>
      ${taskPickerHTML('modal-task')}
    </div>
    <div class="grid-2">
      <div class="form-group">
        <label class="form-label">Start Time</label>
        <input type="datetime-local" step="1" class="form-input" id="modal-start" value="${entry?.startTime ?? localDateTime(new Date(now - 3600e3))}">
      </div>
      <div class="form-group">
        <label class="form-label">End Time</label>
        <input type="datetime-local" step="1" class="form-input" id="modal-end" value="${entry?.endTime ?? localDateTime(now)}">
      </div>
    </div>
    <div class="form-hint" id="modal-duration"></div>
    <div class="form-group">
      <label class="form-label">Note</label>
      <input type="text" class="form-input" id="modal-note" value="${esc(entry?.note)}" maxlength="500" placeholder="Optional note…">
    </div>
    <div class="modal-actions">
      <button class="btn btn-secondary" data-close>Cancel</button>
      <button class="btn btn-primary" id="modal-save">${entry ? 'Update' : 'Add'}</button>
    </div>`);

  const field = sel => m.querySelector(sel);
  const withSeconds = v => `${v}:00`.slice(0, 19); // inputs omit ":00" seconds
  const seconds = () => (new Date(field('#modal-end').value) - new Date(field('#modal-start').value)) / 1000;
  const showDuration = () => {
    const s = seconds();
    field('#modal-duration').textContent = s > 0 ? `Duration: ${fmtDuration(s)}` : '';
  };
  showDuration();
  field('#modal-start').addEventListener('input', showDuration);
  field('#modal-end').addEventListener('input', showDuration);

  field('#modal-save').addEventListener('click', async () => {
    const taskId = +field('#modal-task').value;
    if (!taskId) return showAlertModal('Validation Warning', 'Please select a task.');
    if (!(seconds() > 0)) return showAlertModal('Validation Warning', 'End time must be after start time.');
    const args = [taskId, withSeconds(field('#modal-start').value), withSeconds(field('#modal-end').value), field('#modal-note').value];
    try {
      await (entry ? Go.UpdateTimeEntry(entry.id, ...args) : Go.AddTimeEntry(...args));
      m.remove();
      reloadTimeEntries();
    } catch (e) { showAlertModal('Error', `Failed to save entry: ${e}`); }
  });

  await initTaskPicker('modal-task', '', entry?.taskId);
}

// ══════════════════════════════════════════════════════════════════════════
// MODALS — stacked in #modal-root, so an alert never destroys an open form
// ══════════════════════════════════════════════════════════════════════════

function openModal(html, cls = '') {
  const overlay = document.createElement('div');
  overlay.className = 'modal-overlay';
  overlay.innerHTML = `<div class="modal ${cls}">${html}</div>`;
  overlay.addEventListener('click', e => {
    if (e.target === overlay || e.target.closest('[data-close]')) overlay.remove();
  });
  $('#modal-root').appendChild(overlay);
  return overlay;
}

function showAlertModal(title, message) {
  openModal(`
    <div class="modal-title">${esc(title)}</div>
    <p class="modal-text">${esc(message)}</p>
    <div class="modal-actions"><button class="btn btn-primary" data-close>OK</button></div>`, 'modal-sm')
    .querySelector('button').focus();
}

function showConfirmModal(title, message, onConfirm) {
  openModal(`
    <div class="modal-title text-danger">${esc(title)}</div>
    <p class="modal-text">${esc(message)}</p>
    <div class="modal-actions">
      <button class="btn btn-secondary" data-close>Cancel</button>
      <button class="btn btn-danger" data-close data-ok>Delete</button>
    </div>`, 'modal-sm')
    .querySelector('[data-ok]').addEventListener('click', onConfirm);
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
    <div class="animate-in">${dateRangeHTML('an', 14, 'reloadAnalytics()')}</div>
    <div class="stats-grid stagger" id="an-stats"></div>
    <div class="grid-2 stagger">
      <div class="card">
        <div class="card-header"><span class="card-title">Daily Trend</span></div>
        <div class="chart-container"><canvas id="an-daily-chart"></canvas></div>
      </div>
      <div class="card">
        <div class="card-header"><span class="card-title">Time by Task</span></div>
        <div id="an-task-breakdown"></div>
      </div>
    </div>`;
  await reloadAnalytics();
}

async function reloadAnalytics() {
  const from = $('#an-start')?.value, to = $('#an-end')?.value;
  if (!from || !to) return;
  try {
    const data = await Go.GetAnalyticsData(from, to);
    const stats = $('#an-stats');
    if (!stats) return;

    // One point per calendar day, so gaps show as zero instead of being skipped.
    const byDate = Object.fromEntries(data.dailySummary.map(d => [d.date, d.totalSeconds]));
    const days = [];
    for (const d = new Date(`${from}T00:00:00`); ymd(d) <= to; d.setDate(d.getDate() + 1)) {
      days.push({ date: ymd(d), totalSeconds: byDate[ymd(d)] || 0 });
    }
    const active = data.dailySummary.length;
    stats.innerHTML =
      statCard('⏱', fmtDuration(data.totalSeconds), 'Total Time') +
      statCard('📊', fmtDuration(active ? Math.round(data.totalSeconds / active) : 0), 'Daily Average', 'info') +
      statCard('🏆', fmtDuration(Math.max(0, ...data.dailySummary.map(d => d.totalSeconds))), 'Best Day', 'success');
    drawDailyChart($('#an-daily-chart'), days);

    const max = data.taskSummary[0]?.totalSeconds;
    $('#an-task-breakdown').innerHTML = data.taskSummary.length ? data.taskSummary.map(t => `
      <div class="breakdown-row">
        <div class="flex justify-between items-center">
          <span class="flex items-center gap-sm"><span class="color-dot" style="background:${t.taskColor}"></span>${esc(t.taskName)}</span>
          <span class="text-muted">${fmtDuration(t.totalSeconds)}</span>
        </div>
        <div class="progress-bar"><div class="progress-fill" style="width:${t.totalSeconds / max * 100}%;background:${t.taskColor}"></div></div>
      </div>`).join('') : emptyState('📊', 'No data in this range.');
  } catch (e) {
    console.error('Analytics error', e);
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
      <button class="btn btn-primary" onclick="showTaskModal()">+ New Task</button>
    </div>
    <div id="tasks-grid" class="tasks-grid stagger"></div>`;
  await reloadTasks();
}

async function reloadTasks() {
  try {
    const tasks = await Go.GetTasks();
    const grid = $('#tasks-grid');
    if (!grid) return;
    grid.innerHTML = tasks.map((t, i) => `
      <div class="card task-card" style="--task-color:${t.color}">
        <div class="task-card-name"><span class="color-dot"></span>${esc(t.name)}</div>
        <div class="task-card-date text-muted">Created ${fmtDate(`${t.createdAt.replace(' ', 'T')}Z`)}</div>
        <div class="flex gap-sm">
          <button class="btn btn-sm btn-secondary" data-edit="${i}">✏️ Edit</button>
          <button class="btn btn-sm btn-danger" data-del="${i}">🗑 Delete</button>
        </div>
      </div>`).join('') || emptyState('🗂️', 'No tasks yet. Create one to start tracking!');
    bindItemActions(grid, tasks, showTaskModal, deleteTask);
  } catch (e) {
    const grid = $('#tasks-grid');
    if (grid) grid.innerHTML = `<div class="card text-danger">Error: ${esc(e)}</div>`;
  }
}

function deleteTask(id) {
  showConfirmModal('Delete Task', 'Are you sure you want to delete this task and all its associated time entries? This action cannot be undone.', async () => {
    try {
      await Go.DeleteTask(id);
      await reloadTasks();
    } catch (e) { showAlertModal('Error', `Failed to delete task: ${e}`); }
  });
}

function showTaskModal(task = null) {
  let color = task?.color || COLORS[0];
  const m = openModal(`
    <div class="modal-title">${task ? 'Edit Task' : 'New Task'}</div>
    <div class="form-group">
      <label class="form-label">Task Name</label>
      <input type="text" class="form-input" id="modal-task-name" value="${esc(task?.name)}" maxlength="100" placeholder="e.g., Deep Work, Meetings…">
    </div>
    <div class="form-group">
      <label class="form-label">Color</label>
      <div class="color-picker-grid">
        ${COLORS.map(c => `<button class="color-swatch ${c === color ? 'selected' : ''}" style="background:${c}" data-color="${c}"></button>`).join('')}
      </div>
    </div>
    <div class="modal-actions">
      <button class="btn btn-secondary" data-close>Cancel</button>
      <button class="btn btn-primary" id="modal-save-task">${task ? 'Update' : 'Create'}</button>
    </div>`);

  const input = m.querySelector('#modal-task-name');
  const save = async () => {
    const name = input.value.trim();
    if (!name) return showAlertModal('Validation Warning', 'Task name is required.');
    try {
      await (task ? Go.UpdateTask(task.id, name, color) : Go.AddTask(name, color));
      m.remove();
      if (currentView === 'tasks') reloadTasks();
    } catch (e) { showAlertModal('Error', `Failed to save task: ${e}`); }
  };

  m.querySelector('.color-picker-grid').addEventListener('click', e => {
    const swatch = e.target.closest('[data-color]');
    if (!swatch) return;
    color = swatch.dataset.color;
    m.querySelectorAll('.color-swatch').forEach(s => s.classList.toggle('selected', s === swatch));
  });
  m.querySelector('#modal-save-task').addEventListener('click', save);
  input.addEventListener('keydown', e => e.key === 'Enter' && save());
  input.focus();
}

// ══════════════════════════════════════════════════════════════════════════
// SETTINGS VIEW
// ══════════════════════════════════════════════════════════════════════════

function renderSettings(container) {
  container.innerHTML = `
    <div class="view-header animate-in">
      <h1>Settings</h1>
      <p>Configure your Tempus experience</p>
    </div>
    <div class="settings-stack stagger">
      <div class="card">
        <div class="card-header"><span class="card-title">About</span></div>
        <div class="about-row">
          <div class="about-logo">⏱</div>
          <div>
            <div class="about-name">Tempus</div>
            <div class="text-muted">v2.0.0 — Built with Go + Wails</div>
          </div>
        </div>
        <p class="text-muted">
          Tempus is a modern time tracking application designed for focused productivity.
          Track your tasks, use the Pomodoro technique, and gain insights into how you spend your time.
        </p>
      </div>
      <div class="card">
        <div class="card-header"><span class="card-title">Data</span></div>
        <p class="text-muted">
          Your data is stored locally in a SQLite database in your user config folder
          (<code>%APPDATA%\\Tempus\\tempus.db</code> on Windows). No data leaves your machine.
        </p>
      </div>
      <div class="card">
        <div class="card-header"><span class="card-title">Keyboard Shortcuts</span></div>
        <div class="shortcut-grid">
          ${Object.values(VIEWS).map(([label], i) => `<div><span class="text-muted">${label}</span><kbd>${i + 1}</kbd></div>`).join('')}
          <div><span class="text-muted">Start / pause timer</span><kbd>Space</kbd></div>
        </div>
      </div>
    </div>`;
}

// ── Views & keyboard shortcuts (number keys follow this order) ───────────

const VIEWS = {
  'dashboard':     ['Dashboard', renderDashboard],
  'time-tracking': ['Time Log', renderTimeTracking],
  'stopwatch':     ['Stopwatch', renderStopwatch],
  'pomodoro':      ['Pomodoro', renderPomodoro],
  'analytics':     ['Analytics', renderAnalytics],
  'tasks':         ['Tasks', renderTasks],
  'settings':      ['Settings', renderSettings],
};

document.addEventListener('keydown', e => {
  const modal = $('#modal-root').lastElementChild;
  if (e.key === 'Escape') return modal?.remove();
  if (modal || e.target.matches('input, select, textarea')) return;

  if (e.key === ' ') {
    const toggle = swMini || currentView === 'stopwatch' ? toggleStopwatch
      : currentView === 'pomodoro' ? togglePomodoro : null;
    if (toggle) { e.preventDefault(); toggle(); }
  } else {
    const view = Object.keys(VIEWS)[e.key - 1];
    if (view && !swMini) navigateTo(view);
  }
});
