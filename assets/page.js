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

  // ---- swipe to go back/forward ----------------------------------------
  // The engine turns a horizontal overscroll into Back/Forward unless the
  // page's root says overscroll-behavior-x: none. Off unless Settings turns
  // it on; Browser also applies this after every load, which keeps it
  // working when the page's JavaScript is off.

  var swipe = false;

  function applySwipe() {
    var els = [document.documentElement, document.body];
    for (var i = 0; i < els.length; i++) {
      if (!els[i]) continue;
      if (swipe) els[i].style.removeProperty("overscroll-behavior-x");
      else els[i].style.setProperty("overscroll-behavior-x", "none", "important");
    }
  }
  applySwipe();
  document.addEventListener("DOMContentLoaded", applySwipe);

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

  // Chromium's own long press on text starts a selection of its own, with
  // its native touch handles, which crash Berry's engine (a failed CHECK in
  // flat_map, as in Berry Browser). It comes late, after the page has been
  // busy, so only cancelling it works; this script selects with a Range.
  document.addEventListener("selectstart", function (e) {
    if (!editable(e.target && e.target.nodeType === 1 ? e.target : e.target && e.target.parentElement))
      e.preventDefault();
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
  // The page's own selection is never used for this: whatever Chromium sees
  // selected after a long press gets its native touch handles, and those
  // crash Berry's engine. The selected text is a Range instead, drawn here as
  // highlight boxes with two handles.

  var selecting = false;
  var range = null;         // what is selected
  var drag = null;          // "start" or "end" while a handle is dragged
  var layer = null;         // {boxes, start, end}
  var lastText = "";
  var raf = 0;
  var words = new Intl.Segmenter(undefined, {granularity: "word"});
  var BLUE = "#00a8df";

  function selectWordAt(x, y) {
    var c = document.caretRangeFromPoint(x, y);
    if (!c || c.startContainer.nodeType !== 3) return false;
    var node = c.startContainer, at = c.startOffset;
    var segments = words.segment(node.data);
    // The caret can sit just after the word the finger is on.
    var word = segments.containing(at);
    if ((!word || !word.isWordLike) && at > 0) word = segments.containing(at - 1);
    if (!word || !word.isWordLike) return false;
    range = document.createRange();
    range.setStart(node, word.index);
    range.setEnd(node, word.index + word.segment.length);
    startSelection();
    return true;
  }

  function fixed(css) {
    var el = document.createElement("div");
    el.style.cssText = "position:fixed;z-index:2147483647;margin:0;padding:0;border:0;" +
        "pointer-events:none;display:none;" + css;
    document.documentElement.appendChild(el);
    return el;
  }

  // A handle: a line along the text's edge with a round grip below it.
  function makeHandle() {
    var h = fixed("width:2px;background:" + BLUE);
    var grip = document.createElement("div");
    grip.style.cssText = "position:absolute;left:-8px;bottom:-16px;width:18px;height:18px;" +
        "border-radius:9px;background:" + BLUE + ";box-shadow:0 1px 2px rgba(0,0,0,.4)";
    h.appendChild(grip);
    return h;
  }

  function hide() {
    if (!layer) return;
    layer.start.style.display = layer.end.style.display = "none";
    for (var i = 0; i < layer.boxes.length; i++) layer.boxes[i].style.display = "none";
  }

  function edges() {
    var rects = range ? range.getClientRects() : [];
    if (!rects.length) return null;
    var a = rects[0], b = rects[rects.length - 1];
    return {rects: rects, start: {x: a.left, top: a.top, y: a.bottom},
            end: {x: b.right, top: b.top, y: b.bottom}};
  }

  function placeHandles() {
    raf = 0;
    if (!layer) return;
    var e = edges();
    hide();
    if (!e) return;
    // Highlight what is on screen, at most 200 boxes (select all).
    var shown = 0;
    for (var i = 0; i < e.rects.length && shown < 200; i++) {
      var r = e.rects[i];
      if (r.bottom < 0 || r.top > innerHeight || !r.width) continue;
      var box = layer.boxes[shown] || (layer.boxes[shown] = fixed("background:rgba(0,168,223,.3)"));
      box.style.display = "block";
      box.style.left = r.left + "px";
      box.style.top = r.top + "px";
      box.style.width = r.width + "px";
      box.style.height = r.height + "px";
      shown++;
    }
    var ends = [[layer.start, e.start], [layer.end, e.end]];
    for (var j = 0; j < 2; j++) {
      var h = ends[j][0], p = ends[j][1];
      h.style.display = "block";
      h.style.left = (p.x - 1) + "px";
      h.style.top = p.top + "px";
      h.style.height = (p.y - p.top) + "px";
    }
  }

  function schedule() {
    if (!raf) raf = requestAnimationFrame(placeHandles);
  }

  function changed() {
    schedule();
    var text = range ? range.toString() : "";
    if (text !== lastText) {
      lastText = text;
      report({t: "selection", text: text.slice(0, 10000)});
    }
  }

  function near(p, x, y) {
    return Math.abs(p.x - x) < HANDLE_REACH && y > p.top - HANDLE_REACH && y < p.y + HANDLE_REACH + 18;
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
    if (!drag || !range) return;
    e.preventDefault();
    var t = e.touches[0];
    // Aim above the finger, which covers the text it is moving over.
    var r = document.caretRangeFromPoint(t.clientX, t.clientY - 28);
    if (!r) return;
    if (drag === "end") {
      if (range.comparePoint(r.startContainer, r.startOffset) < 0) return;
      range.setEnd(r.startContainer, r.startOffset);
    } else {
      if (range.comparePoint(r.startContainer, r.startOffset) > 0) return;
      range.setStart(r.startContainer, r.startOffset);
    }
    changed();
  }

  function onSelectTouchEnd() {
    if (drag) setTimeout(function () { drag = null; }, 0);
  }

  function startSelection() {
    if (!layer) layer = {boxes: [], start: makeHandle(), end: makeHandle()};
    lastText = range ? range.toString() : getSelection().toString();
    if (!selecting) {
      selecting = true;
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
    range = null;
    removeEventListener("scroll", schedule, true);
    removeEventListener("resize", schedule);
    document.removeEventListener("touchstart", onSelectTouchStart, {capture: true, passive: false});
    document.removeEventListener("touchmove", onSelectTouchMove, {capture: true, passive: false});
    document.removeEventListener("touchend", onSelectTouchEnd, true);
    hide();
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
    setSwipe: function (on) {
      swipe = !!on;
      applySwipe();
    },
    selectionText: function () {
      return range ? range.toString() : getSelection().toString();
    },
    clearSelection: function () {
      endSelection(false);
    },
    selectAll: function () {
      var el = document.activeElement;
      if (editable(el) && el.select) {
        // Inside a field the field's own selection is what gets copied.
        range = null;
        el.select();
      } else {
        range = document.createRange();
        range.selectNodeContents(document.body || document.documentElement);
      }
      startSelection();
      return lastText.length;
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
