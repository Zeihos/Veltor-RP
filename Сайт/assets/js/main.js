(() => {
  const toggle = document.querySelector('[data-menu-toggle]');
  const nav = document.querySelector('#site-nav');
  if (toggle && nav) {
    toggle.addEventListener('click', () => {
      const opened = nav.classList.toggle('open');
      toggle.setAttribute('aria-expanded', String(opened));
      toggle.setAttribute('aria-label', opened ? 'Закрыть меню' : 'Открыть меню');
    });
    nav.querySelectorAll('a').forEach(a => a.addEventListener('click', () => {
      nav.classList.remove('open');
      toggle.setAttribute('aria-expanded','false');
    }));
    document.addEventListener('keydown', event => {
      if (event.key === 'Escape') { nav.classList.remove('open'); toggle.setAttribute('aria-expanded','false'); }
    });
  }
  document.querySelectorAll('[data-year]').forEach(el => el.textContent=String(new Date().getFullYear()));
  document.querySelectorAll('[data-copy]').forEach(el => el.addEventListener('click', async () => {
    const value = el.getAttribute('data-copy') || '';
    if (!navigator.clipboard || !window.isSecureContext) {
      const node=document.createElement('textarea');node.value=value;node.style.position='fixed';node.style.opacity='0';document.body.appendChild(node);node.select();
      try { document.execCommand('copy'); el.textContent='Скопировано ✓'; } catch { el.textContent='Выделите ссылку'; } finally {node.remove();}
    } else {
      try { await navigator.clipboard.writeText(value); el.textContent='Скопировано ✓'; } catch { el.textContent='Не удалось скопировать'; }
    }
    window.setTimeout(()=>{el.textContent='Скопировать';},2400);
  }));
})();
// Confirm destructive actions.
document.querySelectorAll('form[data-confirm]').forEach(form => {
  form.addEventListener('submit', event => {
    if (!window.confirm(form.dataset.confirm || 'Продолжить?')) event.preventDefault();
  });
});
