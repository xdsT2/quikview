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
    { file: "01-main.webp", title: "主窗口", desc: "顶栏与画布同色，搭配底部控制栏" },
    { file: "02-overview.webp", title: "图片总览", desc: "v0.6.0 重做网格，按文件夹快速定位照片" },
    { file: "02-thumbnails.webp", title: "缩略图浏览", desc: "在底部缩略图栏切换图片" },
    { file: "03-info.webp", title: "图片信息", desc: "EXIF / GPS / 直方图，Tab 呼出" },
    { file: "04-svg.webp", title: "SVG 矢量查看", desc: "直接打开并无损查看 SVG 矢量图" },
    { file: "05-psd.webp", title: "PSD / PSB 图层", desc: "查看内嵌缩略图与图层预览" },
    { file: "04-spread.webp", title: "双页阅览", desc: "画册、漫画并排看" },
    { file: "05-zoom.webp", title: "缩放与平移", desc: "滚轮缩放、拖拽平移、一键适配窗口" },
    { file: "06-effects.webp", title: "图像效果", desc: "反色 / Gamma / 旋转 / 插值" },
    { file: "07-context-menu.webp", title: "右键菜单", desc: "按功能分组，统一悬浮与点击反馈" },
    { file: "08-convert.webp", title: "批量转换", desc: "PNG / JPEG / BMP / WebP / TIFF" },
    { file: "09-settings.webp", title: "设置", desc: "十个分类，修改即时生效并支持导入导出" },
    { file: "10-about.webp", title: "关于页", desc: "设置新增「关于」分类，查看版本与组件信息" },
    { file: "11-start.webp", title: "启动画面", desc: "拖入图片、选择文件或打开文件夹" }
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
    img.src = "assets/img/v0.6.0/" + shot.file;
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
      stageImg.src = "assets/img/v0.6.0/" + shot.file;
      stageImg.alt = shot.title;
      stageImg.classList.remove("swapping");
    };
    pre.src = "assets/img/v0.6.0/" + shot.file;
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
    lbImg.src = "assets/img/v0.6.0/" + shot.file;
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

  /* ---------- copy QQ group ---------- */

  function copyText(text) {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      return navigator.clipboard.writeText(text).catch(function () {
        return copyFallback(text);
      });
    }
    return Promise.resolve(copyFallback(text));
  }

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

  Array.prototype.forEach.call(document.querySelectorAll("[data-qq]"), function (btn) {
    var timerId = 0;
    btn.addEventListener("click", function () {
      var qq = btn.getAttribute("data-qq") || "";
      var label = btn.getAttribute("data-qq-label") || btn.textContent;
      var copied = btn.getAttribute("data-qq-copied") || ("已复制：" + qq);
      copyText(qq).then(function () {
        btn.textContent = copied;
        if (timerId) clearTimeout(timerId);
        timerId = setTimeout(function () {
          btn.textContent = label;
          timerId = 0;
        }, 2000);
      });
    });
  });
})();
