// graph.js — Single source of truth, mirrored exactly from server.cpp
// 22 nodes (0–21), 36 edges — ANY change here MUST be made in server.cpp too

window.NODES = [
  { id: 0,  name: 'Bandra Station',        lat: 19.0544,    lng: 72.8406,    type: 'transit'  },
  { id: 1,  name: 'Khar Station',          lat: 19.0711,    lng: 72.8362,    type: 'transit'  },
  { id: 2,  name: 'Linking Road',          lat: 19.0621,    lng: 72.8347,    type: 'junction' },
  { id: 3,  name: 'S.V. Road',             lat: 19.0622,    lng: 72.8396,    type: 'junction' },
  { id: 4,  name: 'Turner Road',           lat: 19.0548,    lng: 72.8282,    type: 'junction' },
  { id: 5,  name: 'Waterfield Road',       lat: 19.0549,    lng: 72.8316,    type: 'junction' },
  { id: 6,  name: '14th Road',             lat: 19.0509,    lng: 72.8247,    type: 'junction' },
  { id: 7,  name: 'Hill Road',             lat: 19.0511,    lng: 72.8294,    type: 'junction' },
  { id: 8,  name: 'Bandstand',             lat: 19.0447,    lng: 72.8230,    type: 'coastal'  },
  { id: 9,  name: 'Mount Mary',            lat: 19.0478,    lng: 72.8267,    type: 'safe'     },
  { id: 10, name: 'Bandra Talao',          lat: 19.056424,  lng: 72.838245,  type: 'landmark' },
  { id: 11, name: 'Carter Road Promenade', lat: 19.065185,  lng: 72.823235,  type: 'coastal'  },
  { id: 12, name: "St. Andrew's Church",   lat: 19.05500,   lng: 72.82444,   type: 'landmark' },
  { id: 13, name: 'Bandra Reclamation',    lat: 19.05106,   lng: 72.83305,   type: 'junction' },
  { id: 14, name: 'Bandra Fort',           lat: 19.041770,  lng: 72.818580,  type: 'safe'     },
  { id: 15, name: 'Pali Hill',             lat: 19.068,     lng: 72.826,     type: 'safe'     },
  { id: 16, name: 'Khar Gymkhana',         lat: 19.0711,    lng: 72.8362,    type: 'junction' },
  { id: 17, name: 'Juhu Circle',           lat: 19.11552,   lng: 72.83,      type: 'junction' },
  { id: 18, name: 'ISKCON Temple Juhu',    lat: 19.11295,   lng: 72.826553,  type: 'landmark' },
  { id: 19, name: 'Prithvi Theatre',       lat: 19.10622,   lng: 72.82572,   type: 'landmark' },
  { id: 20, name: 'Santacruz West Station',lat: 19.0844,    lng: 72.83729,   type: 'transit'  },
  { id: 21, name: 'Juhu Beach',            lat: 19.10295,   lng: 72.82306,   type: 'safe'     },
];

window.EDGES = [
  // Core Bandra grid (0–8)
  { from: 0,  to: 2,  weight: 1600 },
  { from: 0,  to: 3,  weight: 900  },
  { from: 1,  to: 2,  weight: 800  },
  { from: 1,  to: 3,  weight: 500  },
  { from: 2,  to: 4,  weight: 700  },
  { from: 2,  to: 5,  weight: 600  },
  { from: 3,  to: 4,  weight: 1100 },
  { from: 3,  to: 5,  weight: 800  },
  { from: 4,  to: 6,  weight: 550  },
  { from: 4,  to: 7,  weight: 750  },
  { from: 5,  to: 6,  weight: 600  },
  { from: 5,  to: 7,  weight: 700  },
  { from: 6,  to: 8,  weight: 650  },
  { from: 7,  to: 9,  weight: 500  },
  { from: 8,  to: 9,  weight: 900  },
  // Nodes 10–15
  { from: 0,  to: 10, weight: 350  },
  { from: 3,  to: 10, weight: 200  },
  { from: 7,  to: 11, weight: 400  },
  { from: 4,  to: 12, weight: 300  },
  { from: 11, to: 12, weight: 250  },
  { from: 5,  to: 13, weight: 450  },
  { from: 8,  to: 14, weight: 300  },
  { from: 9,  to: 14, weight: 500  },
  { from: 1,  to: 15, weight: 600  },
  { from: 10, to: 15, weight: 700  },
  { from: 12, to: 15, weight: 350  },
  { from: 14, to: 15, weight: 800  },
  // Nodes 16–21
  { from: 1,  to: 16, weight: 250  },
  { from: 2,  to: 16, weight: 400  },
  { from: 16, to: 17, weight: 1800 },
  { from: 11, to: 17, weight: 900  },
  { from: 17, to: 18, weight: 500  },
  { from: 18, to: 19, weight: 600  },
  { from: 19, to: 21, weight: 300  },
  { from: 17, to: 20, weight: 700  },
  { from: 20, to: 21, weight: 1200 },
  { from: 15, to: 17, weight: 1400 },
  { from: 18, to: 21, weight: 200  },
];

// Filled at runtime by OSRM road geometry fetcher in script.js
window.ROAD_POLYLINES = {};