(() => {
  const toggle = document.querySelector('[data-menu]');
  const nav = document.getElementById('mobile-nav');
  if (toggle && nav) toggle.addEventListener('click', () => {
    const open = nav.classList.toggle('is-open');
    toggle.setAttribute('aria-expanded', String(open));
  });
})();
