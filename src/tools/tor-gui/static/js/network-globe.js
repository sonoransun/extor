/* network-globe.js - 3D network globe visualization */

var NetworkGlobe = {
  _scene: null,
  _camera: null,
  _renderer: null,
  _globe: null,
  _relayGroup: null,
  _arcGroup: null,
  _animationId: null,
  _isVisible: false,
  _mouseDown: false,
  _mouseX: 0,
  _mouseY: 0,
  _rotX: 0.3,
  _rotY: 0,
  _autoRotate: true,

  /* Country centroid coordinates (lat, lng) for mapping IPs to positions */
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
    SI: [46.2, 14.8], MD: [47.4, 28.4], ZA: [30.6, 22.9], IL: [31.0, 34.9],
    TW: [23.7, 121.0], HK: [22.4, 114.1], NZ: [-40.9, 174.9], MX: [23.6, -102.6],
    AR: [-38.4, -63.6], CL: [-35.7, -71.5], CO: [4.6, -74.3], TH: [15.9, 100.5]
  },

  init: function() {
    var page = document.getElementById('page-network');
    page.innerHTML =
      '<h2>Network Globe</h2>' +
      '<div class="card" style="margin-top:16px;padding:0;overflow:hidden;position:relative">' +
        '<canvas id="globe-canvas" style="width:100%;height:500px;display:block"></canvas>' +
        '<div id="globe-info" style="position:absolute;bottom:16px;left:16px;' +
          'background:rgba(0,0,0,0.7);padding:8px 12px;border-radius:6px;' +
          'font-size:13px;display:none"></div>' +
        '<div id="globe-placeholder" style="display:flex;align-items:center;' +
          'justify-content:center;height:500px;color:var(--text-secondary);' +
          'position:absolute;inset:0;pointer-events:none">' +
          'Loading globe...' +
        '</div>' +
      '</div>' +
      '<div class="card" style="margin-top:16px">' +
        '<div class="card-label">Legend</div>' +
        '<div style="display:flex;gap:20px;margin-top:8px;font-size:13px">' +
          '<span><span style="display:inline-block;width:10px;height:10px;' +
            'border-radius:50%;background:#3fb950;margin-right:4px"></span>Guard</span>' +
          '<span><span style="display:inline-block;width:10px;height:10px;' +
            'border-radius:50%;background:#58a6ff;margin-right:4px"></span>Middle</span>' +
          '<span><span style="display:inline-block;width:10px;height:10px;' +
            'border-radius:50%;background:#f85149;margin-right:4px"></span>Exit</span>' +
          '<span><span style="display:inline-block;width:10px;height:10px;' +
            'border-radius:50%;background:#d29922;margin-right:4px"></span>Bridge</span>' +
        '</div>' +
      '</div>';
  },

  onShow: function() {
    /* Called when the network page becomes visible */
    if (typeof THREE === 'undefined' || (typeof THREE !== 'undefined' && THREE._isStub)) {
      var ph = document.getElementById('globe-placeholder');
      if (ph) {
        ph.textContent =
          'Install Three.js for 3D globe. Place three.min.js in static/lib/';
      }
      this._drawFallback();
      return;
    }
    if (!this._scene) this._initScene();
    this._isVisible = true;
    this._animate();
    this._loadCircuits();
  },

  _drawFallback: function() {
    /* 2D fallback on canvas when Three.js is unavailable */
    var canvas = document.getElementById('globe-canvas');
    if (!canvas) return;
    var ctx = canvas.getContext('2d');
    if (!ctx) return;

    var dpr = window.devicePixelRatio || 1;
    var rect = canvas.getBoundingClientRect();
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    var w = rect.width;
    var h = rect.height;
    var cx = w / 2;
    var cy = h / 2;
    var r = Math.min(w, h) * 0.35;

    ctx.fillStyle = '#0d1117';
    ctx.fillRect(0, 0, w, h);

    /* Globe circle */
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
    ctx.fillStyle = '#161b22';
    ctx.fill();
    ctx.strokeStyle = '#30363d';
    ctx.lineWidth = 1;
    ctx.stroke();

    /* Latitude lines */
    ctx.strokeStyle = 'rgba(48, 54, 61, 0.4)';
    ctx.lineWidth = 0.5;
    for (var lat = -60; lat <= 60; lat += 30) {
      var latRad = lat * Math.PI / 180;
      var ry = r * Math.cos(latRad);
      var dy = r * Math.sin(latRad);
      ctx.beginPath();
      ctx.ellipse(cx, cy - dy, ry, ry * 0.3, 0, 0, Math.PI * 2);
      ctx.stroke();
    }

    /* Longitude lines */
    for (var i = 0; i < 12; i++) {
      var angle = i * Math.PI / 6;
      ctx.beginPath();
      ctx.ellipse(cx, cy, r * Math.abs(Math.cos(angle)), r, angle, 0, Math.PI * 2);
      ctx.stroke();
    }

    /* Placeholder text */
    ctx.fillStyle = '#8b949e';
    ctx.font = '14px -apple-system, sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText('Install Three.js for interactive 3D globe', cx, cy);
    ctx.font = '12px -apple-system, sans-serif';
    ctx.fillText('Place three.min.js in static/lib/', cx, cy + 24);

    var ph = document.getElementById('globe-placeholder');
    if (ph) ph.style.display = 'none';
  },

  _initScene: function() {
    var canvas = document.getElementById('globe-canvas');
    if (!canvas) return;

    this._scene = new THREE.Scene();
    this._camera = new THREE.PerspectiveCamera(
      45, canvas.clientWidth / canvas.clientHeight, 0.1, 1000);
    this._camera.position.set(0, 0, 3);

    this._renderer = new THREE.WebGLRenderer({
      canvas: canvas, antialias: true, alpha: true
    });
    this._renderer.setSize(canvas.clientWidth, canvas.clientHeight);
    this._renderer.setClearColor(0x0d1117);
    this._renderer.setPixelRatio(window.devicePixelRatio || 1);

    /* Globe sphere */
    var globeGeo = new THREE.SphereGeometry(1, 64, 64);
    var globeMat = new THREE.MeshPhongMaterial({
      color: 0x1a1a2e, transparent: true, opacity: 0.9
    });
    this._globe = new THREE.Mesh(globeGeo, globeMat);
    this._scene.add(this._globe);

    /* Wireframe overlay */
    var wireGeo = new THREE.SphereGeometry(1.002, 32, 32);
    var wireMat = new THREE.MeshBasicMaterial({
      color: 0x30363d, wireframe: true, transparent: true, opacity: 0.25
    });
    this._scene.add(new THREE.Mesh(wireGeo, wireMat));

    /* Groups for relays and arcs */
    this._relayGroup = new THREE.Group();
    this._arcGroup = new THREE.Group();
    this._scene.add(this._relayGroup);
    this._scene.add(this._arcGroup);

    /* Lights */
    this._scene.add(new THREE.AmbientLight(0x444444));
    var dirLight = new THREE.DirectionalLight(0xffffff, 0.8);
    dirLight.position.set(5, 3, 5);
    this._scene.add(dirLight);

    /* Mouse drag to rotate */
    var self = this;
    canvas.addEventListener('mousedown', function(e) {
      self._mouseDown = true;
      self._mouseX = e.clientX;
      self._mouseY = e.clientY;
      self._autoRotate = false;
    });
    canvas.addEventListener('mousemove', function(e) {
      if (!self._mouseDown) return;
      var dx = e.clientX - self._mouseX;
      var dy = e.clientY - self._mouseY;
      self._rotY += dx * 0.005;
      self._rotX += dy * 0.005;
      self._rotX = Math.max(-Math.PI / 2, Math.min(Math.PI / 2, self._rotX));
      self._mouseX = e.clientX;
      self._mouseY = e.clientY;
    });
    canvas.addEventListener('mouseup', function() { self._mouseDown = false; });
    canvas.addEventListener('mouseleave', function() { self._mouseDown = false; });

    /* Double-click to reset and re-enable auto rotation */
    canvas.addEventListener('dblclick', function() {
      self._autoRotate = true;
      self._rotX = 0.3;
    });

    /* Handle resize */
    window.addEventListener('resize', function() {
      if (!self._renderer || !self._camera) return;
      var c = document.getElementById('globe-canvas');
      if (!c) return;
      var parent = c.parentElement;
      self._camera.aspect = parent.clientWidth / parent.clientHeight;
      self._camera.updateProjectionMatrix();
      self._renderer.setSize(parent.clientWidth, parent.clientHeight);
    });

    var ph = document.getElementById('globe-placeholder');
    if (ph) ph.style.display = 'none';
  },

  _latLngToVec3: function(lat, lng, radius) {
    var phi = (90 - lat) * Math.PI / 180;
    var theta = (lng + 180) * Math.PI / 180;
    return new THREE.Vector3(
      -radius * Math.sin(phi) * Math.cos(theta),
      radius * Math.cos(phi),
      radius * Math.sin(phi) * Math.sin(theta)
    );
  },

  _loadCircuits: async function() {
    /* Fetch circuits, resolve relay IPs to countries via geoip API,
     * place relay nodes on globe surface, draw arcs between circuit hops */
    try {
      var data = await TorAPI.getCircuits();
      var circuits = Array.isArray(data) ? data : (data && data.circuits ? data.circuits : []);

      /* Clear existing markers */
      this._clearGroup(this._relayGroup);
      this._clearGroup(this._arcGroup);

      var seen = {};
      var relayPositions = {};

      for (var i = 0; i < circuits.length; i++) {
        var path = circuits[i].path || [];
        var prevPos = null;

        for (var j = 0; j < path.length; j++) {
          var relay = path[j];
          var ip = typeof relay === 'string' ? '' : (relay.ip || relay.address || '');
          var country = typeof relay === 'string' ? '' : (relay.country || '');
          var relayId = ip || (typeof relay === 'string' ? relay : (relay.fingerprint || ''));

          /* Determine position via geoip or country centroids */
          var lat = null;
          var lng = null;

          if (relayPositions[relayId]) {
            lat = relayPositions[relayId].lat;
            lng = relayPositions[relayId].lng;
          } else if (ip) {
            try {
              var geo = await TorAPI.getGeoip(ip);
              if (geo && geo.latitude !== undefined) {
                lat = geo.latitude;
                lng = geo.longitude;
              } else if (geo && geo.country && this._centroids[geo.country]) {
                lat = this._centroids[geo.country][0];
                lng = this._centroids[geo.country][1];
              }
            } catch (e) {
              /* GeoIP failed, try country code */
            }
          }

          /* Fallback to country code from relay info */
          if (lat === null && country && this._centroids[country.toUpperCase()]) {
            var c = this._centroids[country.toUpperCase()];
            lat = c[0];
            lng = c[1];
          }

          /* Random position if nothing else works */
          if (lat === null) {
            lat = (Math.random() - 0.5) * 120;
            lng = (Math.random() - 0.5) * 360;
          }

          if (relayId) {
            relayPositions[relayId] = { lat: lat, lng: lng };
          }

          /* Color by role: guard=green, middle=blue, exit=red */
          var color = 0x58a6ff;
          if (j === 0) color = 0x3fb950;
          else if (j === path.length - 1) color = 0xf85149;

          /* Add relay dot (only once per relay) */
          if (!seen[relayId]) {
            seen[relayId] = true;
            this._addRelay(lat, lng, color);
          }

          /* Draw arc from previous hop */
          var currentPos = this._latLngToVec3(lat, lng, 1.02);
          if (prevPos) {
            this._addArc(prevPos, currentPos, 0xa855f7);
          }
          prevPos = currentPos;
        }
      }

      /* Update info display */
      var infoEl = document.getElementById('globe-info');
      if (infoEl) {
        var relayCount = Object.keys(seen).length;
        infoEl.textContent = relayCount + ' relay' +
          (relayCount !== 1 ? 's' : '') + ' from ' +
          circuits.length + ' circuit' + (circuits.length !== 1 ? 's' : '');
        infoEl.style.display = relayCount > 0 ? '' : 'none';
      }
    } catch (e) {
      console.error('Failed to load circuits for globe:', e);
    }
  },

  _addRelay: function(lat, lng, color) {
    if (!this._relayGroup) return;
    var pos = this._latLngToVec3(lat, lng, 1.02);
    var dotGeo = new THREE.SphereGeometry(0.015, 8, 8);
    var dotMat = new THREE.MeshBasicMaterial({ color: color });
    var dot = new THREE.Mesh(dotGeo, dotMat);
    dot.position.copy(pos);
    this._relayGroup.add(dot);
  },

  _addArc: function(from, to, color) {
    if (!this._arcGroup) return;
    /* Draw a curved line (great circle arc) between two points.
     * Use a CatmullRomCurve3 with the midpoint elevated above the surface. */
    var mid = new THREE.Vector3().addVectors(from, to).multiplyScalar(0.5);
    var elevation = from.distanceTo(to) * 0.5 + 1.05;
    mid.normalize().multiplyScalar(elevation);

    var curve = new THREE.CatmullRomCurve3([from, mid, to]);
    var points = curve.getPoints(32);
    var geom = new THREE.BufferGeometry().setFromPoints(points);
    var mat = new THREE.LineBasicMaterial({
      color: color, transparent: true, opacity: 0.6
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

  _animate: function() {
    if (!this._isVisible) return;
    var self = this;
    this._animationId = requestAnimationFrame(function() { self._animate(); });

    /* Slowly rotate globe when auto-rotate is on */
    if (this._autoRotate) {
      this._rotY += 0.001;
    }

    if (this._globe) {
      this._globe.rotation.x = this._rotX;
      this._globe.rotation.y = this._rotY;
    }
    if (this._relayGroup) {
      this._relayGroup.rotation.x = this._rotX;
      this._relayGroup.rotation.y = this._rotY;
    }
    if (this._arcGroup) {
      this._arcGroup.rotation.x = this._rotX;
      this._arcGroup.rotation.y = this._rotY;
    }
    if (this._renderer && this._scene && this._camera) {
      this._renderer.render(this._scene, this._camera);
    }
  },

  onHide: function() {
    this._isVisible = false;
    if (this._animationId) {
      cancelAnimationFrame(this._animationId);
      this._animationId = null;
    }
  }
};
