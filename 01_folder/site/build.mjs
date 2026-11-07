// Builds nexplorer.vietkeys.com into site/public (static files for Vercel): one page per language
// (/ = English, /vi/, /ja/, /zh/), the download (../nexplorer.exe as built by build.bat), sitemap,
// robots and vercel.json. Node 22, no packages. Run after build.bat:
//   node site/build.mjs
// The version and date shown come from site/release.json (edit them for a new release); size and
// SHA-256 are read from the exe.
import { createHash } from 'node:crypto';
import { cpSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { LANGS, T, THEMES } from './strings.mjs';

const SITE = dirname(fileURLToPath(import.meta.url));
const APP = join(SITE, '..');
const PUBLIC = join(SITE, 'public');
const ORIGIN = 'https://nexplorer.vietkeys.com';
const AUTHOR = { name: 'Nguyen Ngoc Nam', url: 'https://www.linkedin.com/in/nguyenngocnam/' };
const EXE = 'nexplorer.exe';
const REPO = 'https://github.com/mlightvn/ntools';
const LICENSE_URL = `${REPO}/blob/main/LICENSE`;

// ---- release facts ----
const meta = JSON.parse(readFileSync(join(SITE, 'release.json'), 'utf8'));
const exePath = join(APP, EXE);
if (!existsSync(exePath)) throw new Error(`${exePath} not found: run build.bat first`);
const exe = readFileSync(exePath);
const release = { ...meta, size: exe.length, sha256: createHash('sha256').update(exe).digest('hex') };
const mb = (bytes) => `${(bytes / 1048576).toFixed(1)} MB`;

// ---- output folder ----
rmSync(PUBLIC, { recursive: true, force: true });
mkdirSync(join(PUBLIC, 'assets'), { recursive: true });
mkdirSync(join(PUBLIC, 'download'), { recursive: true });
writeFileSync(join(PUBLIC, 'download', EXE), exe);
writeFileSync(join(PUBLIC, 'download', 'SHA256SUMS.txt'), `${release.sha256}  ${EXE}\n`);

// ---- static files: CSS and JS get a content hash in their URL, so /assets/ can be cached for good ----
const version = {};
for (const f of ['style.css', 'app.js']) {
  const data = readFileSync(join(SITE, 'static', f));
  writeFileSync(join(PUBLIC, 'assets', f), data);
  version[f] = createHash('sha256').update(data).digest('hex').slice(0, 10);
}
const shots = THEMES.filter(([id]) => existsSync(join(SITE, 'static', 'img', `shot-${id}.png`)));
if (!shots.length) throw new Error('No screenshots in site/static/img (shot-<theme>.png)');
cpSync(join(SITE, 'static', 'img'), join(PUBLIC, 'img'), { recursive: true });
cpSync(join(SITE, 'static', 'icon.svg'), join(PUBLIC, 'icon.svg'));
if (existsSync(join(SITE, 'static', 'og.png'))) cpSync(join(SITE, 'static', 'og.png'), join(PUBLIC, 'og.png'));
cpSync(join(APP, 'res', 'app.ico'), join(PUBLIC, 'favicon.ico'));

// ---- helpers ----
const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
const pathOf = (l) => (l === 'en' ? '/' : `/${l}/`);
// Links inside the pages are relative, so the site also works from a sub-folder (VS Code Live Server)
const baseOf = (l) => (l === 'en' ? '' : '../');
const linkTo = (from, to) => baseOf(from) + (to === 'en' ? '' : `${to}/`) || './';
const fill = (s) => s.replaceAll('{ver}', release.version).replaceAll('{date}', release.date).replaceAll('{size}', mb(release.size));
const keys = (combo) => combo.split(' · ').map((part) => part.split(' / ').map((k) => `<kbd>${esc(k)}</kbd>`).join(' / ')).join(' · ');

const ICON = {
  logo: '<svg viewBox="0 0 64 64" aria-hidden="true"><path d="M32 3 57 17.5v29L32 61 7 46.5v-29Z" fill="#061016" stroke="currentColor" stroke-width="4" stroke-linejoin="round"/><path d="M18 21h10l3 4h15v18H18Z" fill="currentColor" fill-opacity=".28" stroke="currentColor" stroke-width="2.5" stroke-linejoin="round"/><path d="M18 29h28" stroke="currentColor" stroke-width="2"/></svg>',
  download: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 3v12m0 0-5-5m5 5 5-5M4 19h16"/></svg>',
  code: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="m8 7-5 5 5 5M16 7l5 5-5 5M14 4l-4 16"/></svg>',
  check: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="m5 12 5 5 9-10"/></svg>',
};
// One icon per feature, in the order of T[lang].features.items
const FEATURE_ICONS = [
  '<rect x="3" y="4" width="18" height="16" rx="1.5"/><path d="M9 4v16M15 4v16"/>', // panes
  '<circle cx="11" cy="11" r="6.5"/><path d="m20 20-4.2-4.2"/>', // search
  '<path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13"/><path d="m10 11 1 5m3-5-1 5"/>', // delete
  '<path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1"/><path d="M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1"/>', // link
  '<path d="M5 4h10l4 4v12H5Z"/><path d="M11 4v2h2v2h-2v2h2v2h-2v3h2"/>', // zip
  '<path d="M4 4h6v16H4Z"/><path d="M13 7h7M13 11h7M13 15h5"/>', // sidebar
  '<path d="M4 17h12M4 7h16"/><path d="M4 12h16" stroke-dasharray="2 2"/><circle cx="19" cy="17" r="2"/>', // progress
  '<path d="M12 3 20 7.5v9L12 21l-8-4.5v-9Z"/><path d="M12 3v18M4 7.5l16 9M20 7.5l-16 9" opacity=".5"/>', // themes (hexagon)
].map((p) => `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${p}</svg>`);

const langSwitch = (l) =>
  `<nav class="langs" aria-label="${esc(T[l].footer.languages)}">` +
  LANGS.map((o) => `<a href="${linkTo(l, o)}" hreflang="${T[o].htmlLang}" lang="${T[o].htmlLang}" data-lang="${o}"${o === l ? ' aria-current="true"' : ''}>${esc(T[o].name)}</a>`).join('') +
  '</nav>';

function page(l) {
  const t = T[l];
  const base = baseOf(l);
  const exeUrl = `${base}download/${EXE}`;
  const ld = {
    '@context': 'https://schema.org',
    '@type': 'SoftwareApplication',
    name: 'nExplorer',
    description: t.description,
    operatingSystem: 'Windows 10, Windows 11',
    applicationCategory: 'UtilitiesApplication',
    softwareVersion: release.version,
    datePublished: release.date,
    fileSize: mb(release.size),
    downloadUrl: `${ORIGIN}/download/${EXE}`,
    url: ORIGIN + pathOf(l),
    offers: { '@type': 'Offer', price: '0', priceCurrency: 'USD' },
    license: 'https://opensource.org/licenses/MIT',
    sameAs: [REPO],
    author: { '@type': 'Person', ...AUTHOR },
    image: `${ORIGIN}/og.png`,
  };
  // On / only: a browser that prefers Vietnamese, Japanese or Chinese goes to that page,
  // unless the visitor picked a language on the site before (app.js remembers it)
  const redirect =
    l === 'en'
      ? `<script>try{if(!localStorage.getItem('nx-lang')){var b=((navigator.languages&&navigator.languages[0])||navigator.language||'').toLowerCase().slice(0,2);if(b==='vi'||b==='ja'||b==='zh')location.replace(b+'/'+location.hash)}}catch(e){}</script>\n`
      : '';
  const dl = (cls = 'btn') =>
    `<a class="${cls}" href="${exeUrl}" download>${ICON.download}<span class="two">${esc(t.hero.download)}<span class="btn-sub">${esc(fill(t.hero.sub))}</span></span></a>`;

  return `<!doctype html>
<html lang="${t.htmlLang}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
${redirect}<title>${esc(t.title)}</title>
<meta name="description" content="${esc(t.description)}">
<link rel="canonical" href="${ORIGIN}${pathOf(l)}">
${LANGS.map((o) => `<link rel="alternate" hreflang="${T[o].htmlLang}" href="${ORIGIN}${pathOf(o)}">`).join('\n')}
<link rel="alternate" hreflang="x-default" href="${ORIGIN}/">
<meta name="theme-color" content="#02060a">
<link rel="icon" href="${base}favicon.ico" sizes="any">
<link rel="icon" href="${base}icon.svg" type="image/svg+xml">
<meta property="og:type" content="website">
<meta property="og:site_name" content="nExplorer">
<meta property="og:title" content="${esc(t.title)}">
<meta property="og:description" content="${esc(t.description)}">
<meta property="og:url" content="${ORIGIN}${pathOf(l)}">
<meta property="og:image" content="${ORIGIN}/og.png">
<meta property="og:image:width" content="1200">
<meta property="og:image:height" content="630">
<meta property="og:locale" content="${t.ogLocale}">
<meta name="twitter:card" content="summary_large_image">
<link rel="stylesheet" href="${base}assets/style.css?v=${version['style.css']}">
<script src="${base}assets/app.js?v=${version['app.js']}" defer></script>
<script type="application/ld+json">${JSON.stringify(ld).replace(/</g, '\\u003c')}</script>
</head>
<body>
<a class="skip" href="#main">${esc(t.skip)}</a>
<header class="top">
  <div class="wrap">
    <a class="brand" href="${linkTo(l, l)}" aria-label="nExplorer" style="color:var(--text)"><span style="color:var(--accent);display:flex">${ICON.logo}</span>nExplorer<small>v${esc(release.version)}</small></a>
    <nav class="nav" aria-label="nExplorer">
      <a href="#features">${esc(t.nav.features)}</a>
      <a href="#search">${esc(t.nav.search)}</a>
      <a href="#keys">${esc(t.nav.keys)}</a>
      <a href="#install">${esc(t.nav.install)}</a>
      <a href="#faq">${esc(t.nav.faq)}</a>
    </nav>
    <div class="top-end">
      ${langSwitch(l)}
      <a class="btn small" href="${exeUrl}" download>${esc(t.nav.download)}</a>
    </div>
  </div>
</header>

<main id="main">
<section class="hero" id="download">
  <div class="wrap center">
    <span class="eyebrow">${esc(t.hero.eyebrow)}</span>
    <h1>${t.hero.h1}</h1>
    <p class="lead">${t.hero.lead}</p>
    <div class="ctas">${dl()}<a class="btn ghost" href="${REPO}" rel="noopener">${ICON.code}<span class="two">${esc(t.hero.source)}<span class="btn-sub">${esc(t.hero.sourceSub)}</span></span></a></div>
    <p class="meta">${esc(fill(t.hero.meta))}</p>
    <ul class="trust">${t.hero.trust.map((s) => `<li>${ICON.check}${esc(fill(s))}</li>`).join('')}</ul>
    <div class="panel showcase" data-showcase>
      <div class="showcase-bar">
        <span class="label">${esc(t.hero.themes)}</span>
        <div class="seg" role="group" aria-label="${esc(t.hero.themes)}">
${shots.map(([id, name, accent], i) => `          <button type="button" data-theme="${id}" data-accent="${accent}" aria-pressed="${i === 0}"><span class="dot" style="background:${accent}"></span>${esc(name)}</button>`).join('\n')}
        </div>
      </div>
      <div class="frame">
${shots.map(([id, name], i) => `        <img data-shot="${id}" src="${base}img/shot-${id}.png" width="1386" height="843" alt="${esc(t.hero.alt.replace('{theme}', name))}"${i === 0 ? ' class="on" fetchpriority="high"' : ' loading="lazy"'} decoding="async">`).join('\n')}
      </div>
    </div>
  </div>
</section>

<section class="stats" aria-label="nExplorer">
  <div class="wrap">
${t.stats.map(([n, s]) => `    <div class="panel stat"><b>${esc(fill(n))}</b><span>${esc(fill(s))}</span></div>`).join('\n')}
  </div>
</section>

<section id="features">
  <div class="wrap">
    <p class="kicker">${esc(t.features.kicker)}</p>
    <h2>${esc(t.features.h2)}</h2>
    <div class="grid">
${t.features.items.map(([h, p], i) => `      <article class="panel feature"><div class="ico">${FEATURE_ICONS[i]}</div><h3>${esc(h)}</h3><p>${fill(p)}</p></article>`).join('\n')}
    </div>
  </div>
</section>

<section id="search">
  <div class="wrap">
    <div class="tables">
      <div class="panel">
        <p class="kicker">${esc(t.search.kicker)}</p>
        <h3>${esc(t.search.h2)}</h3>
        <p class="section-lead">${t.search.p}</p>
        <table><thead><tr><th scope="col">${esc(t.search.you)}</th><th scope="col">${esc(t.search.finds)}</th></tr></thead>
        <tbody>${t.search.rows.map(([k, v]) => `<tr><td><code>${esc(k)}</code></td><td>${v}</td></tr>`).join('')}</tbody></table>
      </div>
      <div class="panel">
        <p class="kicker">${esc(t.clip.kicker)}</p>
        <h3>${esc(t.clip.h2)}</h3>
        <table><thead><tr><th scope="col">${esc(t.clip.to)}</th><th scope="col">${esc(t.clip.get)}</th></tr></thead>
        <tbody>${t.clip.rows.map(([k, v]) => `<tr><td>${esc(k)}</td><td>${esc(v)}</td></tr>`).join('')}</tbody></table>
      </div>
    </div>
  </div>
</section>

<section id="keys">
  <div class="wrap">
    <p class="kicker">${esc(t.keys.kicker)}</p>
    <h2>${esc(t.keys.h2)}</h2>
    <div class="keys-grid">
${t.keys.rows.map(([k, v]) => `      <div><b class="k">${keys(k)}</b><span>${esc(v)}</span></div>`).join('\n')}
    </div>
  </div>
</section>

<section id="install">
  <div class="wrap">
    <p class="kicker">${esc(t.install.kicker)}</p>
    <h2>${esc(t.install.h2)}</h2>
    <ol class="steps">
${t.install.steps.map(([h, p]) => `      <li><div class="panel"><h3>${esc(h)}</h3><p>${fill(p)}</p></div></li>`).join('\n')}
    </ol>
    <div class="notes">
${[t.install.smartscreen, t.install.remove, t.install.reset].map(([h, p]) => `      <div class="note-card"><h3>${esc(h)}</h3><p>${p}</p></div>`).join('\n')}
    </div>
    <details class="sums">
      <summary>${esc(t.install.sums)}</summary>
      <pre>${esc(release.sha256)}  ${EXE}</pre>
    </details>
  </div>
</section>

<section id="faq">
  <div class="wrap">
    <p class="kicker">${esc(t.faq.kicker)}</p>
    <h2>${esc(t.faq.h2)}</h2>
    <div class="faq">
${t.faq.items.map(([q, a]) => `      <details><summary>${esc(q)}</summary><p>${a.replaceAll('{repo}', REPO)}</p></details>`).join('\n')}
    </div>
  </div>
</section>

<section>
  <div class="wrap">
    <div class="panel final">
      <h2>${esc(t.final.h2)}</h2>
      <p>${esc(t.final.p)}</p>
      <div class="ctas">${dl()}</div>
    </div>
  </div>
</section>
</main>

<footer>
  <div class="wrap">
    <p>© ${release.date.slice(0, 4)} nExplorer · ${esc(t.footer.by)} · <a href="${AUTHOR.url}" rel="author">LinkedIn</a> · <a href="${LICENSE_URL}" rel="license">${esc(t.footer.license)}</a> · <a href="${REPO}">${esc(t.footer.source)}</a> · ${esc(t.footer.also)}: <a href="https://vietkeys.com${pathOf(l)}">VietKeys</a></p>
    ${langSwitch(l)}
  </div>
</footer>
</body>
</html>
`;
}

// ---- pages ----
for (const l of LANGS) {
  const dir = l === 'en' ? PUBLIC : join(PUBLIC, l);
  mkdirSync(dir, { recursive: true });
  writeFileSync(join(dir, 'index.html'), page(l));
}

// Not found: a small page in all four languages, linking home (served by Vercel at any path, so absolute links)
writeFileSync(
  join(PUBLIC, '404.html'),
  `<!doctype html>
<html lang="en">
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>nExplorer — 404</title>
<meta name="robots" content="noindex"><link rel="icon" href="/favicon.ico" sizes="any"><link rel="stylesheet" href="/assets/style.css?v=${version['style.css']}"></head>
<body><main class="wrap" style="padding:120px 0;text-align:center">
<a class="brand" href="/" style="justify-content:center;color:var(--text)"><span style="color:var(--accent);display:flex">${ICON.logo}</span>nExplorer</a>
<h1 style="margin:28px 0 12px;font-size:56px">404</h1>
<p style="color:var(--soft)">Page not found · Không tìm thấy trang · ページが見つかりません · 找不到页面</p>
<p style="margin-top:28px">${LANGS.map((o) => `<a href="${pathOf(o)}" lang="${T[o].htmlLang}">${esc(T[o].name)}</a>`).join(' · ')}</p>
</main></body></html>
`,
);

// ---- sitemap, robots ----
writeFileSync(
  join(PUBLIC, 'sitemap.xml'),
  `<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9" xmlns:xhtml="http://www.w3.org/1999/xhtml">
${LANGS.map(
  (l) => `  <url>
    <loc>${ORIGIN}${pathOf(l)}</loc>
    <lastmod>${release.date}</lastmod>
${LANGS.map((o) => `    <xhtml:link rel="alternate" hreflang="${T[o].htmlLang}" href="${ORIGIN}${pathOf(o)}"/>`).join('\n')}
    <xhtml:link rel="alternate" hreflang="x-default" href="${ORIGIN}/"/>
  </url>`,
).join('\n')}
</urlset>
`,
);
writeFileSync(join(PUBLIC, 'robots.txt'), `User-agent: *\nAllow: /\n\nSitemap: ${ORIGIN}/sitemap.xml\n`);

// ---- vercel.json: static output, cache and security headers ----
const vercel = {
  $schema: 'https://openapi.vercel.sh/vercel.json',
  framework: null,
  outputDirectory: 'public',
  redirects: [{ source: '/en', destination: '/', permanent: true }],
  headers: [
    {
      source: '/(.*)',
      headers: [
        { key: 'X-Content-Type-Options', value: 'nosniff' },
        { key: 'Referrer-Policy', value: 'strict-origin-when-cross-origin' },
        { key: 'X-Frame-Options', value: 'DENY' },
        { key: 'Permissions-Policy', value: 'camera=(), microphone=(), geolocation=()' },
      ],
    },
    { source: '/assets/(.*)', headers: [{ key: 'Cache-Control', value: 'public, max-age=31536000, immutable' }] },
    { source: '/download/(.*)', headers: [{ key: 'Cache-Control', value: 'public, max-age=3600' }] },
  ],
};
writeFileSync(join(SITE, 'vercel.json'), JSON.stringify(vercel, null, 2) + '\n');

console.log(`  site ${LANGS.map(pathOf).join(' ')} -> ${PUBLIC} (nExplorer ${release.version}, ${mb(release.size)}, ${shots.length} screenshots)`);
