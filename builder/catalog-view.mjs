import VEFF from './veff.json' with { type: 'json' };

// The Veff Experimental presentation of the machine collection: a browsable grid of
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
  function renderBase(version = base?.version) {
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
        card.append(node('span', 'Built into this firmware', 'veff-kicker'), node('h3', feature.name),
          node('p', feature.summary ?? ''));
        if (target.dataset.veffFeatures === 'full') for (const text of feature.help ?? []) card.append(node('p', text, 'veff-feature-help'));
        if (feature.note) card.append(node('p', feature.note, 'veff-feature-note'));
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
    if (credit && author) credit.textContent = (/\bby \S+$/i.test(m.name) ? '' : 'by ') + author;
    button.prepend(screen(m));
    const tags = node('span', undefined, 'card-tags');
    tags.append(node('span', KINDS[m.kind] ?? m.kind, 'card-kind'));
    if (ex) tags.append(badge());
    button.querySelector('.machine-desc')?.before(tags);
    if (m.keywords?.length) button.append(node('span', m.keywords.join(' · '), 'card-keywords'));
    button.setAttribute('aria-label', m.name + (author ? ' by ' + author : '') + ', ' +
      (KINDS[m.kind] ?? m.kind) + (ex ? ', a Veff machine' : '') + '. Show details');
    button.addEventListener('click', () => open());
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
          link(data.fork?.name ?? 'Veff Experimental', data.fork?.repository ?? 'https://github.com/galfaroth/open-xy'));
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

  const workspace = () => $('machine-workspace');
  const isOpen = () => workspace()?.dataset.view === 'detail';
  function open() {
    const area = workspace();
    if (!area || isOpen()) return;
    returnScroll = window.scrollY;
    area.dataset.view = 'detail';
    $('preview-controls-panel').open = true;
    area.scrollIntoView?.({block: 'start'});
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

  $('detail-back').onclick = close;
  $('detail-select').onclick = () => { if (current) { setSelected(current.id, !isSelected(current.id)); showSelection(); } };
  doc.addEventListener('keydown', event => {
    if (event.key !== 'Escape' || !isOpen() || $('selected-previews')?.open || $('machine-presets')?.open) return;
    if (workspace().closest('[data-page]')?.hidden) return;
    close();
  });
  for (const id of ['veff-intro', 'veff-featured']) if ($(id)) $(id).hidden = false;
  doc.documentElement.dataset.veff = 'true';
  renderBase(); renderFeatures();

  return {
    loaded(kit) { renderBase(kit.version); },
    // Veff machines start unselected, like community machines.
    chosenByDefault: m => !veffMachine(m.id),
    card, detail, open, close,
    selected: showSelection,
  };
}
