// Injected into every page (main world) by Browser over DevTools. Talks back
// through the __berryEvent binding with one JSON object per call:
//   {t:"touch"}                       a finger went down on the page
//   {t:"menu", link, text, image}     long press on a link or an image
//   {t:"select", text}                text selection mode started
//   {t:"selection", text}             the selection changed
//   {t:"selectionEnd"}                selection mode ended in the page
//   {t:"editing", on}                 an editable field gained/lost focus
// Browser calls window.__berry.* through Runtime.evaluate.
//
// Page event listeners only run while the page's JavaScript is on, so with
// JavaScript off none of this works; the one-window part (window.open) still
// does, since it is plain assignment.
(function () {
  if (window.__berry) return;

  var LONG_PRESS_MS = 550;
  var SLOP = 10;
  var HANDLE_REACH = 36;

  function report(o) {
    try { window.__berryEvent(JSON.stringify(o)); } catch (e) {}
  }

  // ---- one window -------------------------------------------------------

  window.open = function (u) {
    if (u) location.href = new URL(u, location.href).href;
    return window;
  };

  // ---- touches and long press ------------------------------------------

  var press = null;
  var suppressClick = false;
  var lastTouchReport = 0;

  function cancelPress() {
    if (press) clearTimeout(press.timer);
    press = null;
  }

  function editable(el) {
    if (!el) return false;
    if (el.isContentEditable) return true;
    var tag = el.tagName;
    if (tag === "TEXTAREA") return !el.readOnly && !el.disabled;
    if (tag !== "INPUT") return false;
    return !/^(button|checkbox|color|file|hidden|image|radio|range|reset|submit)$/i.test(el.type) &&
        !el.readOnly && !el.disabled;
  }

  document.addEventListener("touchstart", function (e) {
    var now = Date.now();
    if (now - lastTouchReport > 500) {
      lastTouchReport = now;
      report({t: "touch"});
    }
    cancelPress();
    if (e.touches.length !== 1) return;
    var t = e.touches[0];
    press = {x: t.clientX, y: t.clientY, target: e.target, moved: false};
    press.timer = setTimeout(longPress, LONG_PRESS_MS);
  }, {capture: true, passive: true});

  document.addEventListener("touchmove", function (e) {
    if (!press) return;
    var t = e.touches[0];
    if (Math.abs(t.clientX - press.x) > SLOP || Math.abs(t.clientY - press.y) > SLOP) {
      press.moved = true;
      clearTimeout(press.timer);
    }
  }, {capture: true, passive: true});

  document.addEventListener("touchend", function () {
    var p = press;
    cancelPress();
    if (suppressClick) {
      setTimeout(function () { suppressClick = false; }, 500);
    } else if (p && !p.moved && selecting && !drag) {
      endSelection(true);
    }
  }, {capture: true, passive: true});

  document.addEventListener("touchcancel", cancelPress, {capture: true, passive: true});

  // A long press must not also activate what is under the finger.
  document.addEventListener("click", function (e) {
    if (suppressClick) {
      e.preventDefault();
      e.stopPropagation();
      suppressClick = false;
      return;
    }
    var a = e.target && e.target.closest ? e.target.closest("a[target]") : null;
    if (a && a.target !== "_self" && a.href) a.target = "_self";
  }, true);

  document.addEventListener("contextmenu", function (e) {
    if (!editable(e.target)) e.preventDefault();
  }, true);

  function longPress() {
    var p = press;
    press = null;
    if (!p) return;
    var el = p.target && p.target.nodeType === 1 ? p.target : (p.target && p.target.parentElement);
    if (!el || editable(el)) return;
    var link = el.closest("a[href]");
    var img = el.closest("img");
    suppressClick = true;
    if (link || img) {
      report({
        t: "menu",
        link: link ? link.href : "",
        text: link ? (link.innerText || "").trim().slice(0, 200) : "",
        image: img ? (img.currentSrc || img.src || "") : ""
      });
      return;
    }
    if (!selectWordAt(p.x, p.y)) suppressClick = false;
  }

  // ---- text selection ----------------------------------------------------

  var selecting = false;
  var drag = null;          // "start" or "end" while a handle is dragged
  var handles = null;
  var lastText = "";
  var raf = 0;

  function selectWordAt(x, y) {
    var r = document.caretRangeFromPoint(x, y);
    if (!r || r.startContainer.nodeType !== 3) return false;
    var s = getSelection();
    s.removeAllRanges();
    s.collapse(r.startContainer, r.startOffset);
    s.modify("move", "forward", "character");
    s.modify("move", "backward", "word");
    s.modify("extend", "forward", "word");
    if (!s.toString().trim()) {
      s.removeAllRanges();
      return false;
    }
    startSelection();
    return true;
  }

  function makeHandle() {
    var h = document.createElement("div");
    h.style.cssText = "position:fixed;z-index:2147483647;width:14px;height:22px;" +
        "margin:0;padding:0;border:0;background:#7a40c4;pointer-events:none;" +
        "border-radius:0 0 7px 7px;box-shadow:0 1px 2px rgba(0,0,0,.4);display:none";
    document.documentElement.appendChild(h);
    return h;
  }

  function edges() {
    var s = getSelection();
    if (!s.rangeCount) return null;
    var rects = s.getRangeAt(0).getClientRects();
    if (!rects.length) return null;
    var a = rects[0], b = rects[rects.length - 1];
    return {start: {x: a.left, y: a.bottom}, end: {x: b.right, y: b.bottom}};
  }

  function placeHandles() {
    raf = 0;
    if (!handles) return;
    var e = edges();
    if (!e) {
      handles.start.style.display = handles.end.style.display = "none";
      return;
    }
    handles.start.style.display = handles.end.style.display = "block";
    handles.start.style.left = (e.start.x - 14) + "px";
    handles.start.style.top = e.start.y + "px";
    handles.end.style.left = e.end.x + "px";
    handles.end.style.top = e.end.y + "px";
  }

  function schedule() {
    if (!raf) raf = requestAnimationFrame(placeHandles);
  }

  function onSelectionChange() {
    if (!selecting) return;
    schedule();
    var text = getSelection().toString();
    if (text !== lastText) {
      lastText = text;
      report({t: "selection", text: text.slice(0, 10000)});
    }
  }

  function near(p, x, y) {
    return Math.abs(p.x - x) < HANDLE_REACH && y > p.y - HANDLE_REACH && y < p.y + HANDLE_REACH + 22;
  }

  function onSelectTouchStart(e) {
    drag = null;
    if (e.touches.length !== 1) return;
    var t = e.touches[0], ed = edges();
    if (!ed) return;
    if (near(ed.end, t.clientX, t.clientY)) drag = "end";
    else if (near(ed.start, t.clientX, t.clientY)) drag = "start";
    if (drag) {
      cancelPress();
      e.preventDefault();
    }
  }

  function onSelectTouchMove(e) {
    if (!drag) return;
    e.preventDefault();
    var t = e.touches[0];
    // Aim above the finger, which covers the text it is moving over.
    var r = document.caretRangeFromPoint(t.clientX, t.clientY - 28);
    if (!r) return;
    var s = getSelection();
    if (!s.rangeCount) return;
    var cur = s.getRangeAt(0);
    if (drag === "end") {
      if (cur.comparePoint(r.startContainer, r.startOffset) < 0) return;
      s.setBaseAndExtent(cur.startContainer, cur.startOffset, r.startContainer, r.startOffset);
    } else {
      if (cur.comparePoint(r.startContainer, r.startOffset) > 0) return;
      s.setBaseAndExtent(r.startContainer, r.startOffset, cur.endContainer, cur.endOffset);
    }
  }

  function onSelectTouchEnd() {
    if (drag) setTimeout(function () { drag = null; }, 0);
  }

  function startSelection() {
    if (!handles) handles = {start: makeHandle(), end: makeHandle()};
    lastText = getSelection().toString();
    if (!selecting) {
      selecting = true;
      document.addEventListener("selectionchange", onSelectionChange);
      addEventListener("scroll", schedule, true);
      addEventListener("resize", schedule);
      document.addEventListener("touchstart", onSelectTouchStart, {capture: true, passive: false});
      document.addEventListener("touchmove", onSelectTouchMove, {capture: true, passive: false});
      document.addEventListener("touchend", onSelectTouchEnd, true);
    }
    report({t: "select", text: lastText.slice(0, 10000)});
    schedule();
  }

  function endSelection(notify) {
    if (!selecting) return;
    selecting = false;
    drag = null;
    document.removeEventListener("selectionchange", onSelectionChange);
    removeEventListener("scroll", schedule, true);
    removeEventListener("resize", schedule);
    document.removeEventListener("touchstart", onSelectTouchStart, {capture: true, passive: false});
    document.removeEventListener("touchmove", onSelectTouchMove, {capture: true, passive: false});
    document.removeEventListener("touchend", onSelectTouchEnd, true);
    if (handles) handles.start.style.display = handles.end.style.display = "none";
    getSelection().removeAllRanges();
    if (notify) report({t: "selectionEnd"});
  }

  // ---- editing -------------------------------------------------------------

  var editing = false;
  function updateEditing() {
    var on = editable(document.activeElement);
    if (on !== editing) {
      editing = on;
      report({t: "editing", on: on});
    }
  }
  document.addEventListener("focusin", updateEditing, true);
  document.addEventListener("focusout", function () { setTimeout(updateEditing, 0); }, true);

  // ---- called by Browser -------------------------------------------------

  window.__berry = {
    selectionText: function () {
      return getSelection().toString();
    },
    clearSelection: function () {
      endSelection(false);
    },
    selectAll: function () {
      var el = document.activeElement;
      if (editable(el) && el.select) {
        el.select();
      } else {
        var s = getSelection();
        s.removeAllRanges();
        var r = document.createRange();
        r.selectNodeContents(document.body || document.documentElement);
        s.addRange(r);
      }
      startSelection();
      return getSelection().toString().length;
    },
    // Finds text from the current selection on; returns {found, count}.
    find: function (text, backwards) {
      if (selecting) endSelection(false);
      if (!text) return {found: false, count: 0};
      var found = window.find(text, false, !!backwards, true, false, false, false);
      var body = (document.body && document.body.innerText) || "";
      var hay = body.toLowerCase(), needle = text.toLowerCase(), count = 0, i = 0;
      while ((i = hay.indexOf(needle, i)) !== -1 && count < 9999) { count++; i += needle.length; }
      return {found: found, count: count};
    },
    scrollY: function () {
      return window.scrollY;
    },
    scrollTo: function (y) {
      window.scrollTo(0, y);
    }
  };
})();
