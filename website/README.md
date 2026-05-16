# Sirius Looper — Website

Static marketing + documentation site for [siriuslooper.com](https://siriuslooper.com).
Built with [Eleventy](https://www.11ty.dev/) (11ty). Published to the `gh-pages`
branch by the GitHub Action at `.github/workflows/pages.yml`.

## Local development

Requires Node 20+.

```bash
cd website
npm install
npm run serve          # builds to _site/ and serves on http://localhost:8080
```

Edit any file under `src/` and the dev server reloads.

## Build

```bash
npm run build          # writes static site to _site/
```

`_site/` is git-ignored. The GitHub Action runs the same command and pushes
the contents of `_site/` to the `gh-pages` branch.

## Layout

```
src/
├── _data/site.json       Site-wide config (title, signup endpoint, links)
├── _includes/
│   ├── base.njk          HTML shell, header, footer
│   └── doc.njk           Layout for long-form markdown docs
├── assets/
│   ├── css/site.css      Single hand-written stylesheet
│   ├── js/signup.js      Email-form submit handler + doc TOC builder
│   └── img/              Favicon + (later) logo & OG card
├── docs/                 Three docs synced from /docs at the repo root
│   ├── user-guide.md
│   ├── whitepaper-v1.md
│   └── whitepaper-v2.md
├── index.njk             Home
├── features.njk          Features deep-dive
├── architecture.njk      First-principles overview
├── about.njk             Project + sister-app story
├── 404.njk               Branded 404
├── sitemap.njk           sitemap.xml
├── CNAME                 siriuslooper.com
└── robots.txt
```

## Syncing docs from the repo root

The three files under `src/docs/` are wrapped copies of the canonical docs at
`/docs/` in the repo root. The wrapping adds Eleventy frontmatter (layout,
title, permalink, sourceFile) and strips the leading `# Title` line so the
doc layout's `<h1>` is the only one on the page.

To re-sync after the canonical docs change, re-run the heredoc commands
documented in the website plan at
`/Users/larryseyer/.claude/plans/we-just-purchased-the-logical-raven.md`,
or — preferred — automate via a `scripts/sync-docs.sh` once the format
stabilizes.

## Design tokens

CSS custom properties in `src/assets/css/site.css` mirror OTTO's palette
from `/Users/larryseyer/AudioDevelopment/OTTO/Source/OTTOColours.h`. When
the shared L&F submodule lands across Sirius and OTTO, this file is the
propagation point until that wiring is complete.

Fonts are loaded from Google Fonts in `base.njk` (Orbitron + Inter +
JetBrains Mono). Self-hosting woff2 subsets is a follow-up — tracked in
`/todo.md`.

## Email signup

`src/_data/site.json#signupEndpoint` is the form's POST target. Leave it
empty during development; submissions will show a friendly inline error.

Recommended providers: ConvertKit (free up to 10k subscribers, plain HTML
form) or Formspree (no list — just an inbox; good for v0). Drop the
provider's form-action URL into `signupEndpoint`, redeploy, done.

## Deploy

Push to `master` (or `main`) with a change under `website/**`. The Action
runs and publishes to `gh-pages`. GitHub Pages settings should point at
`gh-pages` branch, `/` folder, custom domain `siriuslooper.com`, Enforce
HTTPS on.

## License

Marketing copy on this site is released under the same AGPLv3 + App Store
exception that covers the rest of the Sirius Looper source tree. The
bundled Larry Seyer Acoustic Drum Library is separately licensed (see
`/docs/SAMPLE-LICENSE.md`).
