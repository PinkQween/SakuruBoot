document.addEventListener('DOMContentLoaded', () => {
    const contentEl = document.getElementById('docs-content');
    const navLinks  = document.querySelectorAll('.docs-nav-link');

    if (!contentEl) return;

    const slugify = (text) =>
        text.toLowerCase().trim()
            .replace(/[^a-z0-9\s-]/g, '')
            .replace(/\s+/g, '-')
            .replace(/-+/g, '-');

    const setActive = (link) => {
        navLinks.forEach((l) => l.classList.remove('is-active'));
        if (link) link.classList.add('is-active');
    };

    const renderMarkdown = (md) => {
        if (!window.marked || !window.DOMPurify) {
            contentEl.innerHTML = '<p>Markdown renderer is unavailable.</p>';
            return;
        }

        marked.setOptions({ gfm: true, breaks: false });

        const safeHtml = DOMPurify.sanitize(marked.parse(md.trim()));
        contentEl.innerHTML = safeHtml;

        // external links
        contentEl.querySelectorAll('a[href^="http://"], a[href^="https://"]').forEach((a) => {
            a.target = '_blank';
            a.rel    = 'noopener noreferrer';
            a.classList.add('external-link');
        });

        // heading IDs
        contentEl.querySelectorAll('h1, h2, h3, h4, h5, h6').forEach((h) => {
            if (!h.id) h.id = slugify(h.textContent);
        });

        // syntax highlighting
        if (window.Prism) {
            if (!Prism.languages.c_sakuru) {
                // Re-use C grammar under a local alias for sakuru.cfg
                Prism.languages.sakuruconf = {
                    'comment': /;.*/,
                    'string':  { pattern: /"[^"]*"/, greedy: true },
                    'keyword': /\b(?:entry|default|timeout|title|kernel|initrd|cmdline|encrypted|luks_keyfile|luks_tries)\b/,
                    'boolean': /\b(?:yes|no|true|false)\b/,
                    'number':  /\b\d+\b/,
                    'operator': /[=]/,
                };
            }
            Prism.highlightAllUnder(contentEl);
        }

        if (window.attachCopyButtons) window.attachCopyButtons(contentEl);
    };

    const loadDoc = (slug) => {
        contentEl.innerHTML = '<p style="color:rgb(120,120,120);padding:8px 0">Loading…</p>';

        // Resolve relative to docs root regardless of current page depth
        const base = document.querySelector('meta[name="docs-root"]')?.content || './';
        const path = `${base}pages/docs/${slug}.md`;

        return fetch(path)
            .then((r) => { if (!r.ok) throw new Error(r.status); return r.text(); })
            .then((text) => renderMarkdown(text))
            .catch(() => {
                contentEl.innerHTML = '<p>Unable to load this section right now.</p>';
            });
    };

    const initialSlug = (() => {
        const hash = window.location.hash.slice(1);
        if (hash && document.querySelector(`.docs-nav-link[data-doc="${hash}"]`)) return hash;
        return navLinks[0]?.dataset.doc || 'getting-started';
    })();

    navLinks.forEach((link) => {
        link.addEventListener('click', (e) => {
            e.preventDefault();
            const slug = link.dataset.doc;
            history.pushState(null, '', `#${slug}`);
            setActive(link);
            window.SakuruVendorReady.then(() => loadDoc(slug));
        });
    });

    window.addEventListener('popstate', () => {
        const slug = window.location.hash.slice(1) || navLinks[0]?.dataset.doc || 'getting-started';
        setActive(document.querySelector(`.docs-nav-link[data-doc="${slug}"]`));
        window.SakuruVendorReady.then(() => loadDoc(slug));
    });

    const initLink = document.querySelector(`.docs-nav-link[data-doc="${initialSlug}"]`);
    setActive(initLink);

    window.SakuruVendorReady = window.SakuruVendorReady || Promise.resolve();
    window.SakuruVendorReady.then(() => loadDoc(initialSlug));
});
