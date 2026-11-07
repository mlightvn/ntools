# nexplorer.vietkeys.com

Landing page of nExplorer: static files for Vercel, in English (`/`), Vietnamese (`/vi/`), Japanese (`/ja/`) and Chinese (`/zh/`). Node 22, no packages.

| Path | What |
|---|---|
| `strings.mjs` | All page texts in the 4 languages, and the themes of the screenshot switcher |
| `build.mjs` | Writes `public/`: pages, `download/nexplorer.exe` + `SHA256SUMS.txt`, sitemap, robots, and `vercel.json` |
| `release.json` | Version and date shown on the page (size and SHA-256 come from the exe) |
| `static/` | `style.css` (Nexus look; the accent follows the theme shown), `app.js` (theme switcher, remembered language), `icon.svg`, `og.png`, `img/shot-<theme>.png` (1386 × 843, demo folders only) |
| `og.html` | Source of `static/og.png` (render it with headless Chrome, command inside) |

## Release

1. `build.bat` (makes `..\nexplorer.exe`), then set `version` / `date` in `release.json`.
2. `node site/build.mjs`
3. Deploy with the Vercel CLI from this folder: `npx vercel --prod` (first time: `npx vercel login`, `npx vercel` and create a project, e.g. `nexplorer`). The exe is not in git (`.gitignore` excludes `*.exe`), so a Git-connected Vercel project would serve the pages without the download; `.vercelignore` keeps the sources out of the upload.
4. Domain: in the Vercel project add `nexplorer.vietkeys.com`, then the DNS record Vercel shows (a CNAME for `nexplorer`) where vietkeys.com's DNS is managed.

Never put `nexplorer.ini` in the download: the app creates its own.
