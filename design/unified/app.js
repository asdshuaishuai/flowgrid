/* ==========================================================================
   FlowGrid Interaction Layer v2
   --------------------------------------------------------------------------
   交互范式与动效节奏全部沿用上一版，本文件只做「参数化 + 跨平台统一」：
     · 托盘浮层唤出/关闭        → fadeIn .22s ease-enter（上一版 .2~.25s）
     · 折叠区                   → chevron rotate(-90deg) 200ms
     · 布局网格拖拽             → cursor:grab / hover scale(1.05)
     · 切换设备                 → 全屏遮罩 rgba(0,0,0,.15) + 居中提示 scale(.8→1) .3s
     · Toast                    → 底部居中 translateY(80px)→0，停留 1800ms
     · 状态点 pulse             → 2s infinite
     · 实时延迟刷新             → 2000ms
   没有引入任何新的交互范式。
   ========================================================================== */

(function (global) {
  'use strict';

  /* ── 图标：统一 24 网格 / 1.75 描边，替换上一版的 emoji 与字符 ──────── */
  const P = {
    chevronDown:  '<path d="M6 9l6 6 6-6"/>',
    chevronRight: '<path d="M9 6l6 6-6 6"/>',
    check:        '<path d="M20 6L9 17l-5-5"/>',
    close:        '<path d="M18 6L6 18M6 6l12 12"/>',
    plus:         '<path d="M12 5v14M5 12h14"/>',
    minus:        '<path d="M5 12h14"/>',
    arrowRight:   '<path d="M5 12h14M13 6l6 6-6 6"/>',
    refresh:      '<path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 4v5h-5"/>',
    power:        '<path d="M12 3v9"/><path d="M18.4 6.6a9 9 0 1 1-12.8 0"/>',
    devices:      '<rect x="2" y="5" width="13" height="9" rx="1.5"/><rect x="15" y="9" width="7" height="10" rx="1.5"/><path d="M6 18h5"/>',
    keyboard:     '<rect x="2" y="6" width="20" height="12" rx="2"/><path d="M6 10h.01M10 10h.01M14 10h.01M18 10h.01M7 14h10"/>',
    clipboard:    '<rect x="6" y="4" width="12" height="4" rx="1"/><path d="M6 6H5a1 1 0 0 0-1 1v12a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1V7a1 1 0 0 0-1-1h-1"/>',
    activity:     '<path d="M3 12h4l3-8 4 16 3-8h4"/>',
    settings:     '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.9l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-2.9 1.2v.2a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.6 1.7 1.7 0 0 0-1.9.4l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0-1.2-2.9H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.6-1.1 1.7 1.7 0 0 0-.4-1.9l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.9.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 2.9 1.2l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.9V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>',
    search:       '<circle cx="11" cy="11" r="7"/><path d="M20 20l-3.5-3.5"/>',
    monitor:      '<rect x="2" y="3" width="20" height="13" rx="2"/><path d="M8 20h8M12 16v4"/>',
    laptop:       '<rect x="3" y="5" width="18" height="11" rx="2"/><path d="M2 19h20"/>',
    tablet:       '<rect x="4" y="2" width="16" height="20" rx="2"/><path d="M11 18h2"/>',
    phone:        '<rect x="6" y="2" width="12" height="20" rx="2.5"/><path d="M11 18h2"/>',
    wifi:         '<path d="M5 12.5a10 10 0 0 1 14 0"/><path d="M8.5 16a5 5 0 0 1 7 0"/><path d="M12 19.5h.01"/>',
    lan:          '<path d="M9 15l6-6"/><rect x="2" y="13" width="6" height="6" rx="1"/><rect x="16" y="3" width="6" height="6" rx="1"/>',
    lock:         '<rect x="4" y="10" width="16" height="10" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3"/>',
    unlock:       '<rect x="4" y="10" width="16" height="10" rx="2"/><path d="M8 10V7a4 4 0 0 1 7.5-2"/>',
    eye:          '<path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7-10-7-10-7z"/><circle cx="12" cy="12" r="3"/>',
    trash:        '<path d="M4 7h16M10 11v6M14 11v6"/><path d="M6 7l1 13h10l1-13M9 7V4h6v3"/>',
    edit:         '<path d="M12 20h9"/><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4z"/>',
    more:         '<circle cx="5" cy="12" r="1.2"/><circle cx="12" cy="12" r="1.2"/><circle cx="19" cy="12" r="1.2"/>',
    info:         '<circle cx="12" cy="12" r="9"/><path d="M12 16v-4M12 8h.01"/>',
    warn:         '<path d="M10.3 3.9 2.4 18a2 2 0 0 0 1.7 3h15.8a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/><path d="M12 9v4M12 17h.01"/>',
    file:         '<path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><path d="M14 3v5h5"/>',
    upload:       '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><path d="M12 3v12"/><path d="M7 8l5-5 5 5"/>',
    download:     '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><path d="M12 15V3"/><path d="M7 10l5 5 5-5"/>',
    folder:       '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>',
    copy:         '<rect x="9" y="9" width="12" height="12" rx="2"/><path d="M5 15H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v1"/>',
    image:        '<rect x="3" y="4" width="18" height="16" rx="2"/><circle cx="8.5" cy="9.5" r="1.5"/><path d="M21 16l-5-5-9 9"/>',
    type:         '<path d="M4 6V4h16v2M12 4v16M9 20h6"/>',
    code:         '<path d="M8 6l-5 6 5 6M16 6l5 6-5 6"/>',
    grid:         '<rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/>',
    grip:         '<circle cx="9" cy="6" r="1"/><circle cx="15" cy="6" r="1"/><circle cx="9" cy="12" r="1"/><circle cx="15" cy="12" r="1"/><circle cx="9" cy="18" r="1"/><circle cx="15" cy="18" r="1"/>',
    sun:          '<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>',
    moon:         '<path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/>',
    auto:         '<circle cx="12" cy="12" r="9"/><path d="M12 3v18a9 9 0 0 0 0-18z" fill="currentColor" stroke="none"/>',
    apple:        '<path d="M16.5 12.7c0-2.2 1.8-3.3 1.9-3.4-1-1.5-2.6-1.7-3.2-1.7-1.3-.1-2.6.8-3.3.8-.7 0-1.7-.8-2.8-.8-1.4 0-2.8.9-3.5 2.2-1.5 2.6-.4 6.5 1.1 8.6.7 1 1.6 2.2 2.7 2.2 1.1 0 1.5-.7 2.8-.7 1.3 0 1.6.7 2.7.7 1.1 0 1.9-1.1 2.6-2.1.4-.6.9-1.6 1.3-2.7-1.3-.5-2.3-1.7-2.3-3.1z"/><path d="M14.6 6.3c.6-.7 1-1.7.9-2.7-.9 0-2 .6-2.6 1.3-.6.7-1 1.7-.9 2.6 1 .1 2-.5 2.6-1.2z"/>',
    windows:      '<rect x="3" y="3" width="8" height="8" rx="1"/><rect x="13" y="3" width="8" height="8" rx="1"/><rect x="3" y="13" width="8" height="8" rx="1"/><rect x="13" y="13" width="8" height="8" rx="1"/>',
    linux:        '<ellipse cx="12" cy="6" rx="3.5" ry="3"/><path d="M8.5 9.5C7 11 6 13.5 6 16c0 1.5.8 3 2 4M15.5 9.5C17 11 18 13.5 18 16c0 1.5-.8 3-2 4"/><ellipse cx="9" cy="6" rx="1" ry="1.6"/><ellipse cx="15" cy="6" rx="1" ry="1.6"/><path d="M12 9v4"/>',
    deepin:       '<circle cx="12" cy="12" r="9"/><path d="M8 12h8M12 8v8"/>',
    shield:       '<path d="M12 3l8 3v6c0 5-3.5 8.3-8 9-4.5-.7-8-4-8-9V6z"/><path d="M9 12l2 2 4-4"/>',
    bell:         '<path d="M18 8a6 6 0 1 0-12 0c0 7-3 7-3 7h18s-3 0-3-7"/><path d="M13.7 21a2 2 0 0 1-3.4 0"/>',
    pause:        '<rect x="6" y="5" width="4" height="14" rx="1"/><rect x="14" y="5" width="4" height="14" rx="1"/>',
    play:         '<path d="M7 4l12 8-12 8z"/>',
    spinner:      '<path d="M12 3a9 9 0 1 0 9 9"/>',
    drag:         '<path d="M8 6h.01M8 12h.01M8 18h.01M16 6h.01M16 12h.01M16 18h.01"/>',
    mouse:        '<rect x="6" y="3" width="12" height="18" rx="6"/><path d="M12 7v4"/>',
    link:         '<path d="M10 13a5 5 0 0 0 7.5.5l3-3a5 5 0 0 0-7-7l-1.7 1.7"/><path d="M14 11a5 5 0 0 0-7.5-.5l-3 3a5 5 0 0 0 7 7l1.7-1.7"/>',
    unlink:       '<path d="M18.8 13.5l1.7-1.7a5 5 0 0 0-7-7l-1.7 1.7M5.2 10.5l-1.7 1.7a5 5 0 0 0 7 7l1.7-1.7"/><path d="M3 3l18 18"/>',
  };

  function icon(name, cls) {
    const d = P[name];
    if (!d) return '';
    return '<svg class="ic' + (cls ? ' ' + cls : '') + '" viewBox="0 0 24 24" aria-hidden="true">' + d + '</svg>';
  }

  /* ── 平台与主题 ─────────────────────────────────────────────────────── */
  const PLATS = [
    { id: 'macos',   name: 'macOS' },
    { id: 'windows', name: 'Windows' },
    { id: 'kde',     name: 'KDE' },
    { id: 'deepin',  name: 'deepin' },
  ];
  const THEMES = [
    { id: 'light', name: '浅色' },
    { id: 'dark',  name: '深色' },
  ];
  const PLAT_ICON = { macos: 'apple', windows: 'windows', kde: 'linux', deepin: 'deepin' };

  function getPlat()  { return localStorage.getItem('fg.plat')  || 'macos'; }
  function getTheme() { return localStorage.getItem('fg.theme') || 'light'; }
  function applyRoot() {
    const r = document.documentElement;
    r.setAttribute('data-plat', getPlat());
    r.setAttribute('data-theme', getTheme());
  }

  /* 快捷键符号：上一版 mac 用 ⌘，win/kde 用 Ctrl —— 保留该差异 */
  function kbd(key) { return getPlat() === 'macos' ? '⌘' + key : 'Ctrl+' + key; }
  function mod()    { return getPlat() === 'macos' ? '⌘' : 'Ctrl'; }
  const QUIT_KEY = () => kbd('Q');

  /* ── Toast：上一版 1800ms ──────────────────────────────────────────── */
  let toastEl = null, toastTimer = null;
  function toast(msg, kind) {
    if (!toastEl) { toastEl = document.createElement('div'); toastEl.className = 'toast'; document.body.appendChild(toastEl); }
    toastEl.innerHTML = icon(kind === 'danger' ? 'warn' : kind === 'ok' ? 'check' : 'info') + '<span>' + msg + '</span>';
    // 强制回流，保证连续触发时动画重放
    void toastEl.offsetWidth;
    toastEl.classList.add('toast--show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { toastEl.classList.remove('toast--show'); }, 1800);
  }

  /* ── 切换设备：上一版遮罩 + 居中提示，.3s ──────────────────────────── */
  let ovEl = null, swEl = null;
  function ensureOverlay() {
    if (!ovEl) { ovEl = document.createElement('div'); ovEl.className = 'overlay'; document.body.appendChild(ovEl); }
    if (!swEl) { swEl = document.createElement('div'); swEl.className = 'switchtoast'; document.body.appendChild(swEl); }
  }
  function switchTo(name) {
    ensureOverlay();
    ovEl.classList.add('overlay--on');
    swEl.innerHTML = icon('arrowRight') + '<span>已切换到 ' + name + '</span>';
    void swEl.offsetWidth;
    swEl.classList.add('switchtoast--show');
    setTimeout(function () {
      swEl.classList.remove('switchtoast--show');
      ovEl.classList.remove('overlay--on');
    }, 900);
  }

  /* ── 桌面衬底：上一版任务栏 / 菜单栏 / 面板 ────────────────────────── */
  function dockHTML() {
    const p = getPlat();
    const brand = '<span class="dock__brand">' + icon(PLAT_ICON[p], 'ic--sm') + 'FlowGrid <span class="dot dot--pulse"></span></span>';
    if (p === 'macos') {
      return brand +
        '<span class="dock__spacer"></span>' +
        '<span class="dock__item">' + icon('wifi', 'ic--sm') + '</span>' +
        '<span class="dock__item">' + icon('bell', 'ic--sm') + '</span>' +
        '<span class="dock__item">' + icon('search', 'ic--sm') + '</span>';
    }
    if (p === 'windows') {
      return icon('windows', 'ic--sm') + '<span>开始</span>' +
        '<span class="dock__spacer"></span>' +
        '<span class="dock__item">' + icon('wifi', 'ic--sm') + '</span>' +
        '<span class="dock__item">' + icon('bell', 'ic--sm') + '</span>' +
        brand;
    }
    return '<span class="dock__item">' + icon('wifi', 'ic--sm') + '</span>' +
      '<span class="dock__item">' + icon('mouse', 'ic--sm') + '</span>' +
      '<span class="dock__item">' + icon('bell', 'ic--sm') + '</span>' +
      brand;
  }

  /* ── 标题栏：按平台绘制窗口控件，位置沿用上一版 ────────────────────── */
  /* 返回 .titlebar 的内部结构；外层元素由页面持有，平台切换时只换内部 */
  function titlebarHTML(title, opts) {
    opts = opts || {};
    const p = getPlat();
    const left = '<span class="titlebar__left">' +
      (p === 'windows' || p === 'deepin' ? '<span style="width:14px;height:14px;border-radius:4px;background:var(--fg-accent)"></span>' : '') +
      '<span class="titlebar__title">' + title + '</span></span>';

    let controls = '';
    if (p === 'macos') {
      controls = '<span class="wctl">' +
        '<button class="wctl__dot wctl__dot--close" title="关闭" data-action="close"></button>' +
        '<button class="wctl__dot wctl__dot--min" title="最小化" data-action="close"></button>' +
        '<button class="wctl__dot wctl__dot--max" title="最大化" data-action="close"></button>' +
        '</span>';
    } else {
      controls = '<span class="wctl">' +
        '<button class="wctl__flat" title="最小化" data-action="close">' + icon('minus', 'ic--sm') + '</button>' +
        '<button class="wctl__flat" title="最大化" data-action="close">' + icon('grid', 'ic--sm') + '</button>' +
        '<button class="wctl__flat wctl__flat--close" title="关闭" data-action="close">' + icon('close', 'ic--sm') + '</button>' +
        '</span>';
    }

    // mac 控件在左，其余在右 —— 沿用上一版
    if (p === 'macos') {
      return controls + left +
        (opts.right ? '<span class="titlebar__right">' + opts.right + '</span>' : '<span></span>');
    }
    return left +
      (opts.right ? '<span class="titlebar__right">' + opts.right + '</span>' : '') + controls;
  }

  /* ── 折叠区：沿用上一版 chevron 旋转 ───────────────────────────────── */
  function bindSections(root) {
    (root || document).querySelectorAll('[data-collapse]').forEach(function (head) {
      if (head.dataset.bound) return;
      head.dataset.bound = '1';
      head.setAttribute('tabindex', '0');
      head.setAttribute('role', 'button');
      const body = document.getElementById(head.getAttribute('data-collapse'));
      const chev = head.querySelector('.chev');
      if (head.hasAttribute('data-collapsed')) {
        if (body) body.hidden = true;
        if (chev) chev.classList.add('chev--collapsed');
        head.setAttribute('aria-expanded', 'false');
      } else {
        head.setAttribute('aria-expanded', 'true');
      }
      const flip = function () {
        const open = !(body && body.hidden);
        if (body) body.hidden = open;
        if (chev) chev.classList.toggle('chev--collapsed', open);
        head.setAttribute('aria-expanded', String(!open));
      };
      head.addEventListener('click', flip);
      head.addEventListener('keydown', function (e) {
        if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); flip(); }
      });
    });
  }

  /* ── 开关 ──────────────────────────────────────────────────────────── */
  function bindSwitches(root) {
    (root || document).querySelectorAll('.switch').forEach(function (el) {
      if (el.dataset.bound) return;
      el.dataset.bound = '1';
      el.setAttribute('role', 'switch');
      if (!el.hasAttribute('aria-checked')) el.setAttribute('aria-checked', el.classList.contains('on') ? 'true' : 'false');
      el.addEventListener('click', function () {
        const on = el.getAttribute('aria-checked') !== 'true';
        el.setAttribute('aria-checked', String(on));
        if (el.dataset.label) toast(el.dataset.label + (on ? '已开启' : '已关闭'), on ? 'ok' : null);
      });
      el.addEventListener('keydown', function (e) {
        if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); el.click(); }
      });
    });
  }

  /* ── 布局网格拖拽：沿用上一版 grab / hover scale ───────────────────── */
  function bindDrag(root) {
    const grid = (root || document).querySelector('[data-layout]');
    if (!grid || grid.dataset.bound) return;
    grid.dataset.bound = '1';
    let dragging = null;

    grid.querySelectorAll('[draggable="true"]').forEach(function (cell) {
      cell.addEventListener('dragstart', function (e) {
        dragging = cell;
        cell.classList.add('cell--dragging');
        if (e.dataTransfer) { e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('text/plain', cell.dataset.dev || ''); }
      });
      cell.addEventListener('dragend', function () {
        cell.classList.remove('cell--dragging');
        grid.querySelectorAll('.cell--over').forEach(function (n) { n.classList.remove('cell--over'); });
        dragging = null;
      });
    });

    grid.querySelectorAll('[data-slot]').forEach(function (slot) {
      slot.addEventListener('dragover', function (e) {
        if (!dragging) return;
        e.preventDefault();
        if (e.dataTransfer) e.dataTransfer.dropEffect = 'move';
        slot.classList.add('cell--over');
      });
      slot.addEventListener('dragleave', function () { slot.classList.remove('cell--over'); });
      slot.addEventListener('drop', function (e) {
        e.preventDefault();
        slot.classList.remove('cell--over');
        if (!dragging) return;
        const target = slot.closest('.cell') || slot;
        // 交换两个格子的内容
        const a = dragging, b = target;
        if (a === b) return;
        const holder = document.createElement('div');
        grid.insertBefore(holder, a);
        grid.insertBefore(a, b);
        grid.insertBefore(b, holder);
        grid.removeChild(holder);
        toast('布局已更新');
      });
    });
  }

  /* ── 延迟跳动：上一版 2000ms ───────────────────────────────────────── */
  function latencyClass(ms) { return ms < 8 ? 'good' : ms < 20 ? 'warn' : 'bad'; }
  let latencyTimer = null;
  function startLatency() {
    if (latencyTimer) return;          // 平台切换时不要重复起定时器
    // 每 tick 重新查询，平台切换后新渲染的节点也能被覆盖
    latencyTimer = setInterval(function () {
      // 与上一版同一区间：1.5 ~ 3.0ms
      const v = (1.5 + Math.random() * 1.5).toFixed(1);
      document.querySelectorAll('[data-lat]').forEach(function (n) {
        n.textContent = v + 'ms';
        if (n.classList.contains('lat')) {
          n.className = 'lat lat--' + latencyClass(parseFloat(v));
        }
      });
    }, 2000);
  }

  /* ── 事件委托：data-action ─────────────────────────────────────────── */
  const ACTIONS = {
    toast: function (el) { toast(el.dataset.msg || '操作完成'); },
    switch: function (el) { switchTo(el.dataset.dev || '目标设备'); },
    close: function () { toast('窗口已关闭（演示）'); },
    quit: function () {
      const tray = document.getElementById('tray');
      if (tray) tray.innerHTML = '<div class="empty"><span class="empty__icon">' + icon('power') + '</span>' +
        '<span class="empty__title">FlowGrid 已退出</span>' +
        '<span class="empty__desc">托盘图标不再常驻。重新运行 FlowGrid 即可恢复。</span></div>';
      toast('FlowGrid 已退出', 'danger');
    },
    pair: function () { toast('配对请求已发送'); },
  };

  function bindAll(root) {
    bindSections(root);
    bindSwitches(root);
    bindDrag(root);
    const scope = root || document;
    if (scope === document && scope.dataset.actions) return;
    scope.addEventListener('click', function (e) {
      const el = e.target.closest('[data-action]');
      if (!el) return;
      const fn = ACTIONS[el.dataset.action];
      if (fn) { e.preventDefault(); fn(el); }
    });
    if (scope === document) scope.dataset.actions = '1';
  }

  /* ── 设计稿控制条 ──────────────────────────────────────────────────── */
  function buildDevbar(bar) {
    bar.innerHTML =
      '<span class="devbar__label">平台</span><span class="seg" data-seg="plat">' +
        PLATS.map(function (p) { return '<button data-plat-set="' + p.id + '">' + p.name + '</button>'; }).join('') +
      '</span>' +
      '<span class="devbar__sep"></span>' +
      '<span class="devbar__label">主题</span><span class="seg" data-seg="theme">' +
        THEMES.map(function (t) { return '<button data-theme-set="' + t.id + '">' + t.name + '</button>'; }).join('') +
      '</span>';
    bar.addEventListener('click', function (e) {
      const pb = e.target.closest('[data-plat-set]');
      const tb = e.target.closest('[data-theme-set]');
      if (pb) { localStorage.setItem('fg.plat', pb.dataset.platSet); }
      if (tb) { localStorage.setItem('fg.theme', tb.dataset.themeSet); }
      if (pb || tb) { applyRoot(); syncSeg(); refreshPlatformText(); }
    });
    syncSeg();
  }
  function syncSeg() {
    const p = getPlat(), t = getTheme();
    document.querySelectorAll('[data-plat-set]').forEach(function (b) {
      b.setAttribute('aria-pressed', String(b.dataset.platSet === p));
    });
    document.querySelectorAll('[data-theme-set]').forEach(function (b) {
      b.setAttribute('aria-pressed', String(b.dataset.themeSet === t));
    });
  }
  /* 平台切换后需要重算的部分（快捷键符号、窗口控件） */
  function refreshPlatformText() {
    document.querySelectorAll('[data-kbd]').forEach(function (n) { n.textContent = kbd(n.dataset.kbd); });
    document.querySelectorAll('[data-mod]').forEach(function (n) { n.textContent = mod(); });
    document.querySelectorAll('[data-quit]').forEach(function (n) { n.textContent = QUIT_KEY(); });
    // 只换内部结构，外层元素保留，避免标记被 outerHTML 吃掉
    document.querySelectorAll('[data-dock]').forEach(function (n) { n.innerHTML = dockHTML(); });
    document.querySelectorAll('[data-titlebar]').forEach(function (n) {
      n.innerHTML = titlebarHTML(n.dataset.titlebar, { right: n.dataset.right || '' });
    });
    bindSections(document);
  }

  /* ── 启动 ──────────────────────────────────────────────────────────── */
  function boot() {
    applyRoot();
    const bar = document.querySelector('[data-devbar]');
    if (bar) buildDevbar(bar);
    document.querySelectorAll('[data-dock]').forEach(function (n) { n.innerHTML = dockHTML(); });
    document.querySelectorAll('[data-titlebar]').forEach(function (n) {
      n.innerHTML = titlebarHTML(n.dataset.titlebar, { right: n.dataset.right || '' });
    });
    document.querySelectorAll('[data-icon]').forEach(function (n) { n.outerHTML = icon(n.dataset.icon); });
    document.querySelectorAll('[data-kbd]').forEach(function (n) { n.textContent = kbd(n.dataset.kbd); });
    document.querySelectorAll('[data-mod]').forEach(function (n) { n.textContent = mod(); });
    document.querySelectorAll('[data-quit]').forEach(function (n) { n.textContent = QUIT_KEY(); });
    bindAll(document);
    startLatency();
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
  else boot();

  global.FG = {
    icon: icon, toast: toast, switchTo: switchTo,
    titlebarHTML: titlebarHTML, dockHTML: dockHTML,
    kbd: kbd, mod: mod,
    bindAll: bindAll, boot: boot,
    getPlat: getPlat, getTheme: getTheme,
    latencyClass: latencyClass,
  };
})(window);
