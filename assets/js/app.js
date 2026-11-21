(function () {
  "use strict";

  var root = document.documentElement;
  var header = document.querySelector(".site-header");
  var nav = document.getElementById("nav");
  var navToggle = document.getElementById("nav-toggle");
  var themeToggle = document.getElementById("theme-toggle");

  /* ---------- theme ---------- */

  var stored = null;
  try { stored = localStorage.getItem("qv-theme"); } catch (e) {}
  if (stored === "light" || stored === "dark") {
    root.setAttribute("data-theme", stored);
  } else if (window.matchMedia && window.matchMedia("(prefers-color-scheme: light)").matches) {
    root.setAttribute("data-theme", "light");
  }

  themeToggle.addEventListener("click", function () {
    var next = root.getAttribute("data-theme") === "light" ? "dark" : "light";
    root.setAttribute("data-theme", next);
    try { localStorage.setItem("qv-theme", next); } catch (e) {}
  });

  /* ---------- header / nav ---------- */

  function onScroll() {
    header.classList.toggle("scrolled", window.scrollY > 8);
  }
  onScroll();
  window.addEventListener("scroll", onScroll, { passive: true });

  navToggle.addEventListener("click", function () {
    var open = nav.classList.toggle("open");
    navToggle.setAttribute("aria-expanded", open ? "true" : "false");
  });

  nav.addEventListener("click", function (e) {
    if (e.target.tagName === "A") {
      nav.classList.remove("open");
      navToggle.setAttribute("aria-expanded", "false");
    }
  });

  /* ---------- gallery ---------- */

  var shots = [
    { file: "01-main.webp", title: "主窗口", desc: "顶部工具栏和底部控制栏" },
    { file: "02-thumbnails.webp", title: "缩略图浏览", desc: "在底部缩略图栏切换图片" },
    { file: "03-info.webp", title: "图片信息", desc: "EXIF / GPS / 直方图，Tab 呼出" },
    { file: "04-spread.webp", title: "双页阅览", desc: "画册、漫画并排看" },
    { file: "05-zoom.webp", title: "缩放与平移", desc: "滚轮缩放、拖拽平移、一键适配窗口" },
    { file: "06-effects.webp", title: "图像效果", desc: "反色 / Gamma / 旋转 / 插值" },
    { file: "07-context-menu.webp", title: "右键菜单", desc: "按功能分组，标出快捷键" },
    { file: "08-convert.webp", title: "批量转换", desc: "PNG / JPEG / BMP / WebP / TIFF" },
    { file: "09-settings.webp", title: "设置", desc: "九个分类，通用页可导入和导出设置" },
    { file: "10-context-menu-settings.webp", title: "菜单设置", desc: "按组设置菜单项，右侧查看预览" },
    { file: "11-start.webp", title: "启动画面", desc: "拖入图片、选择文件或打开文件夹" },
    { file: "12-overview.webp", title: "图片总览", desc: "按网格查看同一文件夹里的图片" }
  ];

  var stageImg = document.getElementById("gallery-img");
  var stageTitle = document.getElementById("gallery-title");
  var stageDesc = document.getElementById("gallery-desc");
  var thumbsWrap = document.getElementById("thumbs");
  var current = 1;

  shots.forEach(function (shot, i) {
    var btn = document.createElement("button");
    btn.type = "button";
    btn.className = "thumb";
    btn.setAttribute("role", "tab");
    btn.setAttribute("aria-label", shot.title);
    btn.setAttribute("aria-selected", i === current ? "true" : "false");
    var img = document.createElement("img");
    img.src = "assets/img/v0.5.6/" + shot.file;
    img.alt = shot.title;
    img.loading = "lazy";
    img.width = 320;
    img.height = 240;
    btn.appendChild(img);
    btn.addEventListener("click", function () { show(i); });
    thumbsWrap.appendChild(btn);
  });

  function show(i) {
    current = (i + shots.length) % shots.length;
    var shot = shots[current];
    stageImg.classList.add("swapping");
    var pre = new Image();
    pre.onload = function () {
      stageImg.src = "assets/img/v0.5.6/" + shot.file;
      stageImg.alt = shot.title;
      stageImg.classList.remove("swapping");
    };
    pre.src = "assets/img/v0.5.6/" + shot.file;
    stageTitle.textContent = shot.title;
    stageDesc.textContent = shot.desc;
    Array.prototype.forEach.call(thumbsWrap.children, function (btn, idx) {
      var on = idx === current;
      btn.classList.toggle("active", on);
      btn.setAttribute("aria-selected", on ? "true" : "false");
    });
  }

  document.getElementById("gallery-prev").addEventListener("click", function () { show(current - 1); });
  document.getElementById("gallery-next").addEventListener("click", function () { show(current + 1); });

  show(current);

  /* ---------- lightbox ---------- */

  var lightbox = document.getElementById("lightbox");
  var lbImg = document.getElementById("lb-img");
  var lbCap = document.getElementById("lb-cap");

  function openLightbox() {
    var shot = shots[current];
    lbImg.src = "assets/img/v0.5.6/" + shot.file;
    lbImg.alt = shot.title;
    lbCap.textContent = shot.title + " — " + shot.desc;
    lightbox.classList.add("open");
    lightbox.setAttribute("aria-hidden", "false");
    document.body.style.overflow = "hidden";
  }

  function closeLightbox() {
    lightbox.classList.remove("open");
    lightbox.setAttribute("aria-hidden", "true");
    document.body.style.overflow = "";
  }

  stageImg.addEventListener("click", openLightbox);
  document.getElementById("lb-close").addEventListener("click", closeLightbox);
  lightbox.addEventListener("click", function (e) {
    if (e.target === lightbox) closeLightbox();
  });
  document.addEventListener("keydown", function (e) {
    if (e.key === "Escape" && lightbox.classList.contains("open")) closeLightbox();
    if (lightbox.classList.contains("open")) {
      if (e.key === "ArrowLeft") { show(current - 1); openLightbox(); }
      if (e.key === "ArrowRight") { show(current + 1); openLightbox(); }
    }
  });

  /* ---------- active nav link ---------- */

  var sections = ["features", "gallery", "formats", "download", "updates"];
  var navLinks = {};
  sections.forEach(function (id) {
    navLinks[id] = document.querySelector('.nav a[href="#' + id + '"]');
  });

  if ("IntersectionObserver" in window) {
    var waypoints = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (!entry.isIntersecting) return;
        Object.keys(navLinks).forEach(function (id) {
          if (navLinks[id]) navLinks[id].classList.toggle("active", id === entry.target.id);
        });
      });
    }, { rootMargin: "-45% 0px -50% 0px" });
    sections.forEach(function (id) {
      var el = document.getElementById(id);
      if (el) waypoints.observe(el);
    });
  }
})();
