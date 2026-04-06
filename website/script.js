(function () {
      const body = document.body;
      const mobileToggle = document.getElementById('mobileToggle');
      const sidebar = document.getElementById('sidebar');
      const backdrop = document.getElementById('backdrop');
      const hudDate = document.getElementById('hudDate');
      const hudTime = document.getElementById('hudTime');
      const hudLatency = document.getElementById('hudLatency');

      function openNav() {
        body.classList.add('nav-open');
        mobileToggle.setAttribute('aria-expanded', 'true');
        mobileToggle.querySelector('.material-symbols-outlined').textContent = 'close';
      }

      function closeNav() {
        body.classList.remove('nav-open');
        mobileToggle.setAttribute('aria-expanded', 'false');
        mobileToggle.querySelector('.material-symbols-outlined').textContent = 'menu';
      }

      function toggleNav() {
        body.classList.contains('nav-open') ? closeNav() : openNav();
      }

      mobileToggle?.addEventListener('click', toggleNav);
      backdrop?.addEventListener('click', closeNav);

      document.addEventListener('keydown', function (event) {
        if (event.key === 'Escape') closeNav();
      });

      window.addEventListener('resize', function () {
        if (window.innerWidth > 980) closeNav();
      });

      function updateHudClock() {
        const now = new Date();
        hudDate.textContent = now.toLocaleDateString(undefined, {
          month: 'short',
          day: '2-digit',
          year: 'numeric'
        }).replace(',', '');

        hudTime.textContent = now.toLocaleTimeString(undefined, {
          hour: '2-digit',
          minute: '2-digit',
          second: '2-digit',
          hour12: false
        });
      }

      function updateLatency() {
        const value = 14 + Math.floor(Math.random() * 10);
        hudLatency.textContent = value + 'ms';
      }

      updateHudClock();
      updateLatency();
      setInterval(updateHudClock, 1000);
      setInterval(updateLatency, 2500);

      // For project spec schedule table
      const weekExpanders = document.querySelectorAll('.week-expander');

      function openExpander(button, row) {
        const outer = row.querySelector('.week-expander__content-outer');
        if (!outer) return;

        row.setAttribute('aria-hidden', 'false');
        button.setAttribute('aria-expanded', 'true');

        outer.style.height = '0px';
        const fullHeight = outer.scrollHeight;
        outer.style.height = fullHeight + 'px';

        outer.addEventListener(
          'transitionend',
          function handleOpen(e) {
            if (e.propertyName !== 'height') return;
            if (button.getAttribute('aria-expanded') === 'true') {
              outer.style.height = 'auto';
            }
            outer.removeEventListener('transitionend', handleOpen);
          },
          { once: true }
        );
      }

      function closeExpander(button, row) {
        const outer = row.querySelector('.week-expander__content-outer');
        if (!outer) return;

        outer.style.height = outer.scrollHeight + 'px';
        outer.offsetHeight; // force reflow
        outer.style.height = '0px';

        button.setAttribute('aria-expanded', 'false');

        outer.addEventListener(
          'transitionend',
          function handleClose(e) {
            if (e.propertyName !== 'height') return;
            row.setAttribute('aria-hidden', 'true');
            outer.removeEventListener('transitionend', handleClose);
          },
          { once: true }
        );
      }

      weekExpanders.forEach(function (button) {
        button.addEventListener('click', function () {
          const contentId = button.getAttribute('aria-controls');
          const row = document.getElementById(contentId);
          const isExpanded = button.getAttribute('aria-expanded') === 'true';

          if (!row) return;

          if (isExpanded) {
            closeExpander(button, row);
            return;
          }

          // accordion behavior
          weekExpanders.forEach(function (otherButton) {
            if (otherButton === button) return;
            const otherId = otherButton.getAttribute('aria-controls');
            const otherRow = document.getElementById(otherId);
            if (otherRow && otherButton.getAttribute('aria-expanded') === 'true') {
              closeExpander(otherButton, otherRow);
            }
          });

          openExpander(button, row);
        });
      });

    })();
