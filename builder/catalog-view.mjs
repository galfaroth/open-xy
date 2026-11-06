import VEFF from './veff.json' with { type: 'json' };
import LINEAR_PREVIEW from './linear-preview.json' with { type: 'json' };
import SYNC from './sync-status.json' with { type: 'json' };
// Linear by Veff's user guide, shown at #/linear exactly as the repository documents it.
import LINEAR_GUIDE from '../docs/arranger.md?raw';

// The Open-XY Mods by Veff presentation of the machine collection: a browsable grid of
// machine cards, a detail view per machine, the fork's own features and its own
// machines (builder/veff.json), and the Open-XY revision this installer is based on.
// It only presents: selection, numbers, linking and installation stay in app.mjs,
// which calls these hooks when it has rendered a card, inspected a machine or
// changed the selection.

// The Open-XY release and upstream commit this site was built from (vite.config.mjs
// defines it; a bundle made without Vite falls back to the collection's version).
const BUILT_FROM = typeof __VEFF_BASE__ === 'undefined' ? null : __VEFF_BASE__;
const KINDS = {synth: 'Synth', effect: 'Send effect', drum: 'Drum kit', sample: 'Sample engine', player: 'Note player'};
const WHERE = {synth: 'an instrument track’s engine', effect: 'a send effect', drum: 'a drum track’s engine',
  sample: 'a sampler track’s engine', player: 'a note player'};

export function createCatalogView({core, visuals, snapshots, modeFor, getKit, isSelected, setSelected,
  data = VEFF, base = BUILT_FROM, doc = document} = {}) {
  const $ = id => doc.getElementById(id);
  const veffMachines = data.machines ?? {};
  const veffMachine = id => Object.hasOwn(veffMachines, id) ? veffMachines[id] : null;
  let current = null, returnScroll = 0;

  function node(tag, text, className) {
    const el = doc.createElement(tag);
    if (text !== undefined) el.textContent = text;
    if (className) el.className = className;
    return el;
  }
  function link(text, url) {
    const a = node('a', text); a.href = url; a.target = '_blank'; a.rel = 'noopener noreferrer';
    return a;
  }
  function badge() {
    const b = node('span', 'by Veff', 'veff-badge');
    b.title = 'A Veff machine';
    return b;
  }

  // "Based on Open-XY 0.0.5 · staging a2893dd", linked to that upstream commit.
  // Green when this installer carries Open-XY's latest staging; yellow while an Open-XY
  // SDK change waits for its hardware test with Linear by Veff (tools/veff_sync.py).
  function renderSync() {
    const repository = data.upstream?.repository ?? 'https://github.com/etcetc000/open-xy';
    const testing = SYNC.state === 'in-testing';
    const title = testing
      ? 'Open-XY has changed its SDK' + (SYNC.pending?.since ? ' (in testing since ' + SYNC.pending.since + ')' : '') +
        '. Linear by Veff is being tested on an OP–XY with it; until then this installer stays on the last tested Open-XY, and the update follows, usually within a few days.'
      : 'Every synth, effect, player and extension on Open-XY staging is here.';
    for (const target of doc.querySelectorAll('[data-veff-sync]')) {
      const dot = node('span', undefined, 'sync-dot'); dot.dataset.state = testing ? 'testing' : 'current';
      dot.setAttribute('aria-hidden', 'true');
      const label = testing ? 'Open-XY SDK update in testing' : 'Up to date with Open-XY';
      target.replaceChildren(dot, node('span', label, 'sync-label'));
      if (testing && SYNC.pending?.commit)
        target.append(' · ', link('staging ' + SYNC.pending.commit.slice(0, 7), repository + '/commit/' + SYNC.pending.commit));
      target.title = title;
      if (target.dataset.veffSync !== 'short') target.append(node('span', ' — ' + title, 'sync-detail'));
    }
  }
  // About Veff: who makes this fork, in a dialog from the header and footer.
  function renderAbout() {
    const about = data.about, dialog = doc.getElementById('about-veff');
    if (!about || !dialog) return;
    const links = Object.entries(about.links ?? {}).filter(([, url]) => typeof url === 'string' && /^https:\/\//.test(url));
    const linkRow = cls => { const row = node('p', undefined, cls); links.forEach(([label, url], i) => { if (i) row.append(' · '); row.append(link(label, url)); }); return row; };
    const head = node('div', undefined, 'about-head');
    if (about.photo) { const img = doc.createElement('img'); img.src = about.photo; img.alt = about.photoAlt ?? ''; img.width = 120; img.height = 120; img.className = 'about-photo'; head.append(img); }
    const titles = node('div');
    const h = node('h2', about.title ?? 'About'); h.id = 'about-veff-title';
    titles.append(node('span', 'open-xy mods by veff', 'eyebrow'), h, node('p', (about.roles ?? []).join(' · '), 'about-roles'));
    head.append(titles);
    const close = node('button', 'Close', 'secondary about-close'); close.type = 'button'; close.onclick = () => dialog.close();
    // A bio paragraph may link inline with [text](https://…).
    const paragraph = text => {
      const p = node('p', undefined, 'about-bio');
      let at = 0;
      for (const m of text.matchAll(/\[([^\]]+)\]\((https:\/\/[^)\s]+)\)/g)) {
        p.append(text.slice(at, m.index), link(m[1], m[2])); at = m.index + m[0].length;
      }
      p.append(text.slice(at));
      return p;
    };
    dialog.replaceChildren(close, head, ...(about.bio ?? []).map(paragraph));
    if (links.length) dialog.append(linkRow('about-links'));
    dialog.onclick = event => { if (event.target === dialog) dialog.close(); };
    for (const button of doc.querySelectorAll('[data-about-open]')) { button.hidden = false; button.onclick = () => go('#/about'); }
    dialog.addEventListener('close', () => { if (location.hash === '#/about') leave(); });
    for (const target of doc.querySelectorAll('[data-about-links]')) {
      target.hidden = !links.length;
      if (links.length) target.replaceChildren('Veff: ', ...linkRow('').childNodes);
    }
  }
  // A small Markdown reader for the guide: headings, paragraphs, lists, tables, **bold**, `code`, links.
  function inline(text) {
    const out = [];
    const pattern = /\*\*([^*]+)\*\*|`([^`]+)`|\[([^\]]+)\]\(([^)]+)\)/g;
    let at = 0;
    for (const m of text.matchAll(pattern)) {
      out.push(text.slice(at, m.index));
      if (m[1] !== undefined) out.push(node('strong', m[1]));
      else if (m[2] !== undefined) out.push(node('code', m[2]));
      else out.push(/^https:\/\//.test(m[4]) ? link(m[3], m[4]) :
        link(m[3], (data.fork?.repository ?? 'https://github.com/galfaroth/open-xy') + '/blob/veff/staging/docs/' + m[4]));
      at = m.index + m[0].length;
    }
    out.push(text.slice(at));
    return out;
  }
  function markdown(source) {
    const blocks = [], lines = source.replace(/\r/g, '').split('\n');
    for (let i = 0; i < lines.length;) {
      const line = lines[i];
      if (!line.trim()) { ++i; continue; }
      const heading = /^(#{1,3}) (.*)$/.exec(line);
      if (heading) { const h = node('h' + (heading[1].length + 1)); h.append(...inline(heading[2])); blocks.push(h); ++i; continue; }
      if (line.startsWith('|')) {
        const rows = [];
        while (i < lines.length && lines[i].startsWith('|')) rows.push(lines[i++]);
        const cells = row => row.replace(/^\||\|$/g, '').split('|').map(c => c.trim());
        const table = node('table', undefined, 'guide-table'), head = node('tr');
        for (const c of cells(rows[0])) { const th = node('th'); th.append(...inline(c)); head.append(th); }
        const thead = node('thead'); thead.append(head); const tbody = node('tbody');
        for (const row of rows.slice(2)) { const tr = node('tr'); for (const c of cells(row)) { const td = node('td'); td.append(...inline(c)); tr.append(td); } tbody.append(tr); }
        table.append(thead, tbody); blocks.push(table); continue;
      }
      const item = /^(\d+\.|-) (.*)$/.exec(line);
      if (item) {
        const list = node(item[1] === '-' ? 'ul' : 'ol');
        while (i < lines.length && /^(\d+\.|-) /.test(lines[i])) {
          let text = lines[i++].replace(/^(\d+\.|-) /, '');
          while (i < lines.length && /^ {2,}\S/.test(lines[i])) text += ' ' + lines[i++].trim();
          const li = node('li'); li.append(...inline(text)); list.append(li);
        }
        blocks.push(list); continue;
      }
      let text = line;
      while (++i < lines.length && lines[i].trim() && !/^(#|\||\d+\. |- )/.test(lines[i])) text += ' ' + lines[i].trim();
      const p = node('p'); p.append(...inline(text)); blocks.push(p);
    }
    return blocks;
  }
  function renderGuide() {
    const dialog = doc.getElementById('linear-guide');
    if (!dialog || !LINEAR_GUIDE) return;
    const close = node('button', 'Close', 'secondary about-close'); close.type = 'button'; close.onclick = () => dialog.close();
    const body = node('div', undefined, 'guide-body'); body.append(...markdown(LINEAR_GUIDE));
    body.querySelector('h2')?.setAttribute('id', 'linear-guide-title');
    dialog.replaceChildren(close, body);
    dialog.onclick = event => { if (event.target === dialog) dialog.close(); };
    dialog.addEventListener('close', () => { if (location.hash === '#/linear') leave(); });
    for (const target of doc.querySelectorAll('[data-linear-guide]')) target.onclick = event => { event.preventDefault(); event.stopPropagation(); go('#/linear'); };
  }
  function renderBase(version = base?.version) {
    renderSync(); renderAbout(); renderGuide();
    for (const target of doc.querySelectorAll('[data-veff-base]')) {
      const parts = [(data.upstream?.name ?? 'Open-XY') + (version ? ' ' + version : '')];
      target.replaceChildren('Based on ', link(parts[0], data.upstream?.site ?? 'https://open-xy.com'));
      if (base?.commit) {
        target.append(' · ', link((base.branch ? base.branch + ' ' : '') + base.commit.slice(0, 7),
          (data.upstream?.repository ?? 'https://github.com/etcetc000/open-xy') + '/commit/' + base.commit));
      }
    }
  }
  function renderFeatures() {
    for (const target of doc.querySelectorAll('[data-veff-features]')) {
      target.replaceChildren(...(data.features ?? []).map(feature => {
        const card = node('article', undefined, 'veff-feature');
        card.append(node('span', 'Built into this firmware', 'veff-kicker'), node('h3', feature.name));
        if (feature.preview === 'linear' && LINEAR_PREVIEW.gif) {
          const screen = node('figure', undefined, 'veff-feature-screen');
          const img = doc.createElement('img');
          img.src = LINEAR_PREVIEW.gif; img.width = 720; img.height = 330; img.loading = 'lazy';
          img.alt = 'Linear by Veff on the OP–XY screen: playing an arrangement, adding and stretching a clip, the actions menu, bouncing a clip to a sampler, the piano roll and muting a clip.';
          screen.append(img);
          screen.dataset.linearGuide = ''; screen.title = 'Read how Linear by Veff works';
          card.append(screen);
        }
        card.append(node('p', feature.summary ?? ''));
        if (target.dataset.veffFeatures === 'full') for (const text of feature.help ?? []) card.append(node('p', text, 'veff-feature-help'));
        if (feature.note) card.append(node('p', feature.note, 'veff-feature-note'));
        if (feature.preview === 'linear') {
          const more = node('a', 'Read more →', 'veff-feature-more');
          more.href = '#/linear'; more.dataset.linearGuide = ''; card.append(more);
        }
        return card;
      }));
    }
  }
  // A card's screen: the machine's default frame, its static artwork, or its kind's glyph.
  const thumbnails = new WeakMap();
  function thumbnail(m) {
    const kit = getKit();
    if (!m.artwork || !kit) return Promise.resolve(null);
    let artwork;
    try { artwork = core.selectArtwork(m, modeFor(m)); } catch { return Promise.resolve(null); }
    let cache = thumbnails.get(m);
    if (!cache) { cache = new Map(); thumbnails.set(m, cache); }
    if (!cache.has(artwork)) {
      const svg = m.preview ? snapshots.render(m, artwork) : Promise.resolve(kit.screenTemplate.replace('</svg>', artwork + '</svg>'));
      cache.set(artwork, svg.then(markup => URL.createObjectURL(new Blob([markup], {type: 'image/svg+xml'}))).catch(() => null));
    }
    return cache.get(artwork);
  }
  function screen(m) {
    const frame = node('span', undefined, 'card-screen');
    frame.setAttribute('aria-hidden', 'true');
    const glyph = node('span', undefined, 'card-glyph');
    glyph.append(visuals.icon(m.kind, m.icon));
    frame.append(glyph);
    thumbnail(m).then(url => {
      if (!url) return;
      const image = node('img'); image.alt = ''; image.src = url; image.decoding = 'async';
      frame.replaceChildren(image); frame.dataset.art = 'true';
    });
    return frame;
  }

  // Who maintains it: the catalog's maintainer, else (for a Veff machine built into a
  // personal kit) the one veff.json names.
  function maintainer(m) {
    const ex = veffMachine(m.id);
    return m.source?.maintainer ?? (ex ? ex.maintainer ?? data.fork?.maintainer ?? '' : '');
  }
  function card(row, m, button) {
    row.classList.add('card');
    const ex = veffMachine(m.id);
    const author = maintainer(m);
    if (ex) {
      row.dataset.veff = 'machine';
      // The badge replaces the Yours/Community label; a locally built Veff machine names its maintainer.
      button.querySelector('.machine-origin')?.remove();
      const name = button.querySelector('.machine-name');
      if (name && !name.querySelector('.machine-author')) name.append(' ', node('span', '', 'machine-author'));
    }
    // A card names its maintainer on its own line.
    const credit = button.querySelector('.machine-author');
    if (credit && author) credit.textContent = 'by ' + author;
    button.prepend(screen(m));
    const tags = node('span', undefined, 'card-tags');
    tags.append(node('span', KINDS[m.kind] ?? m.kind, 'card-kind'));
    if (ex) tags.append(badge());
    button.querySelector('.machine-desc')?.before(tags);
    if (m.keywords?.length) button.append(node('span', m.keywords.join(' · '), 'card-keywords'));
    // The card selects; its title opens the machine's page.
    button.setAttribute('aria-label', m.name + (author ? ' by ' + author : '') + ', ' +
      (KINDS[m.kind] ?? m.kind) + (ex ? ', a Veff machine' : '') + '. Add to or remove from the install');
    const title = button.querySelector('.machine-name');
    title?.classList.add('card-title');
    const more = node('a', 'Details →', 'card-more');
    more.href = '#preview/machines/' + m.id;
    more.setAttribute('aria-label', 'Details of ' + m.name);
    row.append(more);
    more.href = '#/machine/' + m.id;
    more.addEventListener('click', event => { event.preventDefault(); go('#/machine/' + m.id); });
    button.addEventListener('click', event => { if (event.target.closest('.card-title')) go('#/machine/' + m.id); });
  }

  // The encoders and what each edits: a position's own parameter first, then the ones a
  // press switches to (tooling/definitions.py control_positions).
  function controlMap(m) {
    const positions = m.positions ?? m.controls.map((_, i) => i % 4);
    const list = node('ol', undefined, 'encoder-map');
    let alternates = false;
    for (let p = 0; p < 4; p++) {
      const names = m.controls.filter((_, i) => positions[i] === p);
      const item = node('li', undefined, 'encoder');
      item.append(node('span', String(p + 1), 'encoder-dot'));
      const params = node('ul');
      names.forEach((name, i) => {
        const entry = node('li', name, i ? 'encoder-alt' : 'encoder-main');
        if (i && m.pages) entry.dataset.page = String(i + 1);
        params.append(entry);
      });
      if (!names.length) params.append(node('li', '—', 'encoder-main'));
      if (names.length > 1) alternates = true;
      item.append(params);
      list.append(item);
    }
    const note = node('p', m.pages ? 'Press an encoder to switch all four to the next page.'
      : alternates ? 'Press an encoder to switch to the parameter listed under it.' : '', 'encoder-note');
    note.hidden = !note.textContent;
    return [list, note];
  }
  function help(m) {
    const ex = veffMachine(m.id);
    if (ex?.help?.length) return ex.help.map(text => node('p', text));
    const paragraphs = ['After installing, choose ' + m.name + ' on your OP–XY as ' + (WHERE[m.kind] ?? 'a machine') + '.',
      m.controls.length + (m.controls.length === 1 ? ' control sits' : ' controls sit') + ' on the four encoders, as listed above.'];
    if (m.preview) paragraphs.push('Play the keyboard under the screen to hear it here. The OP–XY’s own envelopes and effects aren’t part of the preview.');
    return paragraphs.map(text => node('p', text));
  }

  function showSelection() {
    const toggle = $('detail-select');
    if (!toggle || !current) return;
    const on = isSelected(current.id);
    toggle.textContent = on ? '✓ Selected for install' : 'Add to install';
    toggle.setAttribute('aria-pressed', String(on));
    toggle.setAttribute('aria-label', (on ? 'Remove ' : 'Add ') + current.name + (on ? ' from' : ' to') + ' the install');
  }
  function detail(m) {
    current = m;
    const ex = veffMachine(m.id);
    const block = $('detail-veff');
    block.hidden = !ex;
    if (ex) {
      block.replaceChildren(badge());
      if (ex.summary) block.append(node('p', ex.summary));
      // A locally built Veff machine has no catalog source; credit its maintainer and this fork.
      if (m.origin === 'personal') {
        $('detail-source').replaceChildren('Maintained by ' + maintainer(m) + ' · ',
          link(data.fork?.name ?? 'Open-XY Mods by Veff', data.fork?.repository ?? 'https://github.com/galfaroth/open-xy'));
      }
    }
    // The Controls section already lists a machine's controls when there is nothing to play.
    $('preview-controls-panel').hidden = !m.preview;
    // Without a browser preview, its static screen still shows what it looks like.
    if (!m.preview) thumbnail(m).then(url => {
      if (!url || current !== m) return;
      const image = node('img'); image.src = url; image.alt = m.name + ' screen';
      $('art-preview').replaceChildren(image);
      $('art-caption').textContent = 'Its screen as drawn. This machine has no sound preview in the browser.';
    });
    $('detail-controls-list').replaceChildren(...controlMap(m));
    $('detail-help-text').replaceChildren(...help(m));
    showSelection();
  }

  // Routes, in the address so Back and links work: '' (the catalog), #/machine/<id>, #/about.
  let routedInside = false;
  function go(hash) { routedInside = true; location.hash = hash; }
  function leave() {
    if (routedInside) { routedInside = false; history.back(); }
    else history.pushState(null, '', location.pathname + location.search), window.dispatchEvent(new HashChangeEvent('hashchange'));
  }
  const workspace = () => $('machine-workspace');
  const isOpen = () => workspace()?.dataset.view === 'detail';
  function open() {
    const area = workspace();
    if (!area || isOpen()) return;
    returnScroll = window.scrollY;
    area.dataset.view = 'detail';
    $('preview-controls-panel').open = true;
    window.scrollTo(0, 0);
    $('detail-back').focus({preventScroll: true});
  }
  function close() {
    const area = workspace();
    if (!area || !isOpen()) return;
    delete area.dataset.view;
    // Closing the preview panel stops its audio (app.mjs).
    $('preview-controls-panel').open = false;
    window.scrollTo(0, returnScroll);
    const id = current?.id;
    const button = id && [...doc.querySelectorAll('#machines button[data-id]')].find(b => b.dataset.id === id);
    (button || $('machine-search'))?.focus({preventScroll: true});
  }

  $('detail-back').onclick = () => (/^#\/machine\//.test(location.hash) ? leave() : close());
  $('detail-select').onclick = () => { if (current) { setSelected(current.id, !isSelected(current.id)); showSelection(); } };
  doc.addEventListener('keydown', event => {
    if (event.key !== 'Escape' || !isOpen() || $('selected-previews')?.open || $('machine-presets')?.open) return;
    if (workspace().closest('[data-page]')?.hidden) return;
    if (/^#\/machine\//.test(location.hash)) leave(); else close();
  });
  for (const id of ['veff-intro', 'veff-featured']) if ($(id)) $(id).hidden = false;
  doc.documentElement.dataset.veff = 'true';
  renderBase(); renderFeatures();

  return {
    loaded(kit) { renderBase(kit.version); },
    // Veff machines start unselected, like community machines.
    chosenByDefault: m => !veffMachine(m.id),
    card, detail, open, close,
    // The catalog's own route: {machine} or {about}, or null for the catalog itself.
    route() {
      const m = /^#\/(?:machine\/([a-z0-9_-]+)|(about)|(linear))?$/.exec(location.hash);
      return m && {machine: m[1] ?? null, about: !!m[2], linear: !!m[3]};
    },
    showAbout(show) {
      const dialog = doc.getElementById('about-veff');
      if (!dialog) return;
      if (show && !dialog.open) dialog.showModal(); else if (!show && dialog.open) dialog.close();
    },
    showGuide(show) {
      const dialog = doc.getElementById('linear-guide');
      if (!dialog) return;
      if (show && !dialog.open) { dialog.showModal(); dialog.scrollTop = 0; } else if (!show && dialog.open) dialog.close();
    },
    isVeff: m => !!veffMachine(m.id),
    // Firmware features every build carries (Linear by Veff), shown as fixed extension cards.
    builtIn: () => (data.features ?? []).map(f => ({name: f.name, summary: f.summary ?? '',
      screen: f.preview === 'linear' ? LINEAR_PREVIEW.gif : null, guide: f.preview === 'linear'})),
    selected: showSelection,
  };
}
