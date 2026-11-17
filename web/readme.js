'use strict';
// Read only documentation shipped with the game; no CDN or GitHub requests.
const documents = new Set(['README.md', 'doc/README.md', 'doc/WALKTHROUGH.md', 'doc/CHANGELOG.md', 'doc/AUDIT.md']);
const siteBase = new URL('.', location.href);
const requested = new URLSearchParams(location.search).get('doc') || 'README.md';
const documentPath = documents.has(requested) ? requested : 'README.md';
const sourceURL = new URL(documentPath, siteBase);
const article = document.getElementById('document');
const docStatus = document.getElementById('doc-status');
async function loadDocument() {
	try {
		const response = await fetch(sourceURL);
		if (!response.ok) throw new Error(`Document HTTP ${response.status}`);
		// These are trusted project files copied at build time, never user input.
		article.innerHTML = marked.parse(await response.text(), {gfm: true});
		for (const image of article.querySelectorAll('img[src]')) {
			image.src = new URL(image.getAttribute('src'), sourceURL).href;
			image.loading = 'lazy';
		}
		for (const link of article.querySelectorAll('a[href]')) {
			const raw = link.getAttribute('href');
			if (raw.startsWith('#')) continue;
			const url = new URL(raw, sourceURL);
			if (url.origin === siteBase.origin && url.pathname.startsWith(siteBase.pathname)) {
				const path = decodeURIComponent(url.pathname.slice(siteBase.pathname.length));
				link.href = documents.has(path)
					? `readme.html?doc=${encodeURIComponent(path)}${url.hash}` : url.href;
			} else if (url.origin === 'https://captain-comic-browser.baltexpo-8341.chatgpt.site') {
				link.href = 'index.html';
			} else {
				link.href = url.href;
				link.target = '_blank';
				link.rel = 'noopener noreferrer';
			}
		}
		const ids = new Set();
		for (const heading of article.querySelectorAll('h1,h2,h3,h4,h5,h6')) {
			const slug = heading.textContent.trim().toLowerCase().replace(/[^\p{L}\p{N}\s_-]/gu, '').replace(/\s+/g, '-') || 'section';
			let id = slug, suffix = 1;
			while (ids.has(id)) id = `${slug}-${suffix++}`;
			ids.add(id); heading.id = id;
		}
		for (const table of article.querySelectorAll('table')) {
			const wrapper = document.createElement('div');
			wrapper.className = 'table-scroll';
			wrapper.tabIndex = 0;
			wrapper.setAttribute('role', 'region');
			wrapper.setAttribute('aria-label', 'Таблица: прокрутите по горизонтали, чтобы увидеть все столбцы');
			table.before(wrapper); wrapper.append(table);
		}
		for (const link of document.querySelectorAll('.docs-nav a')) {
			const path = new URL(link.href).searchParams.get('doc') || 'README.md';
			if (path === documentPath) link.setAttribute('aria-current', 'page');
		}
		const title = article.querySelector('h1');
		document.title = `${title ? title.textContent : 'README'} — Капитан Комик`;
		docStatus.hidden = true;
		if (location.hash) document.getElementById(decodeURIComponent(location.hash.slice(1)))?.scrollIntoView();
	} catch (error) {
		docStatus.textContent = 'Не удалось загрузить описание. Обновите страницу и проверьте, что README.md и каталог doc загружены на хостинг.';
		docStatus.setAttribute('role', 'alert');
		console.error(error);
	}
}
loadDocument();
