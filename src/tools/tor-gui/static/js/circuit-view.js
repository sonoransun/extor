/* circuit-view.js - 2D SVG circuit path visualization */

var CircuitView = {
  _circuits: [],
  _filterStatus: 'all',

  init: function() {
    var page = document.getElementById('page-circuits');
    page.innerHTML =
      '<h2>Circuits</h2>' +
      '<div class="toolbar" style="margin-top:16px">' +
        '<button class="btn btn-secondary" onclick="CircuitView.refresh()">Refresh</button>' +
        '<button class="btn btn-primary" onclick="CircuitView.newIdentity()">New Identity</button>' +
        '<select class="select" id="circuit-filter" style="width:200px" ' +
          'onchange="CircuitView.filter()">' +
          '<option value="all">All Circuits</option>' +
          '<option value="BUILT">Built</option>' +
          '<option value="EXTENDED">Extended</option>' +
          '<option value="LAUNCHED">Launched</option>' +
        '</select>' +
        '<span id="circuit-count" style="font-size:13px;color:var(--text-secondary)"></span>' +
      '</div>' +
      '<div id="circuit-list" style="margin-top:16px"></div>';

    TorWS.on('circuit', function() { CircuitView.refresh(); });
    this.refresh();
  },

  refresh: async function() {
    try {
      var data = await TorAPI.getCircuits();
      this._circuits = Array.isArray(data) ? data : (data && data.circuits ? data.circuits : []);
      this._render();
    } catch (e) {
      console.error('Failed to load circuits:', e);
    }
  },

  _render: function(filterOverride) {
    var container = document.getElementById('circuit-list');
    var countEl = document.getElementById('circuit-count');
    if (!container) return;

    var statusFilter = filterOverride ||
      (document.getElementById('circuit-filter')
        ? document.getElementById('circuit-filter').value
        : 'all');

    var filtered = [];
    for (var i = 0; i < this._circuits.length; i++) {
      var c = this._circuits[i];
      var st = (c.status || c.state || '').toUpperCase();
      if (statusFilter === 'all' || st === statusFilter) {
        filtered.push(c);
      }
    }

    if (countEl) {
      countEl.textContent = filtered.length + ' circuit' +
        (filtered.length !== 1 ? 's' : '') +
        (statusFilter !== 'all' ? ' (' + statusFilter + ')' : '');
    }

    if (filtered.length === 0) {
      container.innerHTML =
        '<div class="card"><div class="empty-state">' +
          '<p>No circuits' +
          (statusFilter !== 'all' ? ' matching filter "' + this._esc(statusFilter) + '"' : '') +
          '</p>' +
        '</div></div>';
      return;
    }

    var html = '';
    for (var j = 0; j < filtered.length; j++) {
      html += this._buildCircuitCard(filtered[j]);
    }
    container.innerHTML = html;
  },

  _buildCircuitCard: function(circuit) {
    var id = circuit.id || circuit.circuitId || circuit.circuit_id || '?';
    var status = (circuit.status || circuit.state || 'UNKNOWN').toUpperCase();
    var purpose = circuit.purpose || '';
    var hops = circuit.path || [];

    /* Normalize hops: ensure each is an object */
    var normalizedHops = [];
    for (var h = 0; h < hops.length; h++) {
      if (typeof hops[h] === 'string') {
        var parts = hops[h].split('~');
        normalizedHops.push({
          fingerprint: parts[0] || '',
          name: parts[1] || parts[0].substring(0, 8)
        });
      } else {
        normalizedHops.push(hops[h]);
      }
    }

    var statusClass = 'badge-info';
    if (status === 'BUILT') statusClass = 'badge-success';
    else if (status === 'EXTENDED') statusClass = 'badge-warning';
    else if (status === 'FAILED' || status === 'CLOSED') statusClass = 'badge-danger';

    var purposeText = purpose ? ' - ' + this._esc(purpose) : '';

    var svgHtml = this._createCircuitSVG(normalizedHops);

    /* Build hop detail list */
    var hopsHtml = '';
    for (var i = 0; i < normalizedHops.length; i++) {
      var hop = normalizedHops[i];
      var name = hop.name || hop.nickname || '';
      var ip = hop.ip || hop.address || '';
      var fingerprint = hop.fingerprint || hop.identity || '';

      var role = 'Middle';
      var roleColor = 'var(--accent-blue)';
      if (i === 0) { role = 'Guard'; roleColor = 'var(--accent-green)'; }
      else if (i === normalizedHops.length - 1) { role = 'Exit'; roleColor = 'var(--accent-red)'; }

      var country = hop.country || '';

      hopsHtml += '<div style="display:flex;align-items:center;gap:8px;' +
        'padding:4px 0;font-size:12px">' +
        '<span style="width:8px;height:8px;border-radius:50%;background:' +
          roleColor + ';flex-shrink:0"></span>' +
        '<span style="font-weight:600;min-width:48px">' + role + '</span>' +
        '<span class="mono" style="color:var(--text-secondary)">' +
          this._esc(name || (fingerprint ? fingerprint.substring(0, 8) + '...' : '?')) +
        '</span>' +
        (ip ? '<span class="mono" style="color:var(--text-muted)">' +
          this._esc(ip) + '</span>' : '') +
        (country ? '<span style="color:var(--text-muted);font-size:10px">' +
          this._esc(country) + '</span>' : '');

      /* Exclude buttons: by relay and by country */
      if (fingerprint && typeof RelayExclude !== 'undefined') {
        hopsHtml +=
          '<button class="btn btn-danger btn-sm" style="padding:1px 6px;' +
            'font-size:10px;margin-left:auto" onclick="RelayExclude' +
            '.excludeRelayFromCircuit(\'' + this._esc(fingerprint) + '\',\'' +
            this._esc(name) + '\')" title="Exclude this relay">Exclude</button>';
      }
      if (country && typeof RelayExclude !== 'undefined') {
        hopsHtml +=
          '<button class="btn btn-secondary btn-sm" style="padding:1px 6px;' +
            'font-size:10px" onclick="RelayExclude.excludeCountry(\'' +
            this._esc(country) + '\')" title="Exclude all relays in ' +
            this._esc(country) + '">Ban ' + this._esc(country) + '</button>';
      }

      hopsHtml += '</div>';
    }

    return '<div class="card" style="margin-bottom:12px">' +
      '<div style="display:flex;justify-content:space-between;align-items:center;' +
        'margin-bottom:12px">' +
        '<div style="display:flex;align-items:center;gap:8px">' +
          '<strong>Circuit #' + this._esc(String(id)) + '</strong>' +
          '<span class="badge ' + statusClass + '">' + this._esc(status) + '</span>' +
          '<span style="font-size:12px;color:var(--text-muted)">' +
            this._esc(purposeText) + '</span>' +
        '</div>' +
        '<span style="font-size:12px;color:var(--text-muted)">' +
          normalizedHops.length + ' hop' + (normalizedHops.length !== 1 ? 's' : '') + '</span>' +
      '</div>' +
      '<div style="overflow-x:auto;padding:8px 0">' + svgHtml + '</div>' +
      (hopsHtml ? '<div style="margin-top:12px;border-top:1px solid var(--border-color);' +
        'padding-top:12px">' + hopsHtml + '</div>' : '') +
    '</div>';
  },

  _createCircuitSVG: function(hops) {
    var width = 640;
    var height = 80;
    /* Nodes: You + each hop + Destination */
    var nodeCount = hops.length + 2;
    var spacing = width / (nodeCount + 1);
    var cy = 34;

    var svg = '<svg viewBox="0 0 ' + width + ' ' + height + '" ' +
      'width="100%" style="max-width:' + width + 'px" ' +
      'xmlns="http://www.w3.org/2000/svg">';

    /* Build node list */
    var nodes = [];
    nodes.push({ x: spacing, label: 'You', color: '#a855f7' });

    for (var i = 0; i < hops.length; i++) {
      var hop = hops[i];
      var label = hop.name || hop.nickname ||
        (hop.fingerprint ? hop.fingerprint.substring(0, 6) : 'Relay');
      var color = '#58a6ff';
      if (i === 0) color = '#3fb950';
      else if (i === hops.length - 1) color = '#f85149';
      nodes.push({ x: spacing * (i + 2), label: label, color: color });
    }

    nodes.push({ x: spacing * (hops.length + 2), label: 'Dest', color: '#d29922' });

    /* Draw connecting lines */
    for (var j = 0; j < nodes.length - 1; j++) {
      var x1 = nodes[j].x + 15;
      var x2 = nodes[j + 1].x - 15;
      svg += '<line x1="' + x1 + '" y1="' + cy + '" ' +
        'x2="' + x2 + '" y2="' + cy + '" ' +
        'stroke="#30363d" stroke-width="2" stroke-dasharray="4,3"/>';

      /* Arrowhead */
      var ax = x2 - 2;
      svg += '<polygon points="' +
        (ax - 5) + ',' + (cy - 3) + ' ' +
        ax + ',' + cy + ' ' +
        (ax - 5) + ',' + (cy + 3) + '" ' +
        'fill="#30363d"/>';
    }

    /* Draw node circles and labels */
    for (var k = 0; k < nodes.length; k++) {
      var n = nodes[k];
      var r = 14;

      /* Outer filled circle (translucent) */
      svg += '<circle cx="' + n.x + '" cy="' + cy + '" r="' + r + '" ' +
        'fill="' + n.color + '" opacity="0.15"/>';

      /* Inner stroke circle */
      svg += '<circle cx="' + n.x + '" cy="' + cy + '" r="' + (r - 2) + '" ' +
        'fill="none" stroke="' + n.color + '" stroke-width="2"/>';

      /* Center dot */
      svg += '<circle cx="' + n.x + '" cy="' + cy + '" r="3" ' +
        'fill="' + n.color + '"/>';

      /* Label below circle */
      var truncLabel = n.label.length > 8
        ? n.label.substring(0, 7) + '.'
        : n.label;
      svg += '<text x="' + n.x + '" y="' + (cy + r + 14) + '" ' +
        'text-anchor="middle" fill="#8b949e" font-size="10" ' +
        'font-family="-apple-system, sans-serif">' +
        this._esc(truncLabel) + '</text>';
    }

    svg += '</svg>';
    return svg;
  },

  filter: function() {
    var val = document.getElementById('circuit-filter').value;
    this._filterStatus = val;
    this._render(val);
  },

  newIdentity: async function() {
    try {
      await TorAPI.sendSignal('NEWNYM');
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('New identity requested. Circuits will be rebuilt.', 'success');
      }
      setTimeout(function() { CircuitView.refresh(); }, 2000);
    } catch (e) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Failed: ' + e.message, 'error');
      }
    }
  },

  _esc: function(str) {
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
};
