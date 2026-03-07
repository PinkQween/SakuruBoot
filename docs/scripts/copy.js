// Copy-to-clipboard buttons for <pre> blocks
function attachCopyButtons(root) {
    root = root || document;
    root.querySelectorAll('pre[class*="language-"]').forEach((pre) => {
        if (pre.parentElement && pre.parentElement.classList.contains('copy-wrapper')) return;

        const wrapper = document.createElement('div');
        wrapper.className = 'copy-wrapper';
        pre.parentNode.insertBefore(wrapper, pre);
        wrapper.appendChild(pre);

        const btn = document.createElement('button');
        btn.className = 'copy-btn unselect';
        btn.title = 'Copy';
        btn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg>';
        wrapper.appendChild(btn);

        btn.addEventListener('click', () => {
            const text = pre.querySelector('code')?.innerText || pre.innerText;
            navigator.clipboard.writeText(text).then(() => {
                btn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"/></svg>';
                setTimeout(() => {
                    btn.innerHTML = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg>';
                }, 1500);
            });
        });
    });
}

window.attachCopyButtons = attachCopyButtons;
