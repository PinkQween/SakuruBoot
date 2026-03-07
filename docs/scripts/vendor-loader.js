// Dynamically load marked + DOMPurify then resolve a promise
// so docs.js can await both before rendering markdown.
window.SakuruVendorReady = new Promise((resolve) => {
    const scripts = [
        'https://cdnjs.cloudflare.com/ajax/libs/marked/9.1.6/marked.min.js',
        'https://cdnjs.cloudflare.com/ajax/libs/dompurify/3.0.8/purify.min.js',
    ];

    let loaded = 0;
    scripts.forEach((src) => {
        const s = document.createElement('script');
        s.src = src;
        s.onload = () => { if (++loaded === scripts.length) resolve(); };
        s.onerror = () => { if (++loaded === scripts.length) resolve(); };
        document.head.appendChild(s);
    });
});
