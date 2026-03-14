/* network-globe.js - 3D network globe with real-time circuit visualization */

var NetworkGlobe = {
  _scene: null,
  _camera: null,
  _renderer: null,
  _globe: null,
  _wireframe: null,
  _atmosphere: null,
  _relayGroup: null,
  _arcGroup: null,
  _countryGroup: null,
  _animationId: null,
  _isVisible: false,
  _mouseDown: false,
  _mouseX: 0,
  _mouseY: 0,
  _rotX: 0.3,
  _rotY: -0.5,
  _targetRotX: 0.3,
  _targetRotY: -0.5,
  _autoRotate: true,
  _zoom: 3.0,
  _targetZoom: 3.0,
  _circuits: [],
  _relayCache: {},
  _animTime: 0,
  _pulsePhase: 0,
  _hoveredRelay: null,
  _tooltipEl: null,
  _circuitListEl: null,

  /* Country centroid coordinates (lat, lng) for mapping relays */
  _centroids: {
    US: [39.8, -98.5], DE: [51.2, 10.4], FR: [46.6, 2.2], GB: [53.5, -2.4],
    NL: [52.1, 5.3], CA: [56.1, -106.3], SE: [60.1, 18.6], CH: [46.8, 8.2],
    AT: [47.5, 14.6], RO: [45.9, 24.9], FI: [61.9, 25.7], NO: [60.5, 8.5],
    RU: [61.5, 105.3], JP: [36.2, 138.3], AU: [-25.3, 133.8], BR: [-14.2, -51.9],
    IN: [20.6, 79.0], CN: [35.9, 104.2], SG: [1.4, 103.8], KR: [35.9, 127.8],
    IT: [41.9, 12.6], ES: [40.5, -3.7], PL: [51.9, 19.1], CZ: [49.8, 15.5],
    UA: [48.4, 31.2], HU: [47.2, 19.5], BG: [42.7, 25.5], IS: [64.9, -19.0],
    LU: [49.8, 6.1], IE: [53.4, -8.2], DK: [56.3, 9.5], LT: [55.2, 23.9],
    LV: [56.9, 24.1], EE: [58.6, 25.0], GR: [39.1, 21.8], PT: [39.4, -8.2],
    BE: [50.5, 4.5], HR: [45.1, 15.2], RS: [44.0, 21.0], SK: [48.7, 19.7],
    SI: [46.2, 14.8], MD: [47.4, 28.4], ZA: [-30.6, 22.9], IL: [31.0, 34.9],
    TW: [23.7, 121.0], HK: [22.4, 114.1], NZ: [-40.9, 174.9], MX: [23.6, -102.6],
    AR: [-38.4, -63.6], CL: [-35.7, -71.5], CO: [4.6, -74.3], TH: [15.9, 100.5],
    PH: [12.9, 121.8], MY: [4.2, 101.9], ID: [-0.8, 113.9], VN: [14.1, 108.3],
    TR: [39.0, 35.2], EG: [26.8, 30.8], NG: [9.1, 8.7], KE: [-0.0, 37.9],
    PE: [-9.2, -75.0], PK: [30.4, 69.3], BD: [23.7, 90.4], GE: [42.3, 43.4],
    AM: [40.1, 44.5], KZ: [48.0, 68.0], CY: [35.1, 33.4], MT: [35.9, 14.4],
    BA: [43.9, 17.7], AL: [41.2, 20.2], MK: [41.5, 21.7], ME: [42.7, 19.4]
  },

  /* Role colors */
  _colors: {
    guard: 0x3fb950,
    middle: 0x58a6ff,
    exit: 0xf85149,
    bridge: 0xd29922,
    arc: 0xa855f7,
    arcActive: 0xc084fc,
    globe: 0x151b26,
    wire: 0x30363d,
    land: 0x1a2332,
    coastline: 0x2d4a5e
  },

  init: function() {
    var page = document.getElementById('page-network');
    page.innerHTML =
      '<h2>Network Globe</h2>' +
      '<div style="display:flex;gap:16px;margin-top:16px;flex-wrap:wrap">' +
        '<div class="card" style="flex:1;min-width:500px;padding:0;overflow:hidden;position:relative">' +
          '<canvas id="globe-canvas" style="width:100%;height:600px;display:block;cursor:grab"></canvas>' +
          '<div id="globe-tooltip" style="position:absolute;display:none;' +
            'background:rgba(13,17,23,0.92);border:1px solid var(--border-color);' +
            'padding:8px 12px;border-radius:6px;font-size:12px;pointer-events:none;' +
            'box-shadow:0 4px 12px rgba(0,0,0,0.5);max-width:260px;z-index:10"></div>' +
          '<div id="globe-info" style="position:absolute;bottom:12px;left:12px;' +
            'background:rgba(13,17,23,0.85);padding:8px 14px;border-radius:6px;' +
            'font-size:13px;display:none;border:1px solid var(--border-color)"></div>' +
          '<div id="globe-controls" style="position:absolute;top:12px;right:12px;' +
            'display:flex;gap:6px">' +
            '<button class="btn btn-secondary" style="padding:4px 10px;font-size:12px" ' +
              'onclick="NetworkGlobe.zoomIn()" title="Zoom In">+</button>' +
            '<button class="btn btn-secondary" style="padding:4px 10px;font-size:12px" ' +
              'onclick="NetworkGlobe.zoomOut()" title="Zoom Out">&minus;</button>' +
            '<button class="btn btn-secondary" style="padding:4px 10px;font-size:12px" ' +
              'onclick="NetworkGlobe.resetView()" title="Reset View">&#8634;</button>' +
          '</div>' +
          '<div id="globe-placeholder" style="display:flex;align-items:center;' +
            'justify-content:center;height:600px;color:var(--text-secondary);' +
            'position:absolute;inset:0;pointer-events:none">' +
            'Loading globe...' +
          '</div>' +
        '</div>' +
        '<div class="card" style="width:280px;max-height:616px;overflow-y:auto;' +
          'flex-shrink:0">' +
          '<div class="card-label">Active Circuits</div>' +
          '<div id="globe-circuit-list" style="margin-top:8px;font-size:12px">' +
            '<span class="text-muted">Loading...</span>' +
          '</div>' +
        '</div>' +
      '</div>' +
      '<div class="card" style="margin-top:16px">' +
        '<div class="card-label">Legend</div>' +
        '<div style="display:flex;gap:20px;margin-top:8px;font-size:13px;flex-wrap:wrap">' +
          '<span><span style="display:inline-block;width:12px;height:12px;' +
            'border-radius:50%;background:#3fb950;margin-right:6px;' +
            'box-shadow:0 0 6px #3fb950"></span>Guard</span>' +
          '<span><span style="display:inline-block;width:12px;height:12px;' +
            'border-radius:50%;background:#58a6ff;margin-right:6px;' +
            'box-shadow:0 0 6px #58a6ff"></span>Middle</span>' +
          '<span><span style="display:inline-block;width:12px;height:12px;' +
            'border-radius:50%;background:#f85149;margin-right:6px;' +
            'box-shadow:0 0 6px #f85149"></span>Exit</span>' +
          '<span><span style="display:inline-block;width:12px;height:12px;' +
            'border-radius:50%;background:#d29922;margin-right:6px;' +
            'box-shadow:0 0 6px #d29922"></span>Bridge</span>' +
          '<span style="display:flex;align-items:center">' +
            '<span style="display:inline-block;width:24px;height:2px;' +
            'background:linear-gradient(90deg,#a855f7,#c084fc);margin-right:6px"></span>' +
            'Circuit Path</span>' +
        '</div>' +
      '</div>';

    this._tooltipEl = document.getElementById('globe-tooltip');
    this._circuitListEl = document.getElementById('globe-circuit-list');

    /* Subscribe to real-time circuit events */
    var self = this;
    TorWS.on('circuit', function(data) {
      if (self._isVisible) {
        self._loadCircuits();
      }
    });
  },

  onShow: function() {
    if (typeof THREE === 'undefined' || THREE._isStub) {
      var ph = document.getElementById('globe-placeholder');
      if (ph) ph.textContent = 'WebGL not available';
      return;
    }
    if (!this._scene) this._initScene();
    this._isVisible = true;
    this._animate();
    this._loadCircuits();
  },

  _initScene: function() {
    var canvas = document.getElementById('globe-canvas');
    if (!canvas) return;

    var rect = canvas.getBoundingClientRect();
    this._scene = new THREE.Scene();
    this._camera = new THREE.PerspectiveCamera(
      45, rect.width / rect.height, 0.1, 100);
    this._camera.position.set(0, 0, this._zoom);

    this._renderer = new THREE.WebGLRenderer({
      canvas: canvas, antialias: true, alpha: false
    });
    this._renderer.setSize(rect.width, rect.height);
    this._renderer.setClearColor(0x0d1117);
    this._renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));

    /* Main globe sphere */
    var globeGeo = new THREE.SphereGeometry(1, 64, 64);
    var globeMat = new THREE.MeshPhongMaterial({
      color: this._colors.globe, transparent: false, opacity: 1.0
    });
    this._globe = new THREE.Mesh(globeGeo, globeMat);
    this._scene.add(this._globe);

    /* Wireframe grid overlay */
    var wireGeo = new THREE.SphereGeometry(1.003, 36, 18);
    var wireMat = new THREE.MeshBasicMaterial({
      color: this._colors.wire, wireframe: true,
      transparent: true, opacity: 0.15
    });
    this._wireframe = new THREE.Mesh(wireGeo, wireMat);
    this._scene.add(this._wireframe);

    /* Atmosphere glow ring */
    var atmoGeo = new THREE.SphereGeometry(1.04, 48, 48);
    var atmoMat = new THREE.MeshBasicMaterial({
      color: 0x4488cc, transparent: true, opacity: 0.06
    });
    this._atmosphere = new THREE.Mesh(atmoGeo, atmoMat);
    this._scene.add(this._atmosphere);

    /* Country outlines group */
    this._countryGroup = new THREE.Group();
    this._scene.add(this._countryGroup);
    this._drawCountryOutlines();

    /* Groups for relays and arcs */
    this._relayGroup = new THREE.Group();
    this._arcGroup = new THREE.Group();
    this._scene.add(this._relayGroup);
    this._scene.add(this._arcGroup);

    /* Lights */
    this._scene.add(new THREE.AmbientLight(0x556677));
    var dirLight = new THREE.DirectionalLight(0xffffff, 0.7);
    dirLight.position.set(5, 3, 5);
    this._scene.add(dirLight);

    /* Mouse interaction */
    var self = this;
    canvas.addEventListener('mousedown', function(e) {
      self._mouseDown = true;
      self._mouseX = e.clientX;
      self._mouseY = e.clientY;
      self._autoRotate = false;
      canvas.style.cursor = 'grabbing';
    });
    canvas.addEventListener('mousemove', function(e) {
      if (self._mouseDown) {
        var dx = e.clientX - self._mouseX;
        var dy = e.clientY - self._mouseY;
        self._targetRotY += dx * 0.005;
        self._targetRotX += dy * 0.005;
        self._targetRotX = Math.max(-1.2, Math.min(1.2, self._targetRotX));
        self._mouseX = e.clientX;
        self._mouseY = e.clientY;
      }
    });
    canvas.addEventListener('mouseup', function() {
      self._mouseDown = false;
      canvas.style.cursor = 'grab';
    });
    canvas.addEventListener('mouseleave', function() {
      self._mouseDown = false;
      canvas.style.cursor = 'grab';
      if (self._tooltipEl) self._tooltipEl.style.display = 'none';
    });

    /* Scroll to zoom */
    canvas.addEventListener('wheel', function(e) {
      e.preventDefault();
      self._targetZoom += e.deltaY * 0.002;
      self._targetZoom = Math.max(1.5, Math.min(6, self._targetZoom));
    }, {passive: false});

    /* Double-click reset */
    canvas.addEventListener('dblclick', function() {
      self._autoRotate = true;
      self._targetRotX = 0.3;
      self._targetRotY = self._rotY;
      self._targetZoom = 3.0;
    });

    /* Touch support */
    var lastTouch = null;
    canvas.addEventListener('touchstart', function(e) {
      if (e.touches.length === 1) {
        lastTouch = {x: e.touches[0].clientX, y: e.touches[0].clientY};
        self._autoRotate = false;
      }
    }, {passive: true});
    canvas.addEventListener('touchmove', function(e) {
      if (e.touches.length === 1 && lastTouch) {
        var dx = e.touches[0].clientX - lastTouch.x;
        var dy = e.touches[0].clientY - lastTouch.y;
        self._targetRotY += dx * 0.005;
        self._targetRotX += dy * 0.005;
        self._targetRotX = Math.max(-1.2, Math.min(1.2, self._targetRotX));
        lastTouch = {x: e.touches[0].clientX, y: e.touches[0].clientY};
      }
    }, {passive: true});
    canvas.addEventListener('touchend', function() { lastTouch = null; },
                            {passive: true});

    /* Resize handler */
    window.addEventListener('resize', function() {
      if (!self._renderer || !self._camera) return;
      var c = document.getElementById('globe-canvas');
      if (!c) return;
      var p = c.parentElement;
      self._camera.aspect = p.clientWidth / p.clientHeight;
      self._camera.updateProjectionMatrix();
      self._renderer.setSize(p.clientWidth, p.clientHeight);
    });

    var ph = document.getElementById('globe-placeholder');
    if (ph) ph.style.display = 'none';
  },

  /* Draw simplified coastline/country boundary outlines on globe */
  _drawCountryOutlines: function() {
    /* Simplified world coastline segments as [lat,lng] pairs.
     * Each sub-array is a polyline (connected line strip). */
    var coastlines = [
      /* North America outline */
      [[72,-170],[71,-156],[68,-164],[66,-164],[64,-170],[61,-165],
       [60,-161],[59,-152],[60,-147],[59,-140],[56,-134],[55,-131],
       [52,-128],[49,-125],[46,-124],[43,-124],[39,-123],[35,-121],
       [33,-118],[30,-114],[27,-110],[25,-107],[22,-106],[20,-105],
       [18,-103],[15,-92],[13,-87],[10,-83],[9,-79],[8,-77],[9,-76]],
      /* Central America/Caribbean */
      [[18,-88],[19,-90],[21,-87],[22,-84],[23,-82],[21,-80],[19,-81],
       [16,-83],[15,-84],[14,-87],[15,-89],[18,-88]],
      /* South America outline */
      [[12,-72],[11,-75],[8,-77],[5,-77],[2,-79],[-1,-80],[-3,-80],
       [-5,-81],[-7,-79],[-10,-77],[-14,-76],[-18,-71],[-22,-70],
       [-27,-71],[-33,-71],[-38,-62],[-42,-64],[-46,-68],[-50,-69],
       [-52,-70],[-55,-67],[-55,-64],[-50,-58],[-45,-58],[-42,-52],
       [-38,-56],[-34,-54],[-30,-50],[-28,-49],[-23,-44],[-18,-39],
       [-13,-38],[-10,-37],[-5,-35],[-2,-50],[2,-52],[5,-60],[7,-60],
       [8,-62],[10,-67],[11,-72],[12,-72]],
      /* Europe outline */
      [[71,28],[70,20],[65,14],[63,10],[60,5],[58,7],[56,8],
       [55,10],[54,9],[53,6],[51,4],[49,1],[47,-2],[44,-1],
       [43,3],[42,-8],[37,-9],[36,-6],[37,-1],[39,0],[41,1],
       [43,3],[44,8],[41,9],[39,9],[38,13],[39,16],[40,18],
       [41,20],[42,24],[41,29],[40,28],[38,24],[37,23],[36,28],
       [37,36],[39,37],[41,33],[42,32],[43,40],[42,44],[44,42],
       [45,40],[45,37],[47,35],[46,30],[47,16],[48,17],[50,20],
       [52,21],[54,18],[55,21],[57,24],[56,15],[57,12],[58,12],
       [60,20],[62,17],[63,15],[65,14],[68,16],[70,20],[71,28]],
      /* Scandinavia */
      [[60,5],[61,5],[63,8],[65,14],[68,16],[70,26],[71,28],
       [69,30],[65,29],[63,30],[62,26],[60,24],[59,18],[57,16],
       [56,15],[58,12],[60,5]],
      /* Africa outline */
      [[37,-1],[36,-6],[35,-1],[34,10],[33,12],[31,10],[30,10],
       [25,17],[20,17],[17,16],[15,17],[13,15],[10,14],[7,10],
       [5,10],[5,2],[4,-3],[5,-7],[5,-5],[4,-8],[4,-10],[-1,-9],
       [-5,12],[-10,14],[-12,14],[-15,12],[-17,12],[-20,15],
       [-22,17],[-26,15],[-29,17],[-32,18],[-35,20],[-35,25],
       [-33,28],[-30,31],[-25,35],[-20,35],[-15,41],[-12,44],
       [-10,50],[-2,51],[5,49],[10,51],[12,45],[14,42],[11,43],
       [10,42],[12,38],[13,35],[15,37],[18,40],[20,40],[23,37],
       [25,35],[30,33],[31,32],[33,12],[35,10],[35,0],[37,-1]],
      /* Asia outline */
      [[42,44],[44,50],[40,54],[37,56],[30,48],[26,50],[25,57],
       [23,58],[20,57],[13,45],[12,44]],
      [[42,44],[44,50],[46,52],[47,53],[45,60],[40,62],[38,65],
       [35,68],[30,67],[28,65],[24,68],[22,72],[18,76],[15,80],
       [11,80],[8,77],[4,74],[1,104],[2,106],[5,105],[8,106],
       [10,108],[12,109],[16,108],[19,106],[20,107],[22,114],
       [25,119],[28,120],[30,122],[35,127],[38,129],[39,126],
       [42,133],[44,132],[46,138],[49,141],[51,143],[53,142],
       [55,137],[57,140],[60,150],[63,171],[66,177],[67,-180],
       [69,-169],[70,-162],[68,-164],[66,-164],[64,-170],[64,-172]],
      /* Russia north */
      [[71,28],[72,40],[73,55],[72,60],[74,65],[75,58],[77,69],
       [77,105],[76,113],[74,112],[73,120],[72,130],[71,140],
       [69,161],[66,177]],
      /* Australia outline */
      [[-12,130],[-14,127],[-15,124],[-18,122],[-20,119],[-22,114],
       [-25,113],[-28,114],[-31,115],[-34,116],[-35,117],[-35,138],
       [-37,140],[-38,145],[-38,148],[-34,151],[-28,153],[-24,153],
       [-20,149],[-18,146],[-16,146],[-15,141],[-14,136],[-12,132],
       [-12,130]],
      /* India/SE Asia */
      [[28,65],[27,63],[24,68],[22,72],[18,76],[15,80],[11,80],
       [8,77],[8,78],[10,79],[13,80],[15,80],[20,86],[22,89],
       [22,92],[18,95],[16,98],[14,99],[10,98],[6,100],[2,104],
       [1,104]],
      /* Japan */
      [[31,131],[33,130],[34,132],[35,134],[36,137],[37,137],
       [39,140],[41,140],[42,143],[43,145],[45,142]],
      /* UK/Ireland */
      [[50,-6],[51,-5],[52,-4],[53,-3],[54,-3],[56,-5],[57,-6],
       [58,-5],[58,-3],[56,-2],[53,0],[52,1],[51,1],[50,-2],[50,-6]],
      [[52,-10],[53,-10],[54,-8],[53,-6],[52,-7],[52,-10]]
    ];

    var R = 1.005; /* slightly above globe surface */
    for (var i = 0; i < coastlines.length; i++) {
      var seg = coastlines[i];
      if (seg.length < 2) continue;
      var pts = [];
      for (var j = 0; j < seg.length; j++) {
        pts.push(this._latLngToVec3(seg[j][0], seg[j][1], R));
      }
      var geo = new THREE.BufferGeometry().setFromPoints(pts);
      var mat = new THREE.LineBasicMaterial({
        color: this._colors.coastline,
        transparent: true, opacity: 0.4
      });
      var line = new THREE.Line(geo, mat);
      this._countryGroup.add(line);
    }
  },

  _latLngToVec3: function(lat, lng, radius) {
    radius = radius || 1.02;
    var phi = (90 - lat) * Math.PI / 180;
    var theta = (lng + 180) * Math.PI / 180;
    return new THREE.Vector3(
      -radius * Math.sin(phi) * Math.cos(theta),
      radius * Math.cos(phi),
      radius * Math.sin(phi) * Math.sin(theta)
    );
  },

  _loadCircuits: async function() {
    try {
      var data = await TorAPI.getCircuits();
      var circuits = Array.isArray(data) ? data :
                     (data && data.circuits ? data.circuits : []);

      /* Filter to BUILT circuits */
      var builtCircuits = [];
      for (var ci = 0; ci < circuits.length; ci++) {
        var c = circuits[ci];
        if (c.status === 'BUILT' && c.path && c.path.length > 0) {
          builtCircuits.push(c);
        }
      }

      this._circuits = builtCircuits;
      this._clearGroup(this._relayGroup);
      this._clearGroup(this._arcGroup);

      var seen = {};
      var relayInfoMap = {};

      for (var i = 0; i < builtCircuits.length; i++) {
        var path = builtCircuits[i].path || [];
        var prevPos = null;

        for (var j = 0; j < path.length; j++) {
          var relay = path[j];
          var relayInfo = this._parseRelay(relay);
          var relayId = relayInfo.fingerprint || relayInfo.raw;
          var pos = await this._resolvePosition(relayInfo);

          if (relayId) {
            relayInfoMap[relayId] = relayInfo;
            relayInfo._pos = pos;
          }

          /* Color by role */
          var color = this._colors.middle;
          var role = 'Middle';
          if (j === 0) { color = this._colors.guard; role = 'Guard'; }
          else if (j === path.length - 1) { color = this._colors.exit; role = 'Exit'; }
          relayInfo._role = role;
          relayInfo._color = color;

          /* Add relay dot (once per unique relay) */
          if (!seen[relayId]) {
            seen[relayId] = true;
            this._addRelay(pos.lat, pos.lng, color, relayInfo);
          }

          /* Draw arc from previous hop */
          var currentVec = this._latLngToVec3(pos.lat, pos.lng, 1.02);
          if (prevPos) {
            this._addArc(prevPos, currentVec, this._colors.arc);
          }
          prevPos = currentVec;
        }
      }

      /* Update info overlay */
      var infoEl = document.getElementById('globe-info');
      if (infoEl) {
        var relayCount = Object.keys(seen).length;
        var countrySet = {};
        for (var id in relayInfoMap) {
          if (relayInfoMap[id].country)
            countrySet[relayInfoMap[id].country] = true;
        }
        var countryCount = Object.keys(countrySet).length;
        infoEl.innerHTML =
          '<span style="color:#e6edf3">' + relayCount + '</span> relays across ' +
          '<span style="color:#e6edf3">' + countryCount + '</span> countries &middot; ' +
          '<span style="color:#e6edf3">' + builtCircuits.length + '</span> circuits';
        infoEl.style.display = relayCount > 0 ? '' : 'none';
      }

      /* Update circuit list panel */
      this._updateCircuitList(builtCircuits, relayInfoMap);

    } catch (e) {
      console.error('Failed to load circuits for globe:', e);
    }
  },

  /* Parse a relay entry - handles both string and object formats */
  _parseRelay: function(relay) {
    if (typeof relay === 'object' && relay !== null) {
      return {
        fingerprint: relay.fingerprint || '',
        nickname: relay.nickname || '',
        ip: relay.ip || relay.address || '',
        country: (relay.country || '').toUpperCase(),
        raw: relay.fingerprint || JSON.stringify(relay)
      };
    }
    /* String format: $FINGERPRINT~nickname */
    var info = {fingerprint: '', nickname: '', ip: '', country: '', raw: relay};
    var str = String(relay);
    if (str.charAt(0) === '$') str = str.substring(1);
    var tilde = str.indexOf('~');
    if (tilde !== -1) {
      info.fingerprint = str.substring(0, tilde);
      info.nickname = str.substring(tilde + 1);
    } else {
      info.fingerprint = str;
    }
    return info;
  },

  /* Resolve relay position via API or cache */
  _resolvePosition: async function(relayInfo) {
    var cacheKey = relayInfo.fingerprint || relayInfo.ip || relayInfo.raw;
    if (this._relayCache[cacheKey]) return this._relayCache[cacheKey];

    var lat = null, lng = null;

    /* Try GeoIP lookup if we have an IP */
    if (relayInfo.ip) {
      try {
        var geo = await TorAPI.getGeoip(relayInfo.ip);
        if (geo) {
          if (geo.latitude !== undefined && geo.longitude !== undefined) {
            lat = geo.latitude;
            lng = geo.longitude;
            relayInfo.country = (geo.country || '').toUpperCase();
          } else if (geo.country && this._centroids[geo.country.toUpperCase()]) {
            var cc = geo.country.toUpperCase();
            relayInfo.country = cc;
            lat = this._centroids[cc][0];
            lng = this._centroids[cc][1];
          }
        }
      } catch (e) { /* fallback below */ }
    }

    /* Fallback: country centroid */
    if (lat === null && relayInfo.country &&
        this._centroids[relayInfo.country]) {
      lat = this._centroids[relayInfo.country][0];
      lng = this._centroids[relayInfo.country][1];
    }

    /* Add jitter so relays in the same country don't overlap */
    if (lat !== null) {
      var hash = 0;
      var key = relayInfo.fingerprint || relayInfo.ip || '';
      for (var k = 0; k < key.length; k++) {
        hash = ((hash << 5) - hash + key.charCodeAt(k)) | 0;
      }
      lat += ((hash & 0xFF) - 128) / 128 * 3;
      lng += (((hash >> 8) & 0xFF) - 128) / 128 * 5;
    }

    /* Last resort: random position */
    if (lat === null) {
      lat = (Math.random() - 0.5) * 100;
      lng = (Math.random() - 0.5) * 360;
    }

    var pos = {lat: lat, lng: lng};
    this._relayCache[cacheKey] = pos;
    return pos;
  },

  _addRelay: function(lat, lng, color, info) {
    if (!this._relayGroup) return;
    var pos = this._latLngToVec3(lat, lng, 1.025);
    var dotGeo = new THREE.SphereGeometry(0.018, 8, 8);
    var dotMat = new THREE.MeshBasicMaterial({ color: color });
    var dot = new THREE.Mesh(dotGeo, dotMat);
    dot.position.copy(pos);
    dot._relayInfo = info;
    this._relayGroup.add(dot);
  },

  _addArc: function(from, to, color) {
    if (!this._arcGroup) return;
    var mid = new THREE.Vector3().addVectors(from, to).multiplyScalar(0.5);
    var dist = from.distanceTo(to);
    var elevation = dist * 0.4 + 1.06;
    mid.normalize().multiplyScalar(elevation);

    var curve = new THREE.CatmullRomCurve3([from, mid, to]);
    var points = curve.getPoints(48);
    var geom = new THREE.BufferGeometry().setFromPoints(points);
    var mat = new THREE.LineBasicMaterial({
      color: color, transparent: true, opacity: 0.55
    });
    var line = new THREE.Line(geom, mat);
    this._arcGroup.add(line);
  },

  _clearGroup: function(group) {
    if (!group) return;
    while (group.children.length > 0) {
      var child = group.children[0];
      group.remove(child);
      if (child.geometry) child.geometry.dispose();
      if (child.material) child.material.dispose();
    }
  },

  _updateCircuitList: function(circuits, relayInfoMap) {
    var el = this._circuitListEl;
    if (!el) return;

    if (circuits.length === 0) {
      el.innerHTML = '<span class="text-muted">No active circuits</span>';
      return;
    }

    var html = '';
    for (var i = 0; i < circuits.length; i++) {
      var c = circuits[i];
      var purpose = c.purpose || 'GENERAL';
      html += '<div style="padding:8px 0;border-bottom:1px solid var(--border-color)">';
      html += '<div style="display:flex;justify-content:space-between;margin-bottom:4px">';
      html += '<span style="color:var(--text-primary);font-weight:500">Circuit #' +
              c.id + '</span>';
      html += '<span class="badge badge-info" style="font-size:10px">' +
              purpose + '</span>';
      html += '</div>';

      var path = c.path || [];
      for (var j = 0; j < path.length; j++) {
        var relay = this._parseRelay(path[j]);
        var role = j === 0 ? 'Guard' : (j === path.length - 1 ? 'Exit' : 'Middle');
        var roleColor = j === 0 ? '#3fb950' : (j === path.length - 1 ? '#f85149' : '#58a6ff');
        var nick = relay.nickname || relay.fingerprint.substring(0, 8) || '?';
        var cc = relay.country || '??';

        html += '<div style="display:flex;align-items:center;gap:6px;' +
                'margin:2px 0;padding-left:4px">';
        html += '<span style="width:6px;height:6px;border-radius:50%;' +
                'background:' + roleColor + ';flex-shrink:0"></span>';
        html += '<span style="color:var(--text-secondary);width:36px;' +
                'font-size:10px">' + role + '</span>';
        html += '<span style="color:var(--text-primary);flex:1;' +
                'overflow:hidden;text-overflow:ellipsis;white-space:nowrap" ' +
                'title="' + relay.fingerprint + '">' + nick + '</span>';
        html += '<span style="color:var(--text-muted);font-size:10px">' +
                cc + '</span>';
        if (relay.fingerprint && typeof RelayExclude !== 'undefined') {
          html += '<span style="cursor:pointer;color:var(--accent-red);font-size:9px;' +
                  'opacity:0.7" onclick="RelayExclude.excludeRelayFromCircuit(\'' +
                  relay.fingerprint + '\',\'' +
                  (relay.nickname || '').replace(/'/g, '') + '\')" ' +
                  'title="Exclude this relay">&times;</span>';
        }
        html += '</div>';

        if (j < path.length - 1) {
          html += '<div style="padding-left:6px;color:var(--text-muted);' +
                  'font-size:10px">&darr;</div>';
        }
      }
      html += '</div>';
    }
    el.innerHTML = html;
  },

  _animate: function() {
    if (!this._isVisible) return;
    var self = this;
    this._animationId = requestAnimationFrame(function() { self._animate(); });
    this._animTime += 0.016;
    this._pulsePhase += 0.03;

    /* Smooth rotation interpolation */
    if (this._autoRotate) {
      this._targetRotY += 0.0008;
    }
    this._rotX += (this._targetRotX - this._rotX) * 0.08;
    this._rotY += (this._targetRotY - this._rotY) * 0.08;
    this._zoom += (this._targetZoom - this._zoom) * 0.08;
    this._camera.position.set(0, 0, this._zoom);

    /* Apply rotation to all globe elements */
    var groups = [this._globe, this._wireframe, this._atmosphere,
                  this._countryGroup, this._relayGroup, this._arcGroup];
    for (var i = 0; i < groups.length; i++) {
      if (groups[i]) {
        groups[i].rotation.x = this._rotX;
        groups[i].rotation.y = this._rotY;
      }
    }

    /* Render */
    if (this._renderer && this._scene && this._camera) {
      this._renderer.render(this._scene, this._camera);
    }
  },

  zoomIn: function() {
    this._targetZoom = Math.max(1.5, this._targetZoom - 0.3);
  },

  zoomOut: function() {
    this._targetZoom = Math.min(6, this._targetZoom + 0.3);
  },

  resetView: function() {
    this._autoRotate = true;
    this._targetRotX = 0.3;
    this._targetZoom = 3.0;
  },

  onHide: function() {
    this._isVisible = false;
    if (this._animationId) {
      cancelAnimationFrame(this._animationId);
      this._animationId = null;
    }
  }
};
