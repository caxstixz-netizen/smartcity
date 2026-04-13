// graph.js — Single source of truth, mirrored exactly from server.cpp
// 20 nodes (0–19), edges — ANY change here MUST be made in server.cpp too

window.NODES = [
  { id: 0,  name: 'Bandra Station',           lat: 19.0544,   lng: 72.8406,   type: 'transit'  },
  { id: 1,  name: 'Khar Station',             lat: 19.0711,   lng: 72.8362,   type: 'transit'  },
  { id: 2,  name: 'Linking Road',             lat: 19.0621,   lng: 72.8347,   type: 'junction' },
  { id: 3,  name: 'S.V. Road',               lat: 19.0622,   lng: 72.8396,   type: 'junction' },
  { id: 4,  name: 'Turner Road',              lat: 19.0548,   lng: 72.8282,   type: 'junction' },
  { id: 5,  name: 'Waterfield Road',          lat: 19.0549,   lng: 72.8316,   type: 'junction' },
  { id: 6,  name: '14th Road',               lat: 19.0509,   lng: 72.8247,   type: 'junction' },
  { id: 7,  name: 'Hill Road',               lat: 19.0511,   lng: 72.8294,   type: 'junction' },
  { id: 8,  name: 'Bandstand',               lat: 19.0447,   lng: 72.8230,   type: 'coastal'  },
  { id: 9,  name: 'Mount Mary',              lat: 19.0478,   lng: 72.8267,   type: 'safe'     },
  { id: 10, name: 'Bandra Talao',            lat: 19.056424, lng: 72.838245, type: 'landmark' },
  { id: 11, name: 'Carter Road Promenade',   lat: 19.065185, lng: 72.823235, type: 'coastal'  },
  { id: 12, name: "St. Andrew's Church",     lat: 19.05500,  lng: 72.82444,  type: 'landmark' },
  { id: 13, name: 'Bandra Reclamation',      lat: 19.05106,  lng: 72.83305,  type: 'junction' },
  { id: 14, name: 'Bandra Fort',             lat: 19.04177,  lng: 72.81858,  type: 'safe'     },
  { id: 15, name: 'Pali Hill',               lat: 19.068,    lng: 72.826,    type: 'safe'     },
  { id: 16, name: 'Khar Gymkhana',           lat: 19.0680,   lng: 72.8365,   type: 'junction' },
  // ── NEW: 3 replacement nodes between Bandra West and Khar (~3–4 km corridor) ──
  { id: 17, name: 'Khar Road',               lat: 19.0660,   lng: 72.8372,   type: 'junction' },
  { id: 18, name: '16th Road–Linking Jn.',   lat: 19.0645,   lng: 72.8355,   type: 'junction' },
  { id: 19, name: 'Perry Cross Road',        lat: 19.0635,   lng: 72.8330,   type: 'junction' },
  // Total: 20 nodes (IDs 0–19)
];

window.EDGES = [
  // ── Core Bandra grid (nodes 0–8) ─────────────────────────────────────────
  { from: 0,  to: 2,  weight: 1600 }, // Bandra Stn → Linking Rd (~1.6 km)
  { from: 0,  to: 3,  weight: 900  }, // Bandra Stn → S.V. Road (~900 m)
  { from: 1,  to: 2,  weight: 800  }, // Khar Stn   → Linking Rd (~800 m)
  { from: 1,  to: 3,  weight: 500  }, // Khar Stn   → S.V. Road (~500 m)
  { from: 2,  to: 4,  weight: 700  }, // Linking Rd → Turner Rd (~700 m)
  { from: 2,  to: 5,  weight: 600  }, // Linking Rd → Waterfield Rd (~600 m)
  { from: 3,  to: 4,  weight: 1100 }, // S.V. Road  → Turner Rd (~1.1 km)
  { from: 3,  to: 5,  weight: 800  }, // S.V. Road  → Waterfield Rd (~800 m)
  { from: 4,  to: 6,  weight: 550  }, // Turner Rd  → 14th Road (~550 m)
  { from: 4,  to: 7,  weight: 750  }, // Turner Rd  → Hill Road (~750 m)
  { from: 5,  to: 6,  weight: 600  }, // Waterfield → 14th Road (~600 m)
  { from: 5,  to: 7,  weight: 700  }, // Waterfield → Hill Road (~700 m)
  { from: 6,  to: 8,  weight: 650  }, // 14th Road  → Bandstand (~650 m)
  { from: 7,  to: 9,  weight: 500  }, // Hill Road  → Mount Mary (~500 m)
  { from: 8,  to: 9,  weight: 900  }, // Bandstand  → Mount Mary (~900 m)
  // ── Nodes 10–15 ──────────────────────────────────────────────────────────
  { from: 0,  to: 10, weight: 350  }, // Bandra Stn → Bandra Talao (~350 m)
  { from: 3,  to: 10, weight: 200  }, // S.V. Road  → Bandra Talao (~200 m)
  { from: 7,  to: 11, weight: 400  }, // Hill Road  → Carter Rd Prom (~400 m)
  { from: 4,  to: 12, weight: 300  }, // Turner Rd  → St. Andrew's (~300 m)
  { from: 11, to: 12, weight: 250  }, // Carter Rd  → St. Andrew's (~250 m)
  { from: 5,  to: 13, weight: 450  }, // Waterfield → Bandra Reclamation (~450 m)
  { from: 8,  to: 14, weight: 300  }, // Bandstand  → Bandra Fort (~300 m)
  { from: 9,  to: 14, weight: 500  }, // Mount Mary → Bandra Fort (~500 m)
  { from: 1,  to: 15, weight: 600  }, // Khar Stn   → Pali Hill (~600 m)
  { from: 10, to: 15, weight: 700  }, // Bandra Talao→ Pali Hill (~700 m)
  { from: 12, to: 15, weight: 350  }, // St. Andrew's→ Pali Hill (~350 m)
  { from: 14, to: 15, weight: 800  }, // Bandra Fort → Pali Hill (~800 m)
  // ── Node 16 ──────────────────────────────────────────────────────────────
  { from: 1,  to: 16, weight: 250  }, // Khar Stn   → Khar Gymkhana (~250 m)
  { from: 2,  to: 16, weight: 550  }, // Linking Rd → Khar Gymkhana (~550 m)
  // ── NEW nodes 17–19: Bandra–Khar corridor ────────────────────────────────
  { from: 16, to: 17, weight: 400  }, // Khar Gymkhana → Khar Road (~400 m)
  { from: 3,  to: 17, weight: 600  }, // S.V. Road     → Khar Road (~600 m)
  { from: 17, to: 18, weight: 350  }, // Khar Road     → 16th Rd–Linking Jn (~350 m)
  { from: 2,  to: 18, weight: 300  }, // Linking Road  → 16th Rd–Linking Jn (~300 m)
  { from: 18, to: 19, weight: 400  }, // 16th Rd–Lnkg  → Perry Cross Road (~400 m)
  { from: 15, to: 19, weight: 450  }, // Pali Hill     → Perry Cross Road (~450 m)
  { from: 11, to: 19, weight: 500  }, // Carter Rd Prom→ Perry Cross Road (~500 m)
];

// Filled at runtime by OSRM road geometry fetcher in script.js
window.ROAD_POLYLINES = {};