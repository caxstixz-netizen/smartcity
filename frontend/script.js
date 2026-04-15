'use strict';

/* ═══════════════════════════════════════════════════════════════════════════
   CONSTANTS
   ═══════════════════════════════════════════════════════════════════════════ */

const API_BASE    = 'https://smartcity-backend-0fjj.onrender.com/api';
const MAP_CENTER  = [19.056, 72.832];
const MAP_ZOOM    = 14;
const OSRM_BASE   = 'https://router.project-osrm.org/route/v1/driving';
const CACHE_KEY   = 'bandra_road_polylines_v4';
const CACHE_TTL   = 7 * 24 * 60 * 60 * 1000;
const FETCH_CONCURRENCY = 8;

const ZOMBIE_LINES = [
  "Don't forget to aim for the head.",
  "Rule 1: Cardio. Rule 2: Double tap. Rule 3: Get out of Mumbai.",
  "The good news: traffic has finally cleared. The bad news: you know why.",
  "Grab a cricket bat. Gonna fight our way through this zombie jam.",
];

/* ═══════════════════════════════════════════════════════════════════════════
   APPLICATION STATE
   ══════════════════════════════════════════════════════════════════════════ */

let map;
let nodeMarkers        = {};
let edgeObjects        = [];
let pathPolyline       = null;
let evacPolyline       = null;
let animationMarker    = null;
let blockedEdgeIndices = new Set();
let selectMode         = 0;
let selectedSource     = null;
let selectedTarget     = null;

// ── Traffic state ────────────────────────────────────────────────────────────
// Keyed by "from-to" (normalised min-max), value = { multiplier, severity }
let trafficData        = {};
let trafficSource      = null;   // EventSource instance
let lastActiveAlgo     = null;   // tracks most recent { source, target, algo } for auto-rerun
let trafficEnabled     = true;

window.ROAD_POLYLINES = {};

/* ═══════════════════════════════════════════════════════════════════════════
   OSRM ROAD ROUTING — CACHE WITH 7-DAY TTL
   ═══════════════════════════════════════════════════════════════════════════ */

// Decodes a Google-encoded polyline string into [[lat, lng], ...] coordinates.
// See: https://developers.google.com/maps/documentation/utilities/polylinealgorithm
function decodePolyline(encoded) {
  const coords = [];
  let index = 0, lat = 0, lng = 0;
  while (index < encoded.length) {
    let b, shift = 0, result = 0;
    do { b = encoded.charCodeAt(index++) - 63; result |= (b & 0x1f) << shift; shift += 5; } while (b >= 0x20);
    lat += (result & 1) ? ~(result >> 1) : (result >> 1);
    shift = 0; result = 0;
    do { b = encoded.charCodeAt(index++) - 63; result |= (b & 0x1f) << shift; shift += 5; } while (b >= 0x20);
    lng += (result & 1) ? ~(result >> 1) : (result >> 1);
    coords.push([lat / 1e5, lng / 1e5]);
  }
  return coords;
}

/* ── localStorage cache helpers ─────────────────────────────────────────── */
function loadPolylineCache() {
  try {
    const raw = localStorage.getItem(CACHE_KEY);
    if (!raw) return;
    const { ts, data } = JSON.parse(raw);
    if (Date.now() - ts < CACHE_TTL) {
      Object.assign(window.ROAD_POLYLINES, data);
      console.log(`[OSRM] Loaded ${Object.keys(data).length} cached polylines.`);
    } else {
      localStorage.removeItem(CACHE_KEY);
      console.log('[OSRM] Cache expired — will re-fetch.');
    }
  } catch (e) {
    console.warn('[OSRM] Cache load error:', e);
  }
}

function savePolylineCache() {
  try {
    localStorage.setItem(CACHE_KEY, JSON.stringify({
      ts:   Date.now(),
      data: window.ROAD_POLYLINES,
    }));
    console.log(`[OSRM] Saved ${Object.keys(window.ROAD_POLYLINES).length} polylines to cache.`);
  } catch (e) {
    console.warn('[OSRM] Cache save error (storage full?):', e);
  }
}

/* ── Single edge fetch ───────────────────────────────────────────────────── */
async function fetchOSRMRoute(fromNode, toNode) {
  const url = `${OSRM_BASE}/${fromNode.lng},${fromNode.lat};${toNode.lng},${toNode.lat}`
            + `?overview=full&geometries=polyline`;
  try {
    const res = await fetch(url, { signal: AbortSignal.timeout(8000) });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const json = await res.json();
    if (json.code !== 'Ok' || !json.routes?.length) return null;
    const coords = decodePolyline(json.routes[0].geometry);
    return coords.length >= 2 ? coords : null;
  } catch (err) {
    console.warn(`[OSRM] ${fromNode.name} → ${toNode.name}: ${err.message}`);
    return null;
  }
}

/* ── Upgrade all edges to real-road polylines ───────────────────────────── */
async function upgradeEdgesToRealRoads() {
  loadPolylineCache();

  const missing = EDGES
    .map((edge, idx) => ({ edge, idx, key: `${edge.from}-${edge.to}` }))
    .filter(({ key }) => !window.ROAD_POLYLINES[key]);

  if (missing.length === 0) {
    applyRoadPolylines();
    hideMapSpinner();
    console.log('[OSRM] All polylines already cached — skipping fetch.');
    return;
  }

  showMapSpinner(`Loading road geometry… 0 / ${missing.length}`);
  console.log(`[OSRM] Fetching ${missing.length} edges (concurrency ${FETCH_CONCURRENCY})…`);

  let done = 0;
  let saved = 0;

  for (let i = 0; i < missing.length; i += FETCH_CONCURRENCY) {
    const batch = missing.slice(i, i + FETCH_CONCURRENCY);

    await Promise.all(batch.map(async ({ edge, key }) => {
      const from = nodeById(edge.from);
      const to   = nodeById(edge.to);
      if (!from || !to) { done++; return; }

      const pts = await fetchOSRMRoute(from, to);
      if (pts) {
        window.ROAD_POLYLINES[key] = pts;
        saved++;
      }
      done++;
      showMapSpinner(`Loading road geometry… ${done} / ${missing.length}`);
    }));

    applyRoadPolylines();
    if (saved > 0) {
      savePolylineCache();
      saved = 0;
    }
  }

  hideMapSpinner();
  console.log('[OSRM] Upgrade complete.');
}

function applyRoadPolylines() {
  EDGES.forEach((edge, idx) => {
    const key = `${edge.from}-${edge.to}`;
    const pts = ROAD_POLYLINES[key];
    const ep  = edgeObjects[idx];
    if (pts && pts.length >= 2 && ep?.polyline) {
      ep.polyline.setLatLngs(pts);
      if (ep.hitArea)    ep.hitArea.setLatLngs(pts);
      if (ep.centerLine) ep.centerLine.setLatLngs(pts);
    }
  });
}

/* ── Map spinner helpers ────────────────────────────────────────────────── */
function showMapSpinner(msg) {
  const el = document.getElementById('mapLoadingOverlay');
  if (!el) return;
  el.querySelector('span').textContent = msg || 'Loading…';
  el.classList.remove('hidden');
}
function hideMapSpinner() {
  const el = document.getElementById('mapLoadingOverlay');
  if (el) el.classList.add('hidden');
}

/* ═══════════════════════════════════════════════════════════════════════════
   UTILITY HELPERS
   ═══════════════════════════════════════════════════════════════════════════ */

function nodeById(id) {
  return NODES.find(n => n.id === id) ?? NODES[id] ?? null;
}
function nodeName(id) {
  const n = nodeById(id); return n ? n.name : `Node ${id}`;
}
function pathStr(ids) {
  if (!ids || ids.length === 0) return '—';
  return ids.map(nodeName).join(' → ');
}
function shortPath(ids) {
  if (!ids || ids.length === 0) return '—';
  if (ids.length <= 2) {
    return ids.map(nodeName).join(' → ');
  }
  const first = nodeName(ids[0]);
  const last = nodeName(ids[ids.length - 1]);
  return `${first} → [${ids.length - 2} more] → ${last}`;
}

function toast(msg, isError = false) {
  const el = document.getElementById('toast');
  el.textContent = msg;
  el.className   = 'toast' + (isError ? ' is-error' : '');
  clearTimeout(el._t);
  el._t = setTimeout(() => el.classList.add('hidden'), 3800);
}

function setBusy(busy) {
  ['runAllBtn', 'autoDecideBtn', 'resetBtn', 'evacuateBtn'].forEach(id => {
    const el = document.getElementById(id);
    if (el) el.disabled = busy;
  });
}

async function apiPost(path, body) {
  let res;
  try {
    res = await fetch(`${API_BASE}${path}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
  } catch {
    throw new Error(`Cannot reach server at ${API_BASE}. Is the C++ backend running?`);
  }
  const data = await res.json();
  if (!res.ok) throw new Error(data.error ?? `HTTP ${res.status}`);
  return data;
}

/* ═══════════════════════════════════════════════════════════════════════════
   SERVER STATUS
   ═══════════════════════════════════════════════════════════════════════════ */

async function checkServer() {
  const dot   = document.getElementById('serverDot');
  const label = document.getElementById('serverLabel');
  try {
    const res  = await fetch(`${API_BASE}/status`, { signal: AbortSignal.timeout(4000) });
    const text = await res.text();
    let ok = false;
    try { ok = res.ok && JSON.parse(text).status === 'ok'; } catch { ok = res.ok; }
    if (ok) {
      dot.className     = 'indicator-dot online';
      label.textContent = 'Server online';
      ['runAllBtn','autoDecideBtn','resetBtn','evacuateBtn'].forEach(id => {
        const el = document.getElementById(id);
        if (el) el.disabled = false;
      });
    } else { throw new Error(); }
  } catch {
    dot.className     = 'indicator-dot offline';
    label.textContent = 'Server offline';
  }
  setTimeout(checkServer, 5000);
}

/* ═══════════════════════════════════════════════════════════════════════════
   MAP INITIALISATION
   ═══════════════════════════════════════════════════════════════════════════ */

function initMap() {
  const mapEl = document.getElementById('map');
  if (mapEl._leaflet_id) return;

  map = L.map('map', {
    center: MAP_CENTER,
    zoom:   MAP_ZOOM,
    zoomControl: true,
    attributionControl: true,
  });

  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
    maxZoom: 19,
  }).addTo(map);

  setTimeout(() => map.invalidateSize(), 100);

  const mapCol = mapEl.closest('.map-col');
  if (mapCol && !document.getElementById('mapFrostOverlay')) {
    const frost = document.createElement('div');
    frost.id = 'mapFrostOverlay';
    mapCol.appendChild(frost);
  }

  drawNodes();
  drawEdges();

  const cachedCount = Object.keys(ROAD_POLYLINES).length;
  if (cachedCount > 0) applyRoadPolylines();

  upgradeEdgesToRealRoads();
}

/* ═══════════════════════════════════════════════════════════════════════════
   NODE MARKERS
   ═══════════════════════════════════════════════════════════════════════════ */

function drawNodes() {
  Object.values(nodeMarkers).forEach(m => m.remove());
  nodeMarkers = {};

  NODES.forEach(node => {
    const marker = makeNodeMarker(node);
    marker.addTo(map);
    marker.bindTooltip(
      `<b>${node.name}</b><br>ID: ${node.id}${node.type ? `<br>${node.type}` : ''}`,
      { direction: 'top', offset: [0, -16] }
    );
    marker.on('click', () => onNodeClick(node.id));
    nodeMarkers[node.id] = marker;
  });
}

function makeNodeMarker(node) {
  const isSrc = node.id === selectedSource;
  const isTgt = node.id === selectedTarget;
  const isSafe = node.type === 'safe';
  let cls = 'map-node-label';
  if (isSrc) cls += ' is-source';
  if (isTgt) cls += ' is-target';
  if (isSafe) cls += ' is-safe';

  const icon = L.divIcon({
    className:  '',
    html:       `<div class="${cls}" data-label="${node.name}"></div>`,
    iconAnchor: [12, 12],
  });
  return L.marker([node.lat, node.lng], { icon });
}

function refreshNodeMarker(nodeId) {
  const node   = nodeById(nodeId);
  const marker = nodeMarkers[nodeId];
  if (!node || !marker) return;

  const isSrc = nodeId === selectedSource;
  const isTgt = nodeId === selectedTarget;
  const isSafe = node.type === 'safe';
  let cls = 'map-node-label';
  if (isSrc) cls += ' is-source';
  if (isTgt) cls += ' is-target';
  if (isSafe) cls += ' is-safe';

  marker.setIcon(L.divIcon({
    className:  '',
    html:       `<div class="${cls}" data-label="${node.name}"></div>`,
    iconAnchor: [12, 12],
  }));
}

function onNodeClick(nodeId) {
  const prevSource = selectedSource;
  const prevTarget = selectedTarget;

  if (selectMode === 0) {
    selectedSource = nodeId;
    selectMode = 1;
  } else {
    if (nodeId === selectedSource) {
      toast('Source and target cannot be the same node.', true);
      return;
    }
    selectedTarget = nodeId;
    selectMode = 0;
  }

  const toRefresh = new Set([prevSource, prevTarget, selectedSource, selectedTarget]);
  toRefresh.forEach(id => { if (id != null) refreshNodeMarker(id); });
  syncDropdownsFromMap();
}

/* ═══════════════════════════════════════════════════════════════════════════
   EDGE POLYLINES
   ═══════════════════════════════════════════════════════════════════════════ */

const EDGE_STYLE_NORMAL  = { color: '#4a4a4a', weight: 4, opacity: 0.9 };
const EDGE_STYLE_BLOCKED = { color: '#bdbdbd', weight: 3, opacity: 0.85, dashArray: '8 5' };
const EDGE_STYLE_CENTER  = { color: 'rgba(255,255,255,0.5)', weight: 1.5, opacity: 1, dashArray: '4 8' };
const EDGE_HIT_STYLE = { color: 'transparent', weight: 14, opacity: 0, interactive: true };

function drawEdges() {
  edgeObjects.forEach(e => {
    if (e?.hitArea)    e.hitArea.remove();
    if (e?.polyline)   e.polyline.remove();
    if (e?.centerLine) e.centerLine.remove();
  });
  edgeObjects = [];

  EDGES.forEach((edge, idx) => {
    const fromNode = nodeById(edge.from);
    const toNode   = nodeById(edge.to);
    if (!fromNode || !toNode) { edgeObjects.push(null); return; }

    const blocked = blockedEdgeIndices.has(idx);
    const cacheKey = `${edge.from}-${edge.to}`;
    const pts = (ROAD_POLYLINES[cacheKey]?.length >= 2)
      ? ROAD_POLYLINES[cacheKey]
      : [[fromNode.lat, fromNode.lng], [toNode.lat, toNode.lng]];

    const edgeType = edge.weight < 700 ? 'Primary Road' : 'Secondary Road';

    const edgeKey = `${Math.min(edge.from, edge.to)}-${Math.max(edge.from, edge.to)}`;
    const trafficStyle = trafficEdgeStyle(edgeKey, blocked);
    const polyline = L.polyline(pts, trafficStyle).addTo(map);

    let centerLine = null;
    if (!blocked) {
      centerLine = L.polyline(pts, EDGE_STYLE_CENTER).addTo(map);
    }

    const hitArea = L.polyline(pts, { ...EDGE_HIT_STYLE }).addTo(map);

    // Tooltip content is a closure so it always reads the latest trafficData
    const tooltipContent = () => {
      const edgeKey = `${Math.min(edge.from, edge.to)}-${Math.max(edge.from, edge.to)}`;
      const td = trafficData[edgeKey];
      const trafficLine = td
        ? `<br>Traffic: <b style="color:${td.severity === 'red' ? '#e74c3c' : td.severity === 'yellow' ? '#f39c12' : '#27ae60'}">${td.severity.toUpperCase()}</b> (×${td.multiplier.toFixed(2)})`
        : '';
      return `<b>${fromNode.name} → ${toNode.name}</b><br>` +
        `Distance: ${edge.weight} m<br>Type: ${edgeType}` +
        trafficLine +
        `<br><i style="opacity:0.6">Click to ${blocked ? 'unblock' : 'block'}</i>`;
    };

    hitArea.bindTooltip(tooltipContent(), { sticky: true });

    hitArea.on('mouseover', () => {
      if (!blocked) {
        polyline.setStyle({ color: '#2F2F2F', weight: 4, opacity: 1 });
        if (centerLine) centerLine.setStyle({ opacity: 0.7 });
      }
      hitArea.getElement()?.setAttribute('style', 'cursor: pointer;');
    });
    hitArea.on('mouseout', () => {
      if (!blocked) {
        polyline.setStyle({ ...EDGE_STYLE_NORMAL });
        if (centerLine) centerLine.setStyle({ opacity: 1 });
      }
    });

    hitArea.on('click', () => toggleEdgeBlock(idx));

    edgeObjects.push({ polyline, centerLine, hitArea, blocked });
  });
}

function toggleEdgeBlock(edgeIdx) {
  const ep   = edgeObjects[edgeIdx];
  const edge = EDGES[edgeIdx];
  if (!ep || !edge) return;

  const nowBlocked = !blockedEdgeIndices.has(edgeIdx);
  if (nowBlocked) blockedEdgeIndices.add(edgeIdx);
  else            blockedEdgeIndices.delete(edgeIdx);

  ep.blocked = nowBlocked;

  if (nowBlocked) {
    ep.polyline.setStyle(EDGE_STYLE_BLOCKED);
    if (ep.centerLine) ep.centerLine.setStyle({ opacity: 0 });
  } else {
    ep.polyline.setStyle({ ...EDGE_STYLE_NORMAL });
    if (ep.centerLine) ep.centerLine.setStyle({ opacity: 1 });
  }

  const edgeType = edge.weight < 700 ? 'Primary Road' : 'Secondary Road';
  ep.hitArea.setTooltipContent(
    `<b>${nodeName(edge.from)} → ${nodeName(edge.to)}</b><br>` +
    `Distance: ${edge.weight} m<br>Type: ${edgeType}<br>` +
    `<i style="opacity:0.6">Click to ${nowBlocked ? 'unblock' : 'block'}</i>`
  );
  updateBlockedBar();
}

function updateBlockedBar() {
  const bar = document.getElementById('blockedBar');
  if (blockedEdgeIndices.size === 0) {
    bar.textContent = 'No edges blocked.';
    bar.classList.remove('has-blocks');
    return;
  }
  const labels = [...blockedEdgeIndices].map(idx => {
    const e = EDGES[idx];
    return `${nodeName(e.from)}→${nodeName(e.to)}`;
  });
  bar.textContent = `Blocked (${blockedEdgeIndices.size}): ${labels.join(', ')}`;
  bar.classList.add('has-blocks');
}

function getBlockedEdgeList() {
  return [...blockedEdgeIndices].map(idx => [EDGES[idx].from, EDGES[idx].to]);
}

/* ═══════════════════════════════════════════════════════════════════════════
   DROPDOWNS
   ═══════════════════════════════════════════════════════════════════════════ */

function populateDropdowns() {
  const srcSel    = document.getElementById('sourceSelect');
  const tgtSel    = document.getElementById('targetSelect');
  const disLocSel = document.getElementById('disasterLoc');

  [srcSel, tgtSel, disLocSel].forEach(sel => { sel.innerHTML = ''; });

  const sortedNodes = [...NODES].sort((a, b) => a.name.localeCompare(b.name));

  sortedNodes.forEach(node => {
    const makeOpt = () => {
      const o = document.createElement('option');
      o.value = node.id;
      o.textContent = `${node.name} (ID ${node.id})`;
      return o;
    };
    srcSel.appendChild(makeOpt());
    tgtSel.appendChild(makeOpt());
    disLocSel.appendChild(makeOpt());
  });

  if (sortedNodes.length > 0) {
    selectedSource = sortedNodes[0].id;
    srcSel.value = sortedNodes[0].id;
  }
  if (sortedNodes.length > 1) {
    selectedTarget = sortedNodes[sortedNodes.length - 1].id;
    tgtSel.value = sortedNodes[sortedNodes.length - 1].id;
  }

  if (selectedSource != null) refreshNodeMarker(selectedSource);
  if (selectedTarget != null) refreshNodeMarker(selectedTarget);

  // Use a guard flag to prevent double-attaching listeners if populateDropdowns is called again
  if (!srcSel.hasListener) {
    srcSel.addEventListener('change', () => {
      const prev = selectedSource;
      selectedSource = parseInt(srcSel.value);
      if (selectedSource === selectedTarget) {
        toast('Source and target cannot be the same.', true);
        selectedSource = prev;
        srcSel.value = prev;
        return;
      }
      refreshNodeMarker(prev);
      refreshNodeMarker(selectedSource);
    });
    srcSel.hasListener = true;
  }

  if (!tgtSel.hasListener) {
    tgtSel.addEventListener('change', () => {
      const prev = selectedTarget;
      selectedTarget = parseInt(tgtSel.value);
      if (selectedTarget === selectedSource) {
        toast('Source and target cannot be the same.', true);
        selectedTarget = prev;
        tgtSel.value = prev;
        return;
      }
      refreshNodeMarker(prev);
      refreshNodeMarker(selectedTarget);
    });
    tgtSel.hasListener = true;
  }
}

function syncDropdownsFromMap() {
  if (selectedSource != null) document.getElementById('sourceSelect').value = selectedSource;
  if (selectedTarget != null) document.getElementById('targetSelect').value = selectedTarget;
}

/* ═══════════════════════════════════════════════════════════════════════════
   PATH ANIMATION
   ═══════════════════════════════════════════════════════════════════════════ */

function pathToLatLngs(nodeIds) {
  const pts = [];
  for (let i = 0; i < nodeIds.length - 1; i++) {
    const a = nodeById(nodeIds[i]);
    const b = nodeById(nodeIds[i + 1]);
    if (!a || !b) continue;

    const fwdKey = `${nodeIds[i]}-${nodeIds[i + 1]}`;
    const revKey = `${nodeIds[i + 1]}-${nodeIds[i]}`;
    let seg = ROAD_POLYLINES[fwdKey];
    if (!seg && ROAD_POLYLINES[revKey]) {
      seg = [...ROAD_POLYLINES[revKey]].reverse();
    }

    if (seg && seg.length >= 2) {
      if (pts.length > 0) pts.push(...seg.slice(1));
      else                pts.push(...seg);
    } else {
      if (pts.length > 0) pts.push([b.lat, b.lng]);
      else                pts.push([a.lat, a.lng], [b.lat, b.lng]);
    }
  }
  return pts;
}

function animatePath(nodeIds, color, onDone) {
  // Always clear both before starting a new trace
  if (pathPolyline)    { pathPolyline.remove();    pathPolyline    = null; }
  if (evacPolyline)    { evacPolyline.remove();    evacPolyline    = null; }
  if (animationMarker) { animationMarker.remove(); animationMarker = null; }

  if (!nodeIds || nodeIds.length < 2) { if (onDone) onDone(); return null; }

  // ── STEP 1+3: Precompute full path BEFORE animation starts (no mid-frame lookups) ──
  const allPts = pathToLatLngs(nodeIds);
  if (allPts.length < 2) { if (onDone) onDone(); return null; }

  const isEvac     = (color === '#B08968');

  // All path types use the same dark navy color; `color` param is kept for API compatibility
  const lineColor  = '#0D1B2A';
  const lineWeight = isEvac ? 7 : 8;
  const glowColor  = 'rgba(13,27,42,0.15)';

  // ── STEP 3: Pre-build LatLng objects to avoid repeated array creation in rAF ──
  const latLngs = allPts.map(p => L.latLng(p[0], p[1]));

  const glowLine = L.polyline([], {
    color: glowColor, weight: lineWeight + 8, opacity: 1,
  }).addTo(map);

  const finalLine = L.polyline([], {
    color: lineColor, weight: lineWeight, opacity: 1,
  }).addTo(map);

  // Store in the correct slot so reset works
  if (isEvac) evacPolyline = finalLine;
  else        pathPolyline = finalLine;

  // ── STEP 1: Preload edges are already drawn — show overview immediately ──
  map.fitBounds(L.latLngBounds(allPts), { padding: [70, 70], animate: true, duration: 0.8 });

  // ── STEP 1: Lerp camera state ──────────────────────────────────────────────
  const FOLLOW_ZOOM    = 16;
  const FOLLOW_DELAY   = 900;    // ms before follow cam kicks in
  const MS_PER_POINT   = latLngs.length > 80 ? 180 : 230;
  const LERP_SPEED     = 0.04;   // 0–1: lower = slower/smoother lerp
  const CAM_MIN_DIST   = 0.0003; // only lerp if marker moved at least this much (degrees)

  let followEnabled  = false;
  let camLat         = null;
  let camLng         = null;
  let lastPanRaf     = 0;        // timestamp of last camera pan rAF
  let idx            = 0;

  setTimeout(() => {
    followEnabled = true;
    const startPt = latLngs[Math.min(idx, latLngs.length - 1)];
    camLat = startPt.lat;
    camLng = startPt.lng;
    map.setView([camLat, camLng], FOLLOW_ZOOM, { animate: true, duration: 1.2, easeLinearity: 0.1 });
  }, FOLLOW_DELAY);

  let lastTime = null;

  function step(ts) {
    if (!lastTime) lastTime = ts;
    const elapsed = Math.min(ts - lastTime, MS_PER_POINT * 4);
    lastTime = ts;

    // ── STEP 3: Batch add points — avoid addLatLng inside tight loops ──
    const steps = Math.max(1, Math.floor(elapsed / MS_PER_POINT));
    const batchPts = [];
    for (let s = 0; s < steps && idx < latLngs.length; s++, idx++) {
      batchPts.push(latLngs[idx]);
    }
    if (batchPts.length) {
      const cur = finalLine.getLatLngs();
      const gCur = glowLine.getLatLngs();
      finalLine.setLatLngs(cur.concat(batchPts));
      glowLine.setLatLngs(gCur.concat(batchPts));
    }

    if (idx < latLngs.length) {
      const currentPt = latLngs[idx];

      if (!animationMarker) {
        animationMarker = L.circleMarker(currentPt, {
          radius: 9, color: '#fff', fillColor: lineColor,
          fillOpacity: 1, weight: 2.5, zIndexOffset: 1000,
        }).addTo(map);
      } else {
        animationMarker.setLatLng(currentPt);
      }

      // ── STEP 1: Smooth lerp follow camera — no abrupt jumps ──
      if (followEnabled && camLat !== null) {
        const tLat = currentPt.lat;
        const tLng = currentPt.lng;
        const dist  = Math.abs(tLat - camLat) + Math.abs(tLng - camLng);

        if (dist > CAM_MIN_DIST) {
          camLat += (tLat - camLat) * LERP_SPEED;
          camLng += (tLng - camLng) * LERP_SPEED;

          // Only issue a pan every ~80ms to avoid flooding Leaflet's pan queue
          if (ts - lastPanRaf > 80) {
            lastPanRaf = ts;
            map.panTo([camLat, camLng], {
              animate: true, duration: 0.45,
              easeLinearity: 0.3, noMoveStart: true,
            });
          }
        }
      }

      requestAnimationFrame(step);
    } else {
      if (animationMarker) { animationMarker.remove(); animationMarker = null; }
      setTimeout(() => {
        map.fitBounds(L.latLngBounds(allPts), { padding: [60, 60], animate: true, duration: 1.2 });
      }, 400);
      if (onDone) onDone();
    }
  }

  requestAnimationFrame(step);
  return finalLine;
}

function clearPathAnimation() {
  if (pathPolyline)    { pathPolyline.remove();    pathPolyline    = null; }
  if (evacPolyline)    { evacPolyline.remove();    evacPolyline    = null; }
  if (animationMarker) { animationMarker.remove(); animationMarker = null; }
}

/* ═══════════════════════════════════════════════════════════════════════════
   ALGO PROGRESS INDICATOR
   ═══════════════════════════════════════════════════════════════════════════ */

function showAlgoProgress(msg) {
  const el = document.getElementById('algoProgress');
  if (!el) return;
  el.querySelector('span').textContent = msg;
  el.classList.remove('hidden');
}
function hideAlgoProgress() {
  const el = document.getElementById('algoProgress');
  if (el) el.classList.add('hidden');
}

/* ═══════════════════════════════════════════════════════════════════════════
   RUN ALL ALGORITHMS
   ═══════════════════════════════════════════════════════════════════════════ */

async function runAllAlgorithms() {
  const source  = parseInt(document.getElementById('sourceSelect').value);
  const target  = parseInt(document.getElementById('targetSelect').value);
  const blocked = getBlockedEdgeList();
  const raiotVal = (document.getElementById('riotInput') || { value: '' }).value.trim();
  const raiot    = raiotVal !== '' ? parseFloat(raiotVal) : -1;

  lastActiveAlgo = { algo: 'all', source, target };

  const algoConfigs = [
    { key: 'dijkstra',  label: 'Dijkstra'    },
    { key: 'bellman',   label: 'Bellman-Ford' },
    { key: 'backtrack', label: 'Backtracking' },
  ];

  setBusy(true);
  clearPathAnimation();
  document.getElementById('resultsWrap').classList.remove('hidden');
  document.getElementById('resultsBody').innerHTML =
    `<tr><td colspan="5" style="text-align:center;color:var(--text-muted);font-style:italic;padding:14px">
      <span style="display:inline-flex;align-items:center;gap:8px">
        <span class="spinner" style="width:12px;height:12px"></span>
        Querying backend…
      </span>
    </td></tr>`;
  showAlgoProgress('Running algorithms…');

  // All three fire in parallel — total wait = slowest single call
  const settled = await Promise.allSettled(
    algoConfigs.map(({ key }) =>
      apiPost('/solve', { source, target, algorithm: key, blockedEdges: blocked, raiot })
    )
  );

  const results = {};
  algoConfigs.forEach(({ key }, i) => {
    const s = settled[i];
    results[key] = s.status === 'fulfilled'
      ? { ok: true, ...s.value }
      : { ok: false, error: s.reason.message };
  });

  // ── Table appears IMMEDIATELY ───────────────────────────────────────────
  renderResultsTable(results, algoConfigs);
  hideAlgoProgress();
  setBusy(false);   // buttons re-enabled right now, don't wait for animation

  // ── STEP 1: Ensure road polylines applied before animation ──
  applyRoadPolylines();

  // ── Animation is fire-and-forget decorative only ────────────────────────
  const successful = Object.values(results).filter(r => r.ok && r.path?.length > 0);
  if (successful.length) {
    const best = successful.reduce((a, b) => a.cost <= b.cost ? a : b);
    animatePath(best.path, '#66A66F', () => {});
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
   RESULTS TABLE
   ═══════════════════════════════════════════════════════════════════════════ */

function renderResultsTable(results, algoConfigs) {
  const tbody  = document.getElementById('resultsBody');
  const table  = tbody.closest('table');
  const thead  = table.querySelector('thead');
  tbody.innerHTML = '';
  thead.innerHTML = '';
  table.className = 'results-table';

  const costs   = algoConfigs.map(a => results[a.key]?.ok ? results[a.key].cost : Infinity);
  const minCost = Math.min(...costs);

  const n = NODES.length;
  const hasNeg = EDGES.some(e => e.weight < 0);
  let chosenKey, chosenReason;
  if (n < 8)      { chosenKey = 'backtrack'; chosenReason = `Small graph (${n} nodes) — exhaustive search optimal`; }
  else if (hasNeg){ chosenKey = 'bellman';   chosenReason = 'Negative edge weights — Bellman-Ford required'; }
  else            { chosenKey = 'dijkstra';  chosenReason = `${n} nodes, non-neg weights — Dijkstra fastest & optimal`; }

  const hRow = document.createElement('tr');
  hRow.innerHTML = `<th></th>` + algoConfigs.map(({ key, label }) => {
    const isBest   = results[key]?.ok && results[key].cost === minCost;
    const isChosen = key === chosenKey;
    return `<th class="${isBest ? 'col-best' : ''}">${label}${isChosen ? ' <span class="star">★</span>' : ''}</th>`;
  }).join('');
  thead.appendChild(hRow);

  const rows = [
    {
      label: 'Path',
      render: key => {
        const r = results[key];
        if (!r?.ok) return `<span style="color:var(--text-muted);font-size:11px">${r?.error ?? 'Error'}</span>`;
        const fullPathStr = pathStr(r.path);
        return `<span title="${fullPathStr}" style="cursor:help;font-size:11px;border-bottom:1px dotted var(--border)">${shortPath(r.path)}</span>`;
      },
    },
    {
      label: 'Cost (m)',
      render: key => {
        const r = results[key];
        if (!r?.ok) return '—';
        const best = r.cost === minCost;
        return `<strong style="color:${best ? 'var(--path-green)' : 'inherit'}">${r.cost?.toFixed(1) ?? '—'}</strong>`;
      },
    },
    {
      label: 'Time (µs)',
      render: key => results[key]?.ok ? (results[key].timeUs ?? '—') : '—',
    },
    {
      label: 'Status',
      render: key => {
        const r = results[key];
        if (!r) return '—';
        return r.ok ? `<span class="cell-ok">OK</span>` : `<span class="cell-err">ERR</span>`;
      },
    },
    {
      label: 'Why chosen',
      render: key => {
        if (key !== chosenKey) return '<span style="color:var(--border)">—</span>';
        return `<span style="font-size:10.5px;font-style:italic;color:var(--text-muted)">${chosenReason}</span>`;
      },
    },
  ];

  rows.forEach(row => {
    const tr = document.createElement('tr');
    tr.innerHTML = `<td class="metric-label">${row.label}</td>` +
      algoConfigs.map(({ key }) => {
        const isBest = results[key]?.ok && results[key].cost === minCost;
        return `<td class="${isBest ? 'col-best' : ''}">${row.render(key)}</td>`;
      }).join('');
    tbody.appendChild(tr);
  });
}

/* ═══════════════════════════════════════════════════════════════════════════
   AUTO-DECIDE
   ═══════════════════════════════════════════════════════════════════════════ */

async function autoDecide() {
  const n       = NODES.length;
  const source  = parseInt(document.getElementById('sourceSelect').value);
  const target  = parseInt(document.getElementById('targetSelect').value);
  const blocked = getBlockedEdgeList();
  const raiotVal = (document.getElementById('riotInput') || { value: '' }).value.trim();
  const raiot    = raiotVal !== '' ? parseFloat(raiotVal) : -1;

  const hasNeg = EDGES.some(e => e.weight < 0);
  let chosenKey, reasoning;
  if (n < 8)      { chosenKey = 'backtrack'; reasoning = `Graph has only ${n} nodes — exhaustive backtracking guarantees the true optimum.`; }
  else if (hasNeg){ chosenKey = 'bellman';   reasoning = 'Negative-weight edges detected — Bellman-Ford handles these correctly.'; }
  else            { chosenKey = 'dijkstra';  reasoning = `${n} nodes with non-negative weights — Dijkstra is fastest and optimal.`; }

  const algoLabel = { dijkstra: 'Dijkstra', bellman: 'Bellman-Ford', backtrack: 'Backtracking' };

  setBusy(true);
  clearPathAnimation();
  showAlgoProgress(`Running ${algoLabel[chosenKey]}…`);
  try {
    const res = await apiPost('/solve', { source, target, algorithm: chosenKey, blockedEdges: blocked, raiot });
    const box = document.getElementById('decisionBox');
    document.getElementById('decisionText').innerHTML =
      `<strong>${algoLabel[chosenKey]}</strong> selected.<br>${reasoning}<br>` +
      `<span style="font-family:var(--mono);font-size:11px;color:var(--text-muted)">` +
      `Path: ${shortPath(res.path)} — Cost: ${res.cost?.toFixed(1)} m — ${res.timeUs} µs</span>`;
    box.classList.remove('hidden');
    hideAlgoProgress();
    setBusy(false);
    applyRoadPolylines(); // STEP 1: ensure edges preloaded
    if (res.path?.length) animatePath(res.path, '#66A66F', () => {});
  } catch (err) {
    hideAlgoProgress();
    setBusy(false);
    toast(err.message, true);
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
   DISASTER EVACUATION
   ═══════════════════════════════════════════════════════════════════════════ */

function updatePublicTransportNotice(disasterType) {
  const noticeEl = document.getElementById('transportNotice');
  if (!noticeEl) return;

  const notices = {
    'urban_flood':       'Urban flooding has disrupted all public transport (buses, trains, autos). Only walking evacuation routes are available.',
    'storm_surge':       'Storm surge warning: Coastal roads may be submerged. Public transport suspended. Evacuate on foot immediately.',
    'cyclone':           'Cyclone alert: All public transport halted. Strong winds may cause debris — seek shelter on high ground.',
    'earthquake':        'Earthquake damage: Roads may be cracked, public transport stopped. Use caution when evacuating.',
    'heatwave':          'Heatwave advisory: Public transport operating but crowded. Stay hydrated while evacuating.',
    'post_flood_disease':'Disease outbreak risk: Avoid stagnant water. Public transport limited in affected zones.',
    'sea_level_rise':    'Long-term sea level rise: Some coastal routes may be permanently affected. Plan alternative routes.',
  };

  noticeEl.textContent = notices[disasterType] || '⚠️ Public transport may be disrupted due to this disaster.';
  noticeEl.classList.remove('hidden');
}

async function evacuate() {
  const disaster    = document.getElementById('disasterType').value;
  const locationId  = parseInt(document.getElementById('disasterLoc').value);

  const panel = document.getElementById('evacResult');
  panel.classList.add('hidden');
  setBusy(true);
  showAlgoProgress('Computing evacuation route…');

  updatePublicTransportNotice(disaster);

  try {
    const blocked = getBlockedEdgeList();
    const data = await apiPost('/disaster', { disaster, location: locationId, blockedEdges: blocked });
    hideAlgoProgress();
    renderEvacResult(data, disaster);
    panel.classList.remove('hidden');
    setBusy(false);
  } catch (err) {
    hideAlgoProgress();
    setBusy(false);
    toast(err.message, true);
  }
}

function renderEvacResult(data, disaster) {
  const panel = document.getElementById('evacResult');
  panel.classList.remove('hidden');

  const coastalAlert = document.getElementById('coastalAlert');
  if (coastalAlert) {
    if (disaster === 'storm_surge' || disaster === 'urban_flood') {
      coastalAlert.classList.remove('hidden');
    } else {
      coastalAlert.classList.add('hidden');
    }
  }

  document.getElementById('evacSafeNode').textContent = data.safeNodeName ?? nodeName(data.safeNode);
  document.getElementById('evacDistance').textContent =
    typeof data.distance === 'number' ? `${data.distance.toFixed(0)} m` : (data.distance ?? '—');
  document.getElementById('evacAlgo').textContent = data.chosenAlgorithm ?? '—';

  const chosenKey = (data.chosenAlgorithm ?? '').toLowerCase().replace(/[^a-z]/g, '');
  const chosenRes = data.results?.[chosenKey] ?? Object.values(data.results ?? {})[0] ?? {};
  const pathIds   = chosenRes.path ?? [];

  document.getElementById('evacRoute').textContent = pathStr(pathIds);

  const jokeLine = document.getElementById('zombieLine');
  if (disaster === 'zombie apocalypse' || disaster === 'zombie') {
    jokeLine.textContent = ZOMBIE_LINES[Math.floor(Math.random() * ZOMBIE_LINES.length)];
    jokeLine.classList.remove('hidden');
  } else {
    jokeLine.classList.add('hidden');
  }

  const evacReasons = {
    dijkstra:  'Fastest runtime — optimal for real-time evacuation on non-negative graph',
    bellman:   'Handles disaster-modified edge weights (high/inf costs)',
    backtrack: 'Small graph — exhaustive search guarantees safest exit',
  };

  const tbody = document.getElementById('evacBreakdown');
  tbody.innerHTML = '';
  const labelMap = { dijkstra: 'Dijkstra', bellman: 'Bellman-Ford', backtrack: 'Backtracking' };
  Object.entries(data.results ?? {}).forEach(([key, r]) => {
    const isBest = key === chosenKey;
    const tr = document.createElement('tr');
    if (isBest) tr.style.background = 'rgba(102,166,111,0.09)';
    tr.innerHTML = `
      <td style="font-weight:${isBest?'700':'400'};color:${isBest?'var(--path-green)':'inherit'}">${labelMap[key] ?? key}${isBest?' ★':''}</td>
      <td class="cell-mono">${typeof r?.cost === 'number' ? r.cost.toFixed(0) : '—'}</td>
      <td class="cell-mono">${r?.timeUs ?? '—'}</td>
      <td style="font-size:10.5px;color:var(--text-muted);font-style:italic">${isBest ? (evacReasons[key]??'—') : '—'}</td>`;
    tbody.appendChild(tr);
  });

  if (evacPolyline) { evacPolyline.remove(); evacPolyline = null; }
  if (pathIds.length) {
    applyRoadPolylines(); // STEP 1: ensure edges preloaded
    const line = animatePath(pathIds, '#B08968', () => {});
    if (line) evacPolyline = line;
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
   REAL-TIME TRAFFIC
   ═══════════════════════════════════════════════════════════════════════════ */

function trafficEdgeStyle(edgeKey, blocked) {
  if (blocked) return EDGE_STYLE_BLOCKED;
  const td = trafficData[edgeKey];
  if (!td) return { ...EDGE_STYLE_NORMAL };
  if (td.severity === 'red')    return { color: '#c0392b', weight: 4.5, opacity: 0.95 };
  if (td.severity === 'yellow') return { color: '#e67e22', weight: 4,   opacity: 0.92 };
  return { color: '#1e8449', weight: 4, opacity: 0.90 };
}

function applyTrafficToMap(updates) {
  updates.forEach(u => {
    const key = u.edgeId;
    trafficData[key] = { multiplier: u.multiplier, severity: u.severity };
  });

  EDGES.forEach((edge, idx) => {
    const ep = edgeObjects[idx];
    if (!ep || !ep.polyline) return;
    if (ep.blocked) return;
    const key = `${Math.min(edge.from, edge.to)}-${Math.max(edge.from, edge.to)}`;
    ep.polyline.setStyle(trafficEdgeStyle(key, false));
  });
}

function updateTrafficBadge(isLive, timestampMs) {
  const badge = document.getElementById('trafficBadge');
  const ts    = document.getElementById('trafficTimestamp');
  if (!badge) return;

  if (!trafficEnabled) {
    badge.textContent  = 'Traffic: Off';
    badge.className    = 'traffic-badge traffic-off';
    if (ts) ts.textContent = '';
    return;
  }

  badge.textContent  = isLive ? '● Traffic: Live' : '◌ Traffic: Simulated';
  badge.className    = 'traffic-badge ' + (isLive ? 'traffic-live' : 'traffic-sim');

  if (ts && timestampMs) {
    const d = new Date(timestampMs);
    ts.textContent = 'Updated ' + d.toLocaleTimeString();
  }
}

function connectTrafficStream() {
  if (trafficSource) { trafficSource.close(); trafficSource = null; }

  trafficSource = new EventSource(`${API_BASE}/traffic-stream`);

  trafficSource.onmessage = (event) => {
    try {
      const data = JSON.parse(event.data);
      if (data.edges && Array.isArray(data.edges)) {
        applyTrafficToMap(data.edges);
      }
      updateTrafficBadge(data.isLive, data.timestamp);

      if (data.significantChange && lastActiveAlgo) {
        console.log('[Traffic] Significant change — re-running', lastActiveAlgo.algo);
        // Always re-runs all algorithms regardless of lastActiveAlgo.algo value
        runAllAlgorithms();
      }
    } catch (err) {
      console.warn('[Traffic] SSE parse error:', err);
    }
  };

  trafficSource.onerror = () => {
    console.warn('[Traffic] SSE connection lost — will retry in 10s');
    trafficSource.close();
    trafficSource = null;
    updateTrafficBadge(false, null);
    setTimeout(connectTrafficStream, 10_000);
  };
}

async function forceTrafficRefresh() {
  try {
    const res = await fetch(`${API_BASE}/traffic/refresh`, {
      method: 'POST',
      signal: AbortSignal.timeout(10_000)
    });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    toast('Traffic data refreshed.');
  } catch {
    toast('Traffic refresh failed — check server connection.');
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
   RESET GRAPH
   ═══════════════════════════════════════════════════════════════════════════ */

async function resetGraph() {
  try {
    await fetch(`${API_BASE}/reset`, { method: 'POST', signal: AbortSignal.timeout(2000) });
  } catch { /* ignore */ }

  blockedEdgeIndices.clear();
  clearPathAnimation();
  if (evacPolyline) { evacPolyline.remove(); evacPolyline = null; }

  drawEdges();
  applyRoadPolylines();
  updateBlockedBar();

  document.getElementById('resultsWrap').classList.add('hidden');
  document.getElementById('resultsBody').innerHTML = '';
  document.getElementById('evacResult').classList.add('hidden');
  document.getElementById('decisionBox').classList.add('hidden');
  hideAlgoProgress();

  toast('Graph reset. All edges unblocked.');
}

/* ═══════════════════════════════════════════════════════════════════════════
   BOOTSTRAP
   ═══════════════════════════════════════════════════════════════════════════ */

document.addEventListener('DOMContentLoaded', () => {
  populateDropdowns();
  initMap();
  checkServer();
  connectTrafficStream();

  document.getElementById('runAllBtn')    .addEventListener('click', runAllAlgorithms);
  document.getElementById('autoDecideBtn').addEventListener('click', autoDecide);
  document.getElementById('resetBtn')     .addEventListener('click', resetGraph);
  document.getElementById('evacuateBtn')  .addEventListener('click', evacuate);

  const refreshBtn = document.getElementById('trafficRefreshBtn');
  if (refreshBtn) refreshBtn.addEventListener('click', forceTrafficRefresh);

  window.addEventListener('resize', () => { if (map) map.invalidateSize(); });
});