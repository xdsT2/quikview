(function () {
  "use strict";

  var REPO_OWNER = "xdsT2";
  var REPO_NAME = "soleview";
  var REPO_URL = "https://github.com/" + REPO_OWNER + "/" + REPO_NAME;
  var RELEASES_API_URL = "https://api.github.com/repos/" + REPO_OWNER + "/" + REPO_NAME + "/releases?per_page=20";
  var RAW_CHANGELOG_URL = "https://raw.githubusercontent.com/" + REPO_OWNER + "/" + REPO_NAME + "/main/CHANGELOG.md";
  var JSDELIVR_CHANGELOG_URL = "https://cdn.jsdelivr.net/gh/" + REPO_OWNER + "/" + REPO_NAME + "@main/CHANGELOG.md";
  var CACHE_KEY = "sv-changelog-cache-v1";
  var CACHE_TTL_MS = 15 * 60 * 1000;
  var INITIAL_VISIBLE_RELEASES = 5;

  var root = document.documentElement;
  root.classList.add("js-anim");

  var header = document.querySelector(".site-header");
  var nav = document.getElementById("nav");
  var navToggle = document.getElementById("nav-toggle");
  var themeToggle = document.getElementById("theme-toggle");
  var themeMeta = document.querySelector('meta[name="theme-color"]');
  var toastEl = document.getElementById("toast");

  /* ==========================================================================
     Toast Helper
     ========================================================================== */

  var toastTimer = 0;
  function showToast(msg) {
    if (!toastEl || !msg) return;
    toastEl.textContent = msg;
    toastEl.classList.add("show");
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(function () {
      toastEl.classList.remove("show");
      toastTimer = 0;
    }, 2400);
  }

  /* ==========================================================================
     Theme Toggle
     ========================================================================== */

  function applyTheme(theme) {
    var mode = theme === "light" ? "light" : "dark";
    root.setAttribute("data-theme", mode);
    if (themeMeta) {
      themeMeta.setAttribute("content", mode === "light" ? "#f6f8fb" : "#0b0d10");
    }
  }

  var storedTheme = null;
  try { storedTheme = localStorage.getItem("sv-theme") || localStorage.getItem("qv-theme"); } catch (e) {}
  if (storedTheme === "light" || storedTheme === "dark") {
    applyTheme(storedTheme);
  } else if (window.matchMedia && window.matchMedia("(prefers-color-scheme: light)").matches) {
    applyTheme("light");
  } else {
    applyTheme("dark");
  }

  if (themeToggle) {
    themeToggle.addEventListener("click", function () {
      var next = root.getAttribute("data-theme") === "light" ? "dark" : "light";
      applyTheme(next);
      try { localStorage.setItem("sv-theme", next); } catch (e) {}
    });
  }

  /* ==========================================================================
     Header / Mobile Nav / Waypoints
     ========================================================================== */

  function onScroll() {
    if (header) header.classList.toggle("scrolled", window.scrollY > 8);
  }
  onScroll();
  window.addEventListener("scroll", onScroll, { passive: true });

  if (navToggle && nav) {
    navToggle.addEventListener("click", function () {
      var open = nav.classList.toggle("open");
      navToggle.setAttribute("aria-expanded", open ? "true" : "false");
    });

    nav.addEventListener("click", function (e) {
      if (e.target && e.target.tagName === "A") {
        nav.classList.remove("open");
        navToggle.setAttribute("aria-expanded", "false");
      }
    });
  }

  var navSectionIds = ["features", "highlights", "gallery", "formats", "download", "updates"];
  var navLinks = {};
  navSectionIds.forEach(function (id) {
    navLinks[id] = document.querySelector('.nav a[href="#' + id + '"]');
  });

  if ("IntersectionObserver" in window) {
    var waypoints = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        navSectionIds.forEach(function (id) {
          if (navLinks[id]) navLinks[id].classList.toggle("active", id === entry.target.id);
        });
      });
    }, { rootMargin: "-42% 0px -52% 0px" });

    navSectionIds.forEach(function (id) {
      var el = document.getElementById(id);
      if (el) waypoints.observe(el);
    });
  }

  /* ==========================================================================
     Scroll Reveal
     ========================================================================== */

  var revealEls = document.querySelectorAll(".reveal");
  if ("IntersectionObserver" in window && revealEls.length) {
    var revealObs = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (entry.isIntersecting) {
          entry.target.classList.add("in-view");
          revealObs.unobserve(entry.target);
        }
      });
    }, { threshold: 0.08, rootMargin: "0px 0px -30px 0px" });

    Array.prototype.forEach.call(revealEls, function (el) {
      var rect = el.getBoundingClientRect();
      if (rect.top < window.innerHeight) {
        el.classList.add("in-view");
      } else {
        revealObs.observe(el);
      }
    });
  } else {
    Array.prototype.forEach.call(revealEls, function (el) {
      el.classList.add("in-view");
    });
  }

  /* ==========================================================================
     Lightbox (Supports Gallery Mode & Single-Image Mode)
     ========================================================================== */

  var lightbox = document.getElementById("lightbox");
  var lbImg = document.getElementById("lb-img");
  var lbCap = document.getElementById("lb-cap");
  var lbClose = document.getElementById("lb-close");
  var lbPrev = document.getElementById("lb-prev");
  var lbNext = document.getElementById("lb-next");
  var lbMode = "single";

  function openLightboxSingle(src, title, desc) {
    if (!lightbox || !lbImg) return;
    lbMode = "single";
    lbImg.src = src;
    lbImg.alt = title || "";
    if (lbCap) lbCap.textContent = desc ? (title + " — " + desc) : (title || "");
    if (lbPrev) lbPrev.hidden = true;
    if (lbNext) lbNext.hidden = true;
    lightbox.classList.add("open");
    lightbox.setAttribute("aria-hidden", "false");
    document.body.style.overflow = "hidden";
  }

  function openLightboxGallery() {
    if (!lightbox || !lbImg || !filteredShots.length) return;
    lbMode = "gallery";
    var shot = filteredShots[currentShotIdx] || filteredShots[0];
    lbImg.src = "assets/img/v0.6.1/" + shot.file;
    lbImg.alt = shot.title;
    if (lbCap) lbCap.textContent = shot.title + " — " + shot.desc;
    var multi = filteredShots.length > 1;
    if (lbPrev) lbPrev.hidden = !multi;
    if (lbNext) lbNext.hidden = !multi;
    lightbox.classList.add("open");
    lightbox.setAttribute("aria-hidden", "false");
    document.body.style.overflow = "hidden";
  }

  function closeLightbox() {
    if (!lightbox) return;
    lightbox.classList.remove("open");
    lightbox.setAttribute("aria-hidden", "true");
    document.body.style.overflow = "";
  }

  if (lbClose) lbClose.addEventListener("click", closeLightbox);
  if (lbPrev) {
    lbPrev.addEventListener("click", function () {
      if (lbMode === "gallery") { showGalleryShot(currentShotIdx - 1); openLightboxGallery(); }
    });
  }
  if (lbNext) {
    lbNext.addEventListener("click", function () {
      if (lbMode === "gallery") { showGalleryShot(currentShotIdx + 1); openLightboxGallery(); }
    });
  }
  if (lightbox) {
    lightbox.addEventListener("click", function (e) {
      if (e.target === lightbox) closeLightbox();
    });
  }

  document.addEventListener("keydown", function (e) {
    if (e.key === "Escape") {
      if (lightbox && lightbox.classList.contains("open")) {
        closeLightbox();
        return;
      }
      if (nav && nav.classList.contains("open")) {
        nav.classList.remove("open");
        if (navToggle) navToggle.setAttribute("aria-expanded", "false");
      }
    }
    if (lightbox && lightbox.classList.contains("open") && lbMode === "gallery") {
      if (e.key === "ArrowLeft") { showGalleryShot(currentShotIdx - 1); openLightboxGallery(); }
      if (e.key === "ArrowRight") { showGalleryShot(currentShotIdx + 1); openLightboxGallery(); }
    }
  });

  /* ==========================================================================
     Hero Interactive Stage Switcher
     ========================================================================== */

  var heroImg = document.getElementById("hero-img");
  var heroZoomBtn = document.getElementById("hero-zoom-btn");
  var heroTabs = document.getElementById("hero-tabs");
  var currentHeroShot = {
    src: "assets/img/v0.6.1/01-main.webp",
    title: "主窗口",
    desc: "精简顶栏与画布同色融合，搭配底部悬浮控制胶囊"
  };

  function swapImage(imgEl, src, alt, onDone) {
    if (!imgEl) return;
    imgEl.classList.add("swapping");
    var pre = new Image();
    pre.onload = function () {
      imgEl.src = src;
      if (alt) imgEl.alt = alt;
      imgEl.classList.remove("swapping");
      if (onDone) onDone();
    };
    pre.onerror = function () {
      imgEl.classList.remove("swapping");
    };
    pre.src = src;
  }

  if (heroTabs && heroImg) {
    heroTabs.addEventListener("click", function (e) {
      var btn = e.target.closest(".hero-tab");
      if (!btn) return;
      var file = btn.getAttribute("data-file");
      var title = btn.getAttribute("data-title") || "";
      var desc = btn.getAttribute("data-desc") || "";
      if (!file) return;
      var src = "assets/img/v0.6.1/" + file;
      currentHeroShot = { src: src, title: title, desc: desc };

      Array.prototype.forEach.call(heroTabs.querySelectorAll(".hero-tab"), function (tab) {
        var active = tab === btn;
        tab.classList.toggle("active", active);
        tab.setAttribute("aria-selected", active ? "true" : "false");
      });

      swapImage(heroImg, src, "SoleView " + title + " 实机截图");
    });

    heroImg.addEventListener("click", function () {
      openLightboxSingle(currentHeroShot.src, currentHeroShot.title, currentHeroShot.desc);
    });
  }

  if (heroZoomBtn) {
    heroZoomBtn.addEventListener("click", function () {
      openLightboxSingle(currentHeroShot.src, currentHeroShot.title, currentHeroShot.desc);
    });
  }

  /* ==========================================================================
     Spotlights Switchers
     ========================================================================== */

  Array.prototype.forEach.call(document.querySelectorAll(".spot-btn"), function (btn) {
    btn.addEventListener("click", function () {
      var targetId = btn.getAttribute("data-spotlight");
      var src = btn.getAttribute("data-src");
      var title = btn.getAttribute("data-title") || "";
      var caption = btn.getAttribute("data-caption") || "";
      var frame = document.getElementById(targetId);
      var capEl = document.getElementById(targetId + "-cap");
      if (!frame || !src) return;

      var group = btn.parentElement;
      if (group) {
        Array.prototype.forEach.call(group.querySelectorAll(".spot-btn"), function (b) {
          b.classList.toggle("active", b === btn);
        });
      }

      var img = frame.querySelector("img");
      swapImage(img, src, "SoleView " + title);
      if (capEl) capEl.textContent = caption;
      frame.setAttribute("data-current-title", title);
      frame.setAttribute("data-current-caption", caption);
    });
  });

  Array.prototype.forEach.call(document.querySelectorAll(".spotlight-frame"), function (frame) {
    frame.addEventListener("click", function () {
      var img = frame.querySelector("img");
      if (!img) return;
      var capEl = document.getElementById(frame.id + "-cap");
      var title = frame.getAttribute("data-current-title") || img.alt || "SoleView 界面";
      var caption = frame.getAttribute("data-current-caption") || (capEl ? capEl.textContent : "");
      openLightboxSingle(img.src, title, caption);
    });
  });

  /* ==========================================================================
     14-Shot Filterable Gallery
     ========================================================================== */

  var allShots = [
    { file: "01-main.webp", title: "主窗口", desc: "精简顶栏与画布同色融合，搭配底部控制胶囊", cat: "view" },
    { file: "02-thumbnails.webp", title: "缩略图浏览", desc: "底部横向胶片栏，按需异步生成缩略图", cat: "view" },
    { file: "03-info.webp", title: "图片信息", desc: "EXIF / GPS / 直方图，Tab 呼出", cat: "tools" },
    { file: "04-spread.webp", title: "双页阅览", desc: "画册、漫画相邻两页并排查看", cat: "view" },
    { file: "05-zoom.webp", title: "1:1 看像素", desc: "滚轮缩放、拖拽平移，原图细节不抹平", cat: "view" },
    { file: "06-effects.webp", title: "图像效果", desc: "反色 / Gamma / 旋转 / 插值算法切换", cat: "tools" },
    { file: "07-context-menu.webp", title: "右键菜单", desc: "Fluent 统一样式，图标与快捷键右省略对齐", cat: "settings" },
    { file: "08-convert.webp", title: "批量转换", desc: "PNG / JPEG / BMP / WebP / TIFF 与裁剪缩放", cat: "tools" },
    { file: "09-settings.webp", title: "设置中心", desc: "十大分类、72 个 40×22 统一开关，十余项即时生效", cat: "settings" },
    { file: "10-context-menu-settings.webp", title: "查看器右键菜单", desc: "按组大纲管理可见性与右侧实时预览", cat: "settings" },
    { file: "11-start.webp", title: "启动画面", desc: "拖入图片，或点「打开」调用原生对话框选图 / 文件夹", cat: "view" },
    { file: "12-overview.webp", title: "图片总览", desc: "自适应列数网格，支持五档缩略图缩放与缓存", cat: "view" },
    { file: "13-svg.webp", title: "SVG 矢量查看", desc: "直接打开并栅格化渲染 SVG 矢量图", cat: "view" },
    { file: "14-about.webp", title: "关于页", desc: "显示版本号、MIT 协议、官网链接与 QQ 交流群", cat: "settings" }
  ];

  var galleryStageImg = document.getElementById("gallery-img");
  var galleryTitle = document.getElementById("gallery-title");
  var galleryDesc = document.getElementById("gallery-desc");
  var galleryCounter = document.getElementById("gallery-counter");
  var galleryThumbsWrap = document.getElementById("thumbs");
  var galleryFiltersWrap = document.getElementById("gallery-filters");
  var galleryPrevBtn = document.getElementById("gallery-prev");
  var galleryNextBtn = document.getElementById("gallery-next");
  var galleryZoomBtn = document.getElementById("gallery-zoom");

  var activeCategory = "all";
  var filteredShots = allShots.slice();
  var currentShotIdx = 1;

  function pad2(n) {
    return n < 10 ? "0" + n : String(n);
  }

  function renderGalleryThumbs() {
    if (!galleryThumbsWrap) return;
    galleryThumbsWrap.innerHTML = "";
    filteredShots.forEach(function (shot, i) {
      var btn = document.createElement("button");
      btn.type = "button";
      btn.className = "thumb";
      btn.setAttribute("role", "tab");
      btn.setAttribute("aria-label", shot.title);
      btn.setAttribute("aria-selected", i === currentShotIdx ? "true" : "false");

      var img = document.createElement("img");
      img.src = "assets/img/v0.6.1/" + shot.file;
      img.alt = shot.title;
      img.loading = "lazy";
      img.width = 320;
      img.height = 240;

      var span = document.createElement("span");
      span.className = "thumb-title";
      span.textContent = shot.title;

      btn.appendChild(img);
      btn.appendChild(span);
      btn.addEventListener("click", function () { showGalleryShot(i); });
      galleryThumbsWrap.appendChild(btn);
    });
  }

  function showGalleryShot(index, skipScroll) {
    if (!filteredShots.length || !galleryStageImg) return;
    currentShotIdx = (index + filteredShots.length) % filteredShots.length;
    var shot = filteredShots[currentShotIdx];
    var src = "assets/img/v0.6.1/" + shot.file;

    swapImage(galleryStageImg, src, shot.title);
    if (galleryTitle) galleryTitle.textContent = shot.title;
    if (galleryDesc) galleryDesc.textContent = shot.desc;
    if (galleryCounter) {
      galleryCounter.textContent = pad2(currentShotIdx + 1) + " / " + pad2(filteredShots.length);
    }

    if (galleryThumbsWrap) {
      Array.prototype.forEach.call(galleryThumbsWrap.children, function (btn, idx) {
        var on = idx === currentShotIdx;
        btn.classList.toggle("active", on);
        btn.setAttribute("aria-selected", on ? "true" : "false");
        if (on && !skipScroll && btn.scrollIntoView) {
          try {
            btn.scrollIntoView({ behavior: "smooth", block: "nearest", inline: "center" });
          } catch (e) {}
        }
      });
    }
  }

  if (galleryFiltersWrap) {
    galleryFiltersWrap.addEventListener("click", function (e) {
      var chip = e.target.closest(".filter-chip");
      if (!chip) return;
      var cat = chip.getAttribute("data-cat") || "all";
      if (cat === activeCategory) return;
      activeCategory = cat;

      Array.prototype.forEach.call(galleryFiltersWrap.querySelectorAll(".filter-chip"), function (c) {
        c.classList.toggle("active", c === chip);
      });

      filteredShots = cat === "all"
        ? allShots.slice()
        : allShots.filter(function (s) { return s.cat === cat; });

      currentShotIdx = 0;
      renderGalleryThumbs();
      showGalleryShot(0, true);
    });
  }

  if (galleryPrevBtn) galleryPrevBtn.addEventListener("click", function () { showGalleryShot(currentShotIdx - 1); });
  if (galleryNextBtn) galleryNextBtn.addEventListener("click", function () { showGalleryShot(currentShotIdx + 1); });
  if (galleryStageImg) galleryStageImg.addEventListener("click", openLightboxGallery);
  if (galleryZoomBtn) galleryZoomBtn.addEventListener("click", openLightboxGallery);

  renderGalleryThumbs();
  showGalleryShot(1, true);

  /* ==========================================================================
     Clipboard Copy Buttons (QQ & Verify Command)
     ========================================================================== */

  function copyFallback(text) {
    var ta = document.createElement("textarea");
    ta.value = text;
    ta.setAttribute("readonly", "");
    ta.style.position = "fixed";
    ta.style.left = "-9999px";
    document.body.appendChild(ta);
    ta.select();
    try {
      document.execCommand("copy");
    } finally {
      document.body.removeChild(ta);
    }
  }

  function copyText(text) {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      return navigator.clipboard.writeText(text).catch(function () {
        copyFallback(text);
      });
    }
    copyFallback(text);
    return Promise.resolve();
  }

  Array.prototype.forEach.call(document.querySelectorAll("[data-qq]"), function (btn) {
    var timerId = 0;
    btn.addEventListener("click", function () {
      var qq = btn.getAttribute("data-qq") || "";
      var label = btn.getAttribute("data-qq-label") || btn.textContent;
      var copied = btn.getAttribute("data-qq-copied") || ("已复制：" + qq);
      copyText(qq).then(function () {
        btn.textContent = copied;
        showToast("已复制 QQ 群号：" + qq);
        if (timerId) clearTimeout(timerId);
        timerId = setTimeout(function () {
          btn.textContent = label;
          timerId = 0;
        }, 2000);
      });
    });
  });

  Array.prototype.forEach.call(document.querySelectorAll("[data-copy]"), function (btn) {
    var timerId = 0;
    var origText = btn.textContent;
    btn.addEventListener("click", function () {
      var text = btn.getAttribute("data-copy") || "";
      if (!text) return;
      copyText(text).then(function () {
        btn.textContent = "已复制";
        showToast("已复制校验命令");
        if (timerId) clearTimeout(timerId);
        timerId = setTimeout(function () {
          btn.textContent = origText;
          timerId = 0;
        }, 2000);
      });
    });
  });

  /* ==========================================================================
     GitHub Changelog Engine (4-Tier Resilient Fetch + Safe Markdown Renderer)
     ========================================================================== */

  var statusPillEl = document.getElementById("changelog-status");
  var statusTextEl = document.getElementById("changelog-status-text");
  var changelogListEl = document.getElementById("changelog-list");
  var changelogSearchEl = document.getElementById("changelog-search");
  var changelogRefreshBtn = document.getElementById("changelog-refresh");
  var changelogMoreWrap = document.getElementById("changelog-more-wrap");
  var changelogMoreBtn = document.getElementById("changelog-more-btn");

  var FALLBACK_RELEASES = [
    {
      tag: "v0.6.1",
      version: "0.6.1",
      title: "SoleView 0.6.1",
      subtitle: "品牌升级为 SoleView · 启动与打开收口 · 设置即时化续批",
      date: "2026-11-26",
      url: REPO_URL + "/releases/tag/v0.6.1",
      prerelease: false,
      unreleased: false,
      assets: [
        { name: "SoleView-0.6.1-setup.exe", size: 29200246, url: REPO_URL + "/releases/download/v0.6.1/SoleView-0.6.1-setup.exe" },
        { name: "soleview-0.6.1-win64.zip", size: 41594143, url: REPO_URL + "/releases/download/v0.6.1/soleview-0.6.1-win64.zip" },
        { name: "SHA256SUMS.txt", size: 182, url: REPO_URL + "/releases/download/v0.6.1/SHA256SUMS.txt" }
      ],
      markdown: [
        "> 本版是 0.6.0 的增量更新，并完成全项目统一更名为 SoleView。继续推进设置即时化与界面 Fluent 收口，重做启动恢复策略与「打开」入口，补齐剪贴板、总览与系统集成。",
        "",
        "### 升级注意：三处行为变更",
        "- **启动恢复改为可选、默认关**：位置在「设置 → 通用 → 启动」，无参启动进入欢迎页，双击或命令行文件优先。",
        "- **顶栏四个入口移除**：书签、幻灯片、编辑、复制到从顶栏撤掉，保留在三横菜单、画布右键与快捷键里。",
        "- **「打开」改用 Windows 原生对话框、备用键修好**：改用 Windows 原生 `IFileOpenDialog` + `IFileDialogCustomize`，`F` 或自定义备用键可直接触发打开文件夹。",
        "",
        "### 界面统一与系统集成",
        "- 六个下拉菜单与四个弹窗统一 Fluent 外壳；设置开关统一为 40×22 Switch。",
        "- 新增十项设置即时生效；图片总览支持五档缩略图缩放与自适应缓存。",
        "- 全景查看器修正仰视 / 俯视纬度投影并实现滚轮缩放先快后清；「定位文件」在资源管理器中直接选中图片。"
      ].join("\n")
    },
    {
      tag: "v0.6.0",
      version: "0.6.0",
      title: "SoleView 0.6.0 (原 QuikView 0.6.0)",
      subtitle: "360° 全景组件 · 总览重做 · 格式与即时化扩展",
      date: "2026-11-22",
      url: REPO_URL + "/releases/tag/v0.6.0",
      prerelease: false,
      unreleased: false,
      assets: [],
      markdown: [
        "> 新增可选 360° 全景组件，重做图片总览网格，打通 SVG 直接打开，并启用自建纯解码 libheif。",
        "",
        "### 主要更新",
        "- 新增可选 360° 全景查看器组件（独立进程 `soleview-pano.exe`，历史版本曾名 `quikview-pano.exe`）与内置 `QSvgRenderer` SVG 直接打开。",
        "- 界面语言、翻页音效与 RAW 打开策略支持即时生效；设置侧栏新增「关于」页。",
        "- 安装包体积由 39.6 MB 瘦身至约 29 MB。"
      ].join("\n")
    },
    {
      tag: "v0.5.6",
      version: "0.5.6",
      title: "SoleView 0.5.6 (原 QuikView 0.5.6)",
      subtitle: "设置九页重组 · 压缩包直读 · 浏览与窗口记忆 · 交互显示收口",
      date: "2026-11-21",
      url: REPO_URL + "/releases/tag/v0.5.6",
      prerelease: false,
      unreleased: false,
      assets: [],
      markdown: [
        "### 主要更新",
        "- 直接打开 ZIP 与 CBZ 压缩包，像浏览文件夹一样翻看内部图片。",
        "- 设置重组为九个分类并支持配置备份导入导出。",
        "- 支持中键点击、拖动与滚轮上下自定义动作。"
      ].join("\n")
    }
  ];

  function escapeHtml(str) {
    return String(str)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function sanitizeLinkUrl(rawUrl) {
    var trimmed = String(rawUrl || "").trim();
    if (!trimmed) return "";
    if (/^(javascript|data|vbscript):/i.test(trimmed)) return "";
    if (/^https?:\/\//i.test(trimmed)) return trimmed;
    if (/^\/\/[^\s]+/.test(trimmed)) return "";
    if (/^[a-zA-Z0-9_.\-\/#?&=%+~]+$/.test(trimmed)) {
      var cleanPath = trimmed.replace(/^\/+/, "");
      return REPO_URL + "/blob/main/" + cleanPath;
    }
    return "";
  }

  function renderInline(rawText) {
    var tokens = [];
    var withPlaceholders = String(rawText || "").replace(/`([^`\n]+)`/g, function (_, code) {
      var idx = tokens.length;
      tokens.push("<code>" + escapeHtml(code) + "</code>");
      return "\u0000CODE" + idx + "\u0000";
    });

    var escaped = escapeHtml(withPlaceholders);

    escaped = escaped.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, function (_, label, url) {
      var safeUrl = sanitizeLinkUrl(url);
      if (!safeUrl) return label;
      return '<a href="' + escapeHtml(safeUrl) + '" target="_blank" rel="noopener">' + label + "</a>";
    });

    escaped = escaped.replace(/\*\*([^*\n]+)\*\*/g, "<strong>$1</strong>");
    escaped = escaped.replace(/(^|[^*])\*([^*\n]+)\*(?!\*)/g, "$1<em>$2</em>");

    return escaped.replace(/\u0000CODE(\d+)\u0000/g, function (_, idx) {
      return tokens[Number(idx)] || "";
    });
  }

  function renderMarkdownBlock(lines) {
    var html = [];
    var i = 0;
    var n = lines.length;

    while (i < n) {
      var startIdx = i;
      var line = lines[i];
      var trimmed = line.trim();

      if (!trimmed || /^(-{3,}|\*{3,}|_{3,})$/.test(trimmed)) {
        i += 1;
        continue;
      }

      if (/^```/.test(trimmed)) {
        i += 1;
        var codeLines = [];
        while (i < n && !/^```/.test(lines[i].trim())) {
          codeLines.push(lines[i]);
          i += 1;
        }
        if (i < n) i += 1;
        html.push('<pre class="cl-codeblock"><code>' + escapeHtml(codeLines.join("\n")) + "</code></pre>");
        continue;
      }

      var hx = trimmed.match(/^#{1,6}\s+(.+)$/);
      if (hx) {
        html.push('<h5 class="cl-subhead">' + renderInline(hx[1]) + "</h5>");
        i += 1;
        continue;
      }

      if (/^>\s?/.test(trimmed)) {
        var qLines = [];
        while (i < n && /^>\s?/.test(lines[i].trim())) {
          qLines.push(lines[i].trim().replace(/^>\s?/, ""));
          i += 1;
        }
        html.push('<div class="cl-lead"><p>' + qLines.map(renderInline).join("<br>") + "</p></div>");
        continue;
      }

      if (trimmed.indexOf("|") === 0 && i + 1 < n && /^\|?[\s:|-]+\|/.test(lines[i + 1].trim())) {
        var headerCells = trimmed.replace(/^\|/, "").replace(/\|$/, "").split("|");
        i += 2;
        var bodyRows = [];
        while (i < n && lines[i].trim().indexOf("|") === 0) {
          bodyRows.push(lines[i].trim().replace(/^\|/, "").replace(/\|$/, "").split("|"));
          i += 1;
        }
        var tHtml = ['<div class="cl-table-wrap"><table class="cl-table"><thead><tr>'];
        headerCells.forEach(function (c) { tHtml.push("<th>" + renderInline(c.trim()) + "</th>"); });
        tHtml.push("</tr></thead><tbody>");
        bodyRows.forEach(function (row) {
          tHtml.push("<tr>");
          row.forEach(function (c) { tHtml.push("<td>" + renderInline(c.trim()) + "</td>"); });
          tHtml.push("</tr>");
        });
        tHtml.push("</tbody></table></div>");
        html.push(tHtml.join(""));
        continue;
      }

      if (/^[-*+]\s+/.test(trimmed) || /^\d+\.\s+/.test(trimmed)) {
        var isOrdered = /^\d+\.\s+/.test(trimmed);
        var tag = isOrdered ? "ol" : "ul";
        var baseIndent = line.length - line.replace(/^\s+/, "").length;
        var listItems = [];

        while (i < n) {
          var cur = lines[i];
          var curTrim = cur.trim();
          var indent = cur.length - cur.replace(/^\s+/, "").length;
          var topMatch = isOrdered
            ? (indent <= baseIndent && curTrim.match(/^\d+\.\s+(.+)$/))
            : (indent <= baseIndent && curTrim.match(/^[-*+]\s+(.+)$/));
          var subMatch = indent > baseIndent && curTrim.match(/^(?:[-*+]|\d+\.)\s+(.+)$/);

          if (topMatch) {
            listItems.push({ html: renderInline(topMatch[1]), subs: [] });
            i += 1;
          } else if (subMatch && listItems.length > 0) {
            listItems[listItems.length - 1].subs.push(renderInline(subMatch[1]));
            i += 1;
          } else if (!curTrim) {
            if (i + 1 < n && /^\s*([-*+]|\d+\.)\s+/.test(lines[i + 1])) {
              i += 1;
            } else {
              break;
            }
          } else {
            break;
          }
        }

        if (listItems.length > 0) {
          var lHtml = ['<' + tag + ' class="cl-list">'];
          listItems.forEach(function (item) {
            var subHtml = item.subs.length
              ? '<ul class="cl-sublist">' + item.subs.map(function (s) { return "<li>" + s + "</li>"; }).join("") + "</ul>"
              : "";
            lHtml.push("<li>" + item.html + subHtml + "</li>");
          });
          lHtml.push("</" + tag + ">");
          html.push(lHtml.join(""));
        }
        if (i === startIdx) {
          html.push('<p class="cl-prose">' + renderInline(trimmed) + "</p>");
          i += 1;
        }
        continue;
      }

      var paraLines = [trimmed];
      i += 1;
      while (i < n) {
        var pt = lines[i].trim();
        if (!pt || /^(-{3,}|\*{3,}|_{3,})$/.test(pt) || /^(#{1,6}\s|>|```|[-*+]\s|\d+\.\s|\|)/.test(pt)) break;
        paraLines.push(pt);
        i += 1;
      }
      html.push('<p class="cl-prose">' + paraLines.map(renderInline).join(" ") + "</p>");

      if (i === startIdx) {
        i += 1;
      }
    }

    return html.join("");
  }

  function parseReleaseSections(markdown) {
    var lines = String(markdown || "").replace(/\r\n/g, "\n").split("\n");
    var leadLines = [];
    var introLines = [];
    var sections = [];
    var currentSection = null;
    var seenHeading = false;

    lines.forEach(function (line) {
      var h3 = line.match(/^#{2,3}\s+(.+)$/);
      if (h3) {
        seenHeading = true;
        if (currentSection) sections.push(currentSection);
        var heading = h3[1].trim();
        currentSection = {
          heading: heading,
          isWarning: /^(升级注意|重要提示|注意|Breaking)/i.test(heading),
          lines: []
        };
        return;
      }

      if (!seenHeading) {
        var trimmed = line.trim();
        if (!trimmed) {
          if (introLines.length) introLines.push("");
          else if (leadLines.length) leadLines.push("");
        } else if (/^>\s?/.test(trimmed) && introLines.length === 0) {
          leadLines.push(line);
        } else if (!/^(-{3,}|\*{3,}|_{3,})$/.test(trimmed)) {
          introLines.push(line);
        }
      } else if (currentSection) {
        currentSection.lines.push(line);
      }
    });

    if (currentSection) sections.push(currentSection);

    if (introLines.length) {
      var introHtml = renderMarkdownBlock(introLines);
      if (introHtml) {
        sections.unshift({
          heading: sections.length ? "主要变更" : "",
          isWarning: false,
          html: introHtml
        });
      }
    }

    sections.forEach(function (sec) {
      if (sec.html === undefined) {
        sec.html = renderMarkdownBlock(sec.lines);
      }
    });

    sections = sections.filter(function (sec) { return sec.html && sec.html.trim() !== ""; });

    return {
      leadHtml: renderMarkdownBlock(leadLines),
      sections: sections
    };
  }

  function parseReleasesApi(apiItems) {
    if (!Array.isArray(apiItems)) return [];
    return apiItems
      .filter(function (item) { return item && !item.draft; })
      .map(function (item) {
        var tag = String(item.tag_name || "");
        var version = tag.replace(/^v/i, "") || String(item.name || "");
        var date = item.published_at ? String(item.published_at).slice(0, 10) : "";
        var rawBody = String(item.body || "").replace(/\r\n/g, "\n");
        var lines = rawBody.split("\n");
        var subtitle = "";

        if (lines.length && /^#\s+/.test(lines[0].trim())) {
          var firstHeading = lines[0].trim().replace(/^#\s+/, "");
          var dotParts = firstHeading.split(/\s+·\s+/);
          if (dotParts.length > 1) {
            subtitle = dotParts.slice(1).join(" · ").trim();
          }
          lines.shift();
        }

        if (!subtitle && item.name) {
          var nameParts = String(item.name).split(/\s+·\s+/);
          if (nameParts.length > 1) {
            subtitle = nameParts.slice(1).join(" · ").trim();
          }
        }

        var assets = Array.isArray(item.assets)
          ? item.assets.map(function (a) {
              return {
                name: String(a.name || ""),
                size: Number(a.size || 0),
                url: sanitizeLinkUrl(a.browser_download_url) || ""
              };
            }).filter(function (a) { return a.name && a.url; })
          : [];

        return {
          tag: tag || ("v" + version),
          version: version,
          title: item.name || ("SoleView " + version),
          subtitle: subtitle,
          date: date,
          url: sanitizeLinkUrl(item.html_url) || (REPO_URL + "/releases"),
          prerelease: Boolean(item.prerelease),
          unreleased: false,
          assets: assets,
          markdown: lines.join("\n").trim()
        };
      });
  }

  function parseChangelogMarkdown(mdText) {
    var lines = String(mdText || "").replace(/\r\n/g, "\n").split("\n");
    var blocks = [];
    var current = null;

    lines.forEach(function (line) {
      var h2 = line.match(/^##\s+(.+)$/);
      if (h2) {
        if (current) blocks.push(current);
        current = { heading: h2[1].trim(), lines: [] };
        return;
      }
      if (current) current.lines.push(line);
    });
    if (current) blocks.push(current);

    return blocks.map(function (blk) {
      var heading = blk.heading;
      var verMatch = heading.match(/^\[?([0-9]+\.[0-9]+\.[0-9]+(?:-[^\]]+)?)\]?/);
      var version = verMatch ? verMatch[1] : heading;
      var unreleased = /未发布|unreleased/i.test(heading);

      var dateMatch = heading.match(/(\d{4}-\d{2}-\d{2})/);
      var date = dateMatch ? dateMatch[1] : "";

      var subtitle = "";
      if (unreleased) {
        var dashParts = heading.split(/——|--/);
        subtitle = dashParts.length > 1 ? dashParts.slice(1).join(" ").trim() : "";
      } else {
        var dotIdx = heading.indexOf("·");
        if (dotIdx !== -1) {
          subtitle = heading.slice(dotIdx + 1).trim();
        }
      }

      var tag = "v" + version;
      var url = unreleased
        ? (REPO_URL + "/blob/main/CHANGELOG.md")
        : (REPO_URL + "/releases/tag/" + encodeURIComponent(tag));

      return {
        tag: tag,
        version: version,
        title: "SoleView " + version,
        subtitle: subtitle,
        date: date,
        url: url,
        prerelease: false,
        unreleased: unreleased,
        assets: [],
        markdown: blk.lines.join("\n").trim()
      };
    });
  }

  function formatSizeMB(bytes) {
    if (!bytes || isNaN(bytes)) return "";
    return (bytes / 1000000).toFixed(1) + " MB";
  }

  function syncHeroAndDownload(releases) {
    if (!Array.isArray(releases) || !releases.length) return;
    var latest = releases.find(function (r) { return !r.unreleased && !r.prerelease; })
      || releases.find(function (r) { return !r.unreleased; });
    if (!latest || !latest.version) return;

    var verTag = latest.tag || ("v" + latest.version);
    var vNum = latest.version;

    var headerPill = document.getElementById("header-version-pill");
    var badgeVer = document.getElementById("hero-badge-ver");
    var badgeNote = document.getElementById("hero-badge-note");
    var heroDlLabel = document.getElementById("hero-download-label");
    var dlMeta = document.getElementById("download-meta");
    var dlReleaseLink = document.getElementById("dl-release-link");

    if (headerPill) headerPill.textContent = verTag;
    if (badgeVer) badgeVer.textContent = verTag + " 现已发布";
    if (badgeNote && latest.subtitle) {
      var shortSub = latest.subtitle.split("·").slice(0, 2).join(" · ").trim();
      badgeNote.textContent = shortSub;
    }
    if (heroDlLabel) heroDlLabel.textContent = "下载 " + verTag;
    if (dlMeta) {
      dlMeta.textContent = verTag + (latest.date ? " · " + latest.date : "") + " · Windows 10 / 11 x64";
    }
    if (dlReleaseLink) {
      dlReleaseLink.href = latest.url;
      dlReleaseLink.textContent = verTag + " GitHub 发布页 ↗";
    }

    var setupBtn = document.getElementById("dl-setup-btn");
    var setupName = document.getElementById("dl-setup-name");
    var setupSize = document.getElementById("dl-setup-size");
    var zipBtn = document.getElementById("dl-zip-btn");
    var zipName = document.getElementById("dl-zip-name");
    var zipSize = document.getElementById("dl-zip-size");
    var shaBtn = document.getElementById("dl-sha-btn");
    var verifyCmd = document.getElementById("verify-cmd-text");
    var verifyCopyBtn = document.getElementById("verify-copy-btn");

    var setupAsset = null;
    var zipAsset = null;
    var shaAsset = null;

    if (latest.assets && latest.assets.length) {
      latest.assets.forEach(function (a) {
        if (/setup.*\.exe$/i.test(a.name)) setupAsset = a;
        else if (/win64.*\.zip$/i.test(a.name)) zipAsset = a;
        else if (/\.zip$/i.test(a.name) && !zipAsset && !/pano/i.test(a.name)) zipAsset = a;
        else if (/sha256/i.test(a.name)) shaAsset = a;
      });
    }

    var expectedSetupName = setupAsset ? setupAsset.name : ("SoleView-" + vNum + "-setup.exe");
    var expectedZipName = zipAsset ? zipAsset.name : ("soleview-" + vNum + "-win64.zip");

    if (setupBtn) setupBtn.href = setupAsset ? setupAsset.url : (REPO_URL + "/releases/download/" + verTag + "/" + expectedSetupName);
    if (setupName) setupName.textContent = expectedSetupName;
    if (setupSize && setupAsset && setupAsset.size) setupSize.textContent = formatSizeMB(setupAsset.size);

    if (zipBtn) zipBtn.href = zipAsset ? zipAsset.url : (REPO_URL + "/releases/download/" + verTag + "/" + expectedZipName);
    if (zipName) zipName.textContent = expectedZipName;
    if (zipSize && zipAsset && zipAsset.size) zipSize.textContent = formatSizeMB(zipAsset.size);

    if (shaBtn) shaBtn.href = shaAsset ? shaAsset.url : (REPO_URL + "/releases/download/" + verTag + "/SHA256SUMS.txt");

    var cmdStr = "Get-FileHash " + expectedSetupName + " -Algorithm SHA256";
    if (verifyCmd) verifyCmd.textContent = cmdStr;
    if (verifyCopyBtn) verifyCopyBtn.setAttribute("data-copy", cmdStr);
  }

  function setStatus(kind, text) {
    if (!statusPillEl || !statusTextEl) return;
    statusPillEl.className = "sync-pill " + kind;
    statusTextEl.textContent = text;
  }

  var state = {
    releases: [],
    expandedAll: false,
    searchQuery: ""
  };

  function createReleaseCard(rel, idx, forceExpand) {
    var parsed = parseReleaseSections(rel.markdown);
    var card = document.createElement("article");
    card.className = "release-card" + (idx === 0 && !rel.unreleased ? " is-latest" : "");

    var node = document.createElement("span");
    node.className = "release-node";
    node.setAttribute("aria-hidden", "true");
    card.appendChild(node);

    var head = document.createElement("header");
    head.className = "release-head";

    var headLeft = document.createElement("div");
    var idRow = document.createElement("div");
    idRow.className = "release-id-row";

    var verEl = document.createElement("h3");
    verEl.className = "release-ver";
    verEl.textContent = rel.unreleased ? (rel.version + " 批次") : rel.tag;
    idRow.appendChild(verEl);

    if (idx === 0 && !rel.unreleased) {
      var latestTag = document.createElement("span");
      latestTag.className = "rel-tag rel-tag-latest";
      latestTag.textContent = "最新版";
      idRow.appendChild(latestTag);
    } else if (rel.prerelease) {
      var preTag = document.createElement("span");
      preTag.className = "rel-tag rel-tag-pre";
      preTag.textContent = "预发布";
      idRow.appendChild(preTag);
    } else if (rel.unreleased) {
      var unTag = document.createElement("span");
      unTag.className = "rel-tag rel-tag-unreleased";
      unTag.textContent = "未单独发布";
      idRow.appendChild(unTag);
    }

    if (rel.date) {
      var timeEl = document.createElement("time");
      timeEl.className = "release-date";
      timeEl.dateTime = rel.date;
      timeEl.textContent = rel.date;
      idRow.appendChild(timeEl);
    }

    headLeft.appendChild(idRow);

    if (rel.subtitle) {
      var subEl = document.createElement("p");
      subEl.className = "release-subtitle";
      subEl.textContent = rel.subtitle;
      headLeft.appendChild(subEl);
    }

    var ghLink = document.createElement("a");
    ghLink.className = "release-gh-link";
    ghLink.href = rel.url || (REPO_URL + "/releases");
    ghLink.target = "_blank";
    ghLink.rel = "noopener";
    ghLink.textContent = rel.unreleased ? "查看 CHANGELOG ↗" : "GitHub 发布页 ↗";

    head.appendChild(headLeft);
    head.appendChild(ghLink);
    card.appendChild(head);

    var body = document.createElement("div");
    body.className = "release-body";

    if (parsed.leadHtml) {
      var leadWrap = document.createElement("div");
      leadWrap.innerHTML = parsed.leadHtml;
      body.appendChild(leadWrap);
    }

    var visibleLimit = (idx === 0) ? 2 : (parsed.leadHtml ? 0 : 1);
    var visibleSections = [];
    var extraSections = [];
    var normalCount = 0;

    parsed.sections.forEach(function (sec) {
      if (sec.isWarning) {
        visibleSections.push(sec);
      } else if (normalCount < visibleLimit) {
        visibleSections.push(sec);
        normalCount += 1;
      } else {
        extraSections.push(sec);
      }
    });

    function buildSectionEl(sec) {
      var el = document.createElement("div");
      el.className = sec.isWarning ? "cl-callout-warn" : "cl-group";
      if (sec.heading) {
        var h4 = document.createElement("h4");
        h4.textContent = sec.heading;
        el.appendChild(h4);
      }
      var content = document.createElement("div");
      content.innerHTML = sec.html;
      el.appendChild(content);
      return el;
    }

    visibleSections.forEach(function (sec) {
      body.appendChild(buildSectionEl(sec));
    });

    var extraWrap = null;
    if (extraSections.length) {
      extraWrap = document.createElement("div");
      extraWrap.className = "cl-extra";
      extraWrap.hidden = !forceExpand;
      extraSections.forEach(function (sec) {
        extraWrap.appendChild(buildSectionEl(sec));
      });
      body.appendChild(extraWrap);
    }

    card.appendChild(body);

    var hasAssets = rel.assets && rel.assets.length > 0;
    if (extraWrap || hasAssets) {
      var actionsRow = document.createElement("div");
      actionsRow.className = "cl-actions-row";

      if (extraWrap) {
        var toggleBtn = document.createElement("button");
        toggleBtn.type = "button";
        toggleBtn.className = "cl-toggle-btn";
        var countLabel = "展开完整更新日志（另含 " + extraSections.length + " 个分组）";
        toggleBtn.textContent = forceExpand ? "收起详细内容" : countLabel;
        toggleBtn.setAttribute("aria-expanded", forceExpand ? "true" : "false");
        toggleBtn.addEventListener("click", function () {
          var isOpen = !extraWrap.hidden;
          extraWrap.hidden = isOpen;
          toggleBtn.textContent = isOpen ? countLabel : "收起详细内容";
          toggleBtn.setAttribute("aria-expanded", isOpen ? "false" : "true");
        });
        actionsRow.appendChild(toggleBtn);
      }

      if (hasAssets) {
        var assetsWrap = document.createElement("div");
        assetsWrap.className = "cl-assets";
        rel.assets.forEach(function (a) {
          var aLink = document.createElement("a");
          aLink.className = "cl-asset-pill";
          aLink.href = a.url;
          aLink.target = "_blank";
          aLink.rel = "noopener";
          var nSpan = document.createElement("span");
          nSpan.textContent = a.name;
          aLink.appendChild(nSpan);
          if (a.size) {
            var sSpan = document.createElement("span");
            sSpan.className = "cl-asset-size";
            sSpan.textContent = formatSizeMB(a.size);
            aLink.appendChild(sSpan);
          }
          assetsWrap.appendChild(aLink);
        });
        actionsRow.appendChild(assetsWrap);
      }

      card.appendChild(actionsRow);
    }

    return card;
  }

  function renderChangelog() {
    if (!changelogListEl) return;
    var q = state.searchQuery.trim().toLowerCase();
    var all = state.releases;

    var matched = q
      ? all.filter(function (r) {
          var hay = (r.tag + " " + r.version + " " + r.date + " " + r.subtitle + " " + r.markdown).toLowerCase();
          return hay.indexOf(q) !== -1;
        })
      : all;

    changelogListEl.innerHTML = "";

    if (!matched.length) {
      var empty = document.createElement("div");
      empty.className = "cl-empty";
      empty.textContent = '未在 GitHub 更新日志中找到匹配「' + state.searchQuery.trim() + '」的版本记录。';
      changelogListEl.appendChild(empty);
      if (changelogMoreWrap) changelogMoreWrap.hidden = true;
      return;
    }

    var toShow = (q || state.expandedAll) ? matched : matched.slice(0, INITIAL_VISIBLE_RELEASES);
    var frag = document.createDocumentFragment();
    toShow.forEach(function (rel, idx) {
      frag.appendChild(createReleaseCard(rel, idx, Boolean(q)));
    });
    changelogListEl.appendChild(frag);

    if (changelogMoreWrap && changelogMoreBtn) {
      if (q || matched.length <= INITIAL_VISIBLE_RELEASES) {
        changelogMoreWrap.hidden = true;
      } else {
        changelogMoreWrap.hidden = false;
        var restCount = matched.length - INITIAL_VISIBLE_RELEASES;
        changelogMoreBtn.textContent = state.expandedAll
          ? "收起更早版本"
          : ("加载更早版本（还有 " + restCount + " 个历史版本）");
      }
    }
  }

  function fetchWithTimeout(url, options, timeoutMs) {
    var controller = typeof AbortController !== "undefined" ? new AbortController() : null;
    var timer = controller ? setTimeout(function () { controller.abort(); }, timeoutMs) : 0;
    var opts = Object.assign({}, options || {});
    if (controller) opts.signal = controller.signal;

    return fetch(url, opts).finally(function () {
      if (timer) clearTimeout(timer);
    });
  }

  function readCache(allowExpired) {
    try {
      var raw = localStorage.getItem(CACHE_KEY);
      if (!raw) return null;
      var parsed = JSON.parse(raw);
      if (!parsed || !Array.isArray(parsed.releases) || !parsed.releases.length) return null;
      if (!allowExpired && (Date.now() - Number(parsed.timestamp || 0) > CACHE_TTL_MS)) {
        return null;
      }
      return parsed;
    } catch (e) {
      return null;
    }
  }

  function writeCache(sourceLabel, releases) {
    try {
      localStorage.setItem(CACHE_KEY, JSON.stringify({
        timestamp: Date.now(),
        sourceLabel: sourceLabel,
        releases: releases
      }));
    } catch (e) {}
  }

  function loadFromGitHub(forceRefresh) {
    if (!forceRefresh) {
      var cached = readCache(false);
      if (cached) {
        state.releases = cached.releases;
        syncHeroAndDownload(state.releases);
        renderChangelog();
        setStatus("live", (cached.sourceLabel || "已同步自 GitHub") + " · 本地缓存");
        return Promise.resolve();
      }
    }

    setStatus("loading", "正在从 GitHub 获取最新日志…");

    return fetchWithTimeout(RELEASES_API_URL, {
      headers: { Accept: "application/vnd.github+json" }
    }, 5000)
      .then(function (res) {
        if (!res.ok) throw new Error("API HTTP " + res.status);
        return res.json();
      })
      .then(function (json) {
        var rels = parseReleasesApi(json);
        if (!rels.length) throw new Error("Empty GitHub releases");
        return { kind: "live", sourceLabel: "已同步自 GitHub Releases (" + rels.length + " 个版本)", releases: rels };
      })
      .catch(function () {
        return fetchWithTimeout(RAW_CHANGELOG_URL, {}, 6000)
          .then(function (res) {
            if (!res.ok) throw new Error("Raw HTTP " + res.status);
            return res.text();
          })
          .then(function (md) {
            var rels = parseChangelogMarkdown(md);
            if (!rels.length) throw new Error("Empty CHANGELOG.md");
            return { kind: "live", sourceLabel: "已同步自 GitHub CHANGELOG.md (" + rels.length + " 个版本)", releases: rels };
          });
      })
      .catch(function () {
        return fetchWithTimeout(JSDELIVR_CHANGELOG_URL, {}, 6000)
          .then(function (res) {
            if (!res.ok) throw new Error("jsDelivr HTTP " + res.status);
            return res.text();
          })
          .then(function (md) {
            var rels = parseChangelogMarkdown(md);
            if (!rels.length) throw new Error("Empty mirror CHANGELOG.md");
            return { kind: "live", sourceLabel: "已同步自 GitHub 镜像 (" + rels.length + " 个版本)", releases: rels };
          });
      })
      .then(function (result) {
        state.releases = result.releases;
        writeCache(result.sourceLabel, result.releases);
        syncHeroAndDownload(state.releases);
        renderChangelog();
        setStatus(result.kind, result.sourceLabel);
        if (forceRefresh) showToast("更新日志已从 GitHub 刷新");
      })
      .catch(function () {
        var stale = readCache(true);
        if (stale) {
          state.releases = stale.releases;
          syncHeroAndDownload(state.releases);
          renderChangelog();
          setStatus("fallback", "GitHub 暂不可达 · 显示历史缓存");
        } else {
          state.releases = FALLBACK_RELEASES;
          syncHeroAndDownload(state.releases);
          renderChangelog();
          setStatus("fallback", "离线快照 · 可点击刷新重试");
        }
        if (forceRefresh) showToast("GitHub 连接受限，已保留可用版本记录");
      });
  }

  if (changelogSearchEl) {
    changelogSearchEl.addEventListener("input", function () {
      state.searchQuery = changelogSearchEl.value || "";
      renderChangelog();
    });
  }

  if (changelogMoreBtn) {
    changelogMoreBtn.addEventListener("click", function () {
      state.expandedAll = !state.expandedAll;
      renderChangelog();
    });
  }

  if (changelogRefreshBtn) {
    changelogRefreshBtn.addEventListener("click", function () {
      try { localStorage.removeItem(CACHE_KEY); } catch (e) {}
      loadFromGitHub(true);
    });
  }

  loadFromGitHub(false);
})();
