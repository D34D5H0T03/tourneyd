"use strict";

const SLUG_RE = /^[a-z0-9-]{1,64}$/;
const FILE_RE = /\/files\/[a-z0-9-]{1,64}\/[A-Za-z0-9._-]{1,100}/g;
const SOLVED_KEY = "tourneyd.solved";

class ApiError extends Error {
  constructor(status, message, response) {
    super(message);
    this.status = status;
    this.response = response || null;
  }
}

async function api(path, options) {
  let response;
  try {
    response = await fetch(path, options);
  } catch (err) {
    throw new ApiError(0, "Cannot reach the server. Check connection.");
  }
  let data = null;
  if (
    (response.headers.get("Content-Type") || "").includes("application/json")
  ) {
    try {
      data = await response.json();
    } catch (err) {
      data = null;
    }
  }
  if (!response.ok) {
    throw new ApiError(
      response.status,
      (data && data.error) || `Request failed (${response.status}).`,
      response,
    );
  }
  return data;
}

function el(tag, attrs, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs || {})) {
    if (key === "class") node.className = value;
    else node.setAttribute(key, value);
  }
  for (const child of children) node.append(child);
  return node;
}

function param(name) {
  return new URLSearchParams(location.search).get(name);
}
function fmtDate(unixSeconds) {
  return new Date(unixSeconds * 1000).toISOString().slice(0, 10);
}

function renderText(container, text) {
  container.textContent = "";
  let last = 0;
  for (const match of text.matchAll(FILE_RE)) {
    if (match.index > last) container.append(text.slice(last, match.index));
    container.append(
      el("a", { href: match[0], style: "color: var(--accent);" }, match[0]),
    );
    last = match.index + match[0].length;
  }
  if (last < text.length) container.append(text.slice(last));
}

function safeExternalUrl(value) {
  try {
    const u = new URL(value);
    return u.protocol === "https:" || u.protocol === "http:" ? u.href : null;
  } catch (err) {
    return null;
  }
}

function solvedSet() {
  try {
    return new Set(JSON.parse(localStorage.getItem(SOLVED_KEY) || "[]"));
  } catch (err) {
    return new Set();
  }
}

function markSolved(slug) {
  try {
    const set = solvedSet();
    set.add(slug);
    localStorage.setItem(SOLVED_KEY, JSON.stringify([...set]));
  } catch (err) {}
}
