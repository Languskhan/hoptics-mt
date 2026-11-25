// Hoptics site: fills the updates, firmware list and flasher from nodes.json, updates.json
// and firmware/version.json (written by tools/make_site.py from the latest release).
const $ = (s, el = document) => el.querySelector(s);
const make = (tag, attrs = {}, ...kids) => {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') el.className = v;
    else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else if (v !== false && v != null) el.setAttribute(k, v === true ? '' : v);
  }
  for (const kid of kids.flat()) if (kid != null) el.append(kid);
  return el;
};
const getJSON = (url) => fetch(url, { cache: 'no-store' }).then(r => r.ok ? r.json() : null).catch(() => null);

const data = Promise.all([getJSON('nodes.json'), getJSON('updates.json'), getJSON('firmware/version.json')])
  .then(([n, u, v]) => ({ nodes: (n && n.nodes) || [], updates: (u && u.updates) || [], ver: v }));

const TAGS = { released: 'Released', progress: 'In progress', coming: 'Coming' };
const PLATFORM = { meshtastic: ['mt', 'Meshtastic'], meshcore: ['mc', 'MeshCore'] };

function release(ver, id) {
  return ver && ver.boards && ver.boards[id] ? ver.boards[id] : null;
}
function versionLine(node, ver) {
  if (node.status !== 'available') return 'Not released yet';
  const r = release(ver, node.id);
  if (!r) return 'Not in this release';
  return ver.tag + (node.beta ? ' beta' : '') + (r.meshtastic ? ' · Meshtastic ' + r.meshtastic : '');
}

// ---------- home page ----------
function renderUpdates(list) {
  const box = $('#update-list');
  if (!box) return;
  box.replaceChildren(...list.slice(0, 6).map(u => make('article', { class: 'card' },
    make('div', { class: 'top' },
      make('span', { class: 'pill ' + (TAGS[u.tag] ? u.tag : 'released') }, TAGS[u.tag] || 'Update'),
      make('span', { class: 'date' }, u.date || '')),
    make('h3', {}, u.title),
    make('p', {}, u.text))));
}

function renderNodes(nodes, ver, filter) {
  const box = $('#node-list');
  if (!box) return;
  const shown = nodes.filter(n => filter === 'all' || n.platform === filter);
  box.replaceChildren(...shown.map(n => {
    const [cls, label] = PLATFORM[n.platform] || ['mt', n.platform];
    const r = release(ver, n.id);
    const ready = n.status === 'available' && r;
    const base = 'firmware/' + n.id + '/';
    const files = [];
    if (ready) {
      files.push(make('a', { class: 'btn', href: 'flash.html?node=' + encodeURIComponent(n.id) }, 'Flash in browser'));
      if (r.factory) files.push(make('a', { class: 'btn ghost', href: base + r.factory, download: r.factory }, 'Factory .bin'));
      if (r.update) files.push(make('a', { class: 'btn ghost', href: base + r.update, download: r.update }, 'Update .bin'));
    }
    return make('article', { class: 'card node' },
      make('div', { class: 'top' },
        make('span', { class: 'pill ' + cls }, (n.firmware || 'Hoptics') + ' · ' + label),
        make('span', { class: 'pill ' + (!ready ? 'soon' : n.beta ? 'beta' : 'ok') }, !ready ? 'Coming soon' : n.beta ? 'Beta' : 'Available')),
      make('h3', {}, n.name),
      make('p', {}, n.detail || ''),
      ready ? make('p', { class: 'ver mono' }, versionLine(n, ver)) : null,
      ready ? make('div', { class: 'files' }, files)
            : make('p', { class: 'dl' }, 'Not released yet. Watch the updates above.'));
  }));
  if (!shown.length) box.replaceChildren(make('p', { class: 'sub' }, 'No nodes here yet.'));
}

function setupFilter(nodes, ver) {
  const chips = document.querySelectorAll('.chip');
  if (!chips.length) return;
  const apply = (f) => {
    chips.forEach(c => c.setAttribute('aria-pressed', String(c.dataset.filter === f)));
    renderNodes(nodes, ver, f);
  };
  chips.forEach(c => c.addEventListener('click', () => apply(c.dataset.filter)));
  apply('all');
}

// ---------- flasher page ----------
function setupFlasher(nodes, ver) {
  const pick = $('#pick');
  if (!pick) return;
  const want = new URLSearchParams(location.search).get('node');
  const usable = (n) => n.status === 'available' && release(ver, n.id);
  let current = (nodes.find(n => n.id === want && usable(n)) || nodes.find(usable) || {}).id;

  const draw = () => {
    pick.replaceChildren(...nodes.map(n => {
      const ok = usable(n);
      const [, label] = PLATFORM[n.platform] || ['', n.platform];
      return make('button', {
        type: 'button', class: 'opt', 'aria-pressed': String(n.id === current), disabled: !ok,
        onclick: () => { current = n.id; draw(); }
      }, make('b', {}, n.name), make('span', {}, (n.firmware || 'Hoptics') + ' · ' + label + (ok ? (n.beta ? ' · beta' : '') : ' · coming soon')));
    }));
    const node = nodes.find(n => n.id === current);
    const esp = $('#esp'), uf2 = $('#uf2'), btn = $('#install');
    if (!node) { esp.hidden = true; uf2.hidden = true; $('#summary').textContent = 'No firmware released yet.'; return; }
    const r = release(ver, node.id);
    $('#summary').textContent = node.name + ' · ' + versionLine(node, ver);
    if (node.flash === 'uf2') {
      esp.hidden = true; uf2.hidden = false;
      const f = r && (r.uf2 || r.factory);
      const a = $('#uf2-link');
      a.href = 'firmware/' + node.id + '/' + (f || '');
      a.setAttribute('download', f || '');
    } else {
      uf2.hidden = true; esp.hidden = false;
      btn.setAttribute('manifest', 'firmware/' + node.id + '/manifest.json');
    }
  };
  draw();
}

data.then(({ nodes, updates, ver }) => {
  const v = $('#ver');
  if (v && ver) v.textContent = 'Latest release: ' + ver.tag;
  renderUpdates(updates);
  setupFilter(nodes, ver);
  setupFlasher(nodes, ver);
});

// Web Serial is what the browser flasher needs; say so up front.
if (!('serial' in navigator)) document.querySelectorAll('.bad').forEach(el => { el.style.display = 'block'; });
