(function () {
      const body = document.body;
      const mobileToggle = document.getElementById('mobileToggle');
      const sidebar = document.getElementById('sidebar');
      const backdrop = document.getElementById('backdrop');
      const hudDate = document.getElementById('hudDate');
      const hudTime = document.getElementById('hudTime');
      const hudLatency = document.getElementById('hudLatency');
      const reportsHudFills = document.querySelectorAll('.reports-hud-fill[data-target]');

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

      function animateReportsHud() {
        if (!reportsHudFills.length) return;

        requestAnimationFrame(function () {
          reportsHudFills.forEach(function (fill, index) {
            const target = Number(fill.getAttribute('data-target') || '0');

            setTimeout(function () {
              fill.style.width = target + '%';
            }, index * 120);
          });
        });
      }

      function parseImageLinksFromDirectoryHtml(html, baseDir) {
        const parser = new DOMParser();
        const doc = parser.parseFromString(html, 'text/html');
        const links = Array.from(doc.querySelectorAll('a[href]'));
        const imageExt = /\.(png|jpe?g|gif|webp|bmp|svg)$/i;

        return links
          .map(function (link) {
            return link.getAttribute('href') || '';
          })
          .filter(function (href) {
            return imageExt.test(href);
          })
          .map(function (href) {
            if (/^https?:\/\//i.test(href) || href.startsWith('/')) {
              return href;
            }
            return baseDir + href.replace(/^\.\//, '').replace(/^\/+/, '');
          })
          .filter(function (src, index, arr) {
            return arr.indexOf(src) === index;
          });
      }

      async function loadHeroGallerySources(baseDir) {
        // Try manifest first, then directory listing as fallback.
        try {
          const manifestResponse = await fetch(baseDir + 'manifest.json', { cache: 'no-store' });
          if (manifestResponse.ok) {
            const manifest = await manifestResponse.json();
            if (Array.isArray(manifest)) {
              const imageExt = /\.(png|jpe?g|gif|webp|bmp|svg)$/i;
              const manifestImages = manifest
                .filter(function (name) {
                  return typeof name === 'string' && imageExt.test(name);
                })
                .map(function (name) {
                  return baseDir + name.replace(/^\/+/, '');
                });

              if (manifestImages.length) {
                return manifestImages;
              }
            }
          }
        } catch (error) {
          // ignore and attempt directory listing
        }

        try {
          const listingResponse = await fetch(baseDir, { cache: 'no-store' });
          if (!listingResponse.ok) return [];
          const html = await listingResponse.text();
          return parseImageLinksFromDirectoryHtml(html, baseDir);
        } catch (error) {
          return [];
        }
      }

      async function initHomeHeroRotation() {
        const heroImage = document.querySelector('.home-hero-image');
        if (!heroImage) return;

        const defaultSrc = heroImage.getAttribute('src') || '';
        const baseDir = 'img/screenshots/';
        const discovered = await loadHeroGallerySources(baseDir);
        const images = discovered.length ? discovered : [defaultSrc];

        if (images.length <= 1) return;

        const rotationMs = 3000;
        const fadeMs = 500;
        const recentRotationLimit = 10;
        const recentImages = [];
        let isTransitioning = false;

        heroImage.style.transition = 'opacity ' + fadeMs + 'ms ease';
        heroImage.style.opacity = '1';

        setInterval(function () {
          if (isTransitioning) return;

          const current = heroImage.getAttribute('src') || '';
          let pool = images.filter(function (src) {
            return src !== current && !recentImages.includes(src);
          });

          if (!pool.length) {
            pool = images.filter(function (src) {
              return src !== current;
            });
          }

          if (!pool.length) {
            pool = images;
          }

          const next = pool[Math.floor(Math.random() * pool.length)];

          const preloaded = new Image();
          preloaded.onload = function () {
            isTransitioning = true;
            heroImage.style.opacity = '0';

            setTimeout(function () {
              heroImage.src = next;
              recentImages.push(next);
              if (recentImages.length > recentRotationLimit) {
                recentImages.shift();
              }
              heroImage.style.opacity = '1';

              setTimeout(function () {
                isTransitioning = false;
              }, fadeMs);
            }, fadeMs);
          };

          preloaded.onerror = function () {
            // If preload fails, skip this cycle and try a new image next rotation.
          };

          preloaded.src = next;
        }, rotationMs);
      }

      updateHudClock();
      updateLatency();
      animateReportsHud();
      initHomeHeroRotation();
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
