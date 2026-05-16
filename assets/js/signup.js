// Sirius Looper — signup.js
// Handles email-capture form submit + builds the doc-page TOC from h2s.
(function () {
  "use strict";

  function buildDocToc() {
    var list = document.getElementById("doc-toc-list");
    if (!list) return;
    var body = document.querySelector(".doc-body");
    if (!body) return;
    var headings = body.querySelectorAll("h2");
    if (!headings.length) {
      list.parentNode.style.display = "none";
      return;
    }
    headings.forEach(function (h, i) {
      if (!h.id) {
        h.id = (h.textContent || "section-" + i)
          .toLowerCase()
          .replace(/[^a-z0-9]+/g, "-")
          .replace(/^-|-$/g, "");
      }
      var li = document.createElement("li");
      var a = document.createElement("a");
      a.href = "#" + h.id;
      a.textContent = h.textContent;
      li.appendChild(a);
      list.appendChild(li);
    });
  }

  function attachSignup() {
    document.querySelectorAll("form.signup-form").forEach(function (form) {
      form.addEventListener("submit", function (e) {
        var endpoint = form.getAttribute("action");
        var honeypot = form.querySelector('input[name="company"]');
        if (honeypot && honeypot.value) {
          e.preventDefault();
          return;
        }
        if (!endpoint) {
          e.preventDefault();
          setStatus(form, "Sign-up isn't wired up yet — check back soon.", "error");
          return;
        }
        e.preventDefault();
        var data = new FormData(form);
        setStatus(form, "Sending…", "");
        fetch(endpoint, { method: "POST", body: data, headers: { Accept: "application/json" } })
          .then(function (res) {
            if (!res.ok) throw new Error("HTTP " + res.status);
            form.reset();
            setStatus(form, "Thanks — you're on the list.", "success");
          })
          .catch(function () {
            setStatus(form, "Something went wrong. Try again in a moment.", "error");
          });
      });
    });
  }

  function setStatus(form, msg, kind) {
    var el = form.parentNode.querySelector(".signup-status");
    if (!el) return;
    el.textContent = msg;
    el.classList.remove("is-success", "is-error");
    if (kind) el.classList.add("is-" + kind);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", function () { buildDocToc(); attachSignup(); });
  } else {
    buildDocToc();
    attachSignup();
  }
})();
