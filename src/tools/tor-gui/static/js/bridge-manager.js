/* bridge-manager.js - Bridge configuration management interface */

var BridgeManager = {
  _bridges: [],

  init: function() {
    var page = document.getElementById('page-bridges');
    page.innerHTML =
      '<h2>Bridge Configuration</h2>' +
      '<div class="toolbar" style="margin-top:16px">' +
        '<button class="btn btn-primary" onclick="BridgeManager.showAddDialog()">Add Bridge</button>' +
        '<button class="btn btn-secondary" onclick="BridgeManager.showPasteDialog()">Paste Bridge Lines</button>' +
        '<button class="btn btn-secondary" onclick="BridgeManager.refresh()">Refresh</button>' +
        '<button class="btn btn-secondary" onclick="BridgeManager.saveAll()">Save to Tor</button>' +
      '</div>' +
      '<div class="card" style="margin-top:16px">' +
        '<table class="table" id="bridge-table">' +
          '<thead>' +
            '<tr>' +
              '<th>Enabled</th>' +
              '<th>Transport</th>' +
              '<th>Address</th>' +
              '<th>Fingerprint</th>' +
              '<th>Actions</th>' +
            '</tr>' +
          '</thead>' +
          '<tbody id="bridge-list"></tbody>' +
        '</table>' +
        '<div id="bridge-empty" style="text-align:center;padding:40px;color:var(--text-secondary);display:none">' +
          'No bridges configured. Add bridges to connect through censorship.' +
        '</div>' +
      '</div>';
    this.refresh();
  },

  refresh: async function() {
    try {
      var data = await TorAPI.getBridges();
      var raw = Array.isArray(data) ? data : (data && data.bridges ? data.bridges : []);
      this._bridges = [];
      for (var i = 0; i < raw.length; i++) {
        if (typeof raw[i] === 'string') {
          var parsed = this._parseBridgeLine(raw[i]);
          if (parsed) this._bridges.push(parsed);
        } else if (raw[i] && typeof raw[i] === 'object') {
          this._bridges.push(raw[i]);
        }
      }
      this._render();
    } catch (e) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Failed to load bridges: ' + e.message, 'error');
      }
    }
  },

  _render: function() {
    var tbody = document.getElementById('bridge-list');
    var emptyDiv = document.getElementById('bridge-empty');
    var tableEl = document.getElementById('bridge-table');
    if (!tbody) return;

    if (this._bridges.length === 0) {
      tbody.innerHTML = '';
      if (tableEl) tableEl.style.display = 'none';
      if (emptyDiv) emptyDiv.style.display = '';
      return;
    }

    if (tableEl) tableEl.style.display = '';
    if (emptyDiv) emptyDiv.style.display = 'none';

    var rows = '';
    for (var i = 0; i < this._bridges.length; i++) {
      var b = this._bridges[i];
      var transport = b.transport || 'vanilla';
      var fingerShort = b.fingerprint
        ? b.fingerprint.substring(0, 8) + '...'
        : '--';

      var badgeClass = 'badge badge-info';
      if (transport === 'obfs4') badgeClass = 'badge badge-success';
      else if (transport === 'snowflake') badgeClass = 'badge badge-warning';
      else if (transport === 'meek_lite') badgeClass = 'badge badge-danger';
      else if (transport === 'webtunnel') badgeClass = 'badge badge-info';

      rows += '<tr>' +
        '<td><input type="checkbox"' + (b.enabled !== false ? ' checked' : '') +
          ' onchange="BridgeManager.toggleBridge(' + i + ')"></td>' +
        '<td><span class="' + badgeClass + '">' + this._esc(transport) + '</span></td>' +
        '<td class="mono" style="font-size:12px;max-width:260px" title="' +
          this._esc(b.address || '') + '">' + this._esc(b.address || '') + '</td>' +
        '<td class="mono" style="font-size:12px" title="' +
          this._esc(b.fingerprint || '') + '">' + this._esc(fingerShort) + '</td>' +
        '<td style="white-space:nowrap">' +
          '<button class="btn btn-secondary btn-sm" onclick="BridgeManager.editBridge(' +
            i + ')">Edit</button> ' +
          '<button class="btn btn-danger btn-sm" onclick="BridgeManager.removeBridge(' +
            i + ')">Delete</button>' +
        '</td>' +
      '</tr>';
    }
    tbody.innerHTML = rows;
  },

  showAddDialog: function() {
    this._showBridgeForm(-1, {
      transport: '',
      address: '',
      fingerprint: '',
      args: '',
      enabled: true
    });
  },

  _showBridgeForm: function(index, bridge) {
    var isEdit = index >= 0;
    var title = isEdit ? 'Edit Bridge' : 'Add Bridge';

    var overlay = document.createElement('div');
    overlay.className = 'modal-overlay';
    overlay.id = 'bridge-modal-overlay';

    var transports = ['', 'obfs4', 'snowflake', 'meek_lite', 'webtunnel'];
    var transportLabels = {
      '': 'None (vanilla)',
      'obfs4': 'obfs4',
      'snowflake': 'snowflake',
      'meek_lite': 'meek_lite',
      'webtunnel': 'webtunnel'
    };
    var transportOpts = '';
    for (var i = 0; i < transports.length; i++) {
      var t = transports[i];
      transportOpts += '<option value="' + t + '"' +
        ((bridge.transport || '') === t ? ' selected' : '') + '>' +
        transportLabels[t] + '</option>';
    }

    /* For the args field, expand space-separated key=val into lines */
    var argsDisplay = bridge.args || '';
    if (argsDisplay && argsDisplay.indexOf('\n') === -1) {
      argsDisplay = argsDisplay.replace(/\s+(?=\S+=)/g, '\n');
    }

    overlay.innerHTML =
      '<div class="modal" onclick="event.stopPropagation()">' +
        '<div class="modal-header">' +
          '<h3>' + title + '</h3>' +
          '<button class="modal-close" onclick="BridgeManager._closeModal()">&times;</button>' +
        '</div>' +
        '<div class="form-group">' +
          '<label>Transport Type</label>' +
          '<select class="select" id="bridge-form-transport" ' +
            'onchange="BridgeManager._onTransportChange()">' +
            transportOpts +
          '</select>' +
        '</div>' +
        '<div class="form-group">' +
          '<label>Address (IP:Port)</label>' +
          '<input class="input" id="bridge-form-address" ' +
            'placeholder="198.51.100.1:443" value="' +
            this._esc(bridge.address || '') + '">' +
        '</div>' +
        '<div class="form-group">' +
          '<label>Fingerprint (40 hex characters, optional)</label>' +
          '<input class="input" id="bridge-form-fingerprint" ' +
            'placeholder="ABCDEF1234567890ABCDEF1234567890ABCDEF12" ' +
            'maxlength="40" value="' +
            this._esc(bridge.fingerprint || '') + '">' +
        '</div>' +
        '<div class="form-group">' +
          '<label>Transport Arguments (key=value per line)</label>' +
          '<textarea class="textarea" id="bridge-form-args" ' +
            'style="height:100px" placeholder="cert=...\niat-mode=0">' +
            this._esc(argsDisplay) + '</textarea>' +
          '<div class="hint" id="bridge-form-args-hint"></div>' +
        '</div>' +
        '<div class="modal-footer">' +
          '<button class="btn btn-secondary" ' +
            'onclick="BridgeManager._closeModal()">Cancel</button>' +
          '<button class="btn btn-primary" ' +
            'onclick="BridgeManager._saveBridgeForm(' + index + ')">' +
            (isEdit ? 'Update' : 'Add') + '</button>' +
        '</div>' +
      '</div>';

    document.body.appendChild(overlay);
    overlay.addEventListener('click', function(e) {
      if (e.target === overlay) BridgeManager._closeModal();
    });
  },

  _onTransportChange: function() {
    var transport = document.getElementById('bridge-form-transport').value;
    var argsField = document.getElementById('bridge-form-args');
    var hint = document.getElementById('bridge-form-args-hint');
    if (!argsField || !hint) return;

    if (transport === 'obfs4' && !argsField.value.trim()) {
      argsField.value = 'cert=\niat-mode=0';
      hint.textContent = 'obfs4 requires a cert value and iat-mode.';
    } else if (transport === 'snowflake' && !argsField.value.trim()) {
      argsField.value =
        'url=https://snowflake-broker.torproject.net.global.prod.fastly.net/\n' +
        'front=foursquare.com\n' +
        'ice=stun:stun.l.google.com:19302';
      hint.textContent = 'Snowflake uses WebRTC through volunteer proxies.';
    } else if (transport === 'meek_lite' && !argsField.value.trim()) {
      argsField.value = 'url=https://meek.azureedge.net/\nfront=ajax.aspnetcdn.com';
      hint.textContent = 'meek_lite uses domain fronting through a CDN.';
    } else if (transport === 'webtunnel' && !argsField.value.trim()) {
      argsField.value = 'url=https://';
      hint.textContent = 'webtunnel tunnels through HTTPS websites.';
    } else {
      hint.textContent = '';
    }
  },

  _saveBridgeForm: function(index) {
    var transport = document.getElementById('bridge-form-transport').value;
    var address = document.getElementById('bridge-form-address').value.trim();
    var fingerprint = document.getElementById('bridge-form-fingerprint').value.trim();
    var argsRaw = document.getElementById('bridge-form-args').value.trim();

    if (!address) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Address is required.', 'error');
      }
      return;
    }

    if (fingerprint && !/^[0-9A-Fa-f]{40}$/.test(fingerprint)) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Fingerprint must be 40 hexadecimal characters.', 'error');
      }
      return;
    }

    /* Collapse multi-line args into space-separated key=value pairs */
    var args = argsRaw.split('\n').map(function(l) {
      return l.trim();
    }).filter(Boolean).join(' ');

    var bridge = {
      transport: transport,
      address: address,
      fingerprint: fingerprint,
      args: args,
      enabled: true
    };

    if (index >= 0 && index < this._bridges.length) {
      bridge.enabled = this._bridges[index].enabled;
      this._bridges[index] = bridge;
    } else {
      this._bridges.push(bridge);
    }

    this._closeModal();
    this._render();
  },

  showPasteDialog: function() {
    var overlay = document.createElement('div');
    overlay.className = 'modal-overlay';
    overlay.id = 'bridge-modal-overlay';

    overlay.innerHTML =
      '<div class="modal" onclick="event.stopPropagation()">' +
        '<div class="modal-header">' +
          '<h3>Paste Bridge Lines</h3>' +
          '<button class="modal-close" ' +
            'onclick="BridgeManager._closeModal()">&times;</button>' +
        '</div>' +
        '<div class="form-group">' +
          '<label>Paste bridge lines (one per line)</label>' +
          '<textarea class="textarea" id="bridge-paste-text" style="height:200px" ' +
            'placeholder="obfs4 198.51.100.1:443 FINGERPRINT cert=... iat-mode=0\n' +
            'snowflake 192.0.2.3:1 2B280B23E1107BB62ABFC40DDCC8824814F80A72\n' +
            'torbridge://..."></textarea>' +
          '<div class="hint">Accepts raw bridge lines, one per line. ' +
            'Also supports torbridge:// URIs. Lines starting with # are skipped.</div>' +
        '</div>' +
        '<div class="modal-footer">' +
          '<button class="btn btn-secondary" ' +
            'onclick="BridgeManager._closeModal()">Cancel</button>' +
          '<button class="btn btn-primary" ' +
            'onclick="BridgeManager._importPasted()">Import</button>' +
        '</div>' +
      '</div>';

    document.body.appendChild(overlay);
    overlay.addEventListener('click', function(e) {
      if (e.target === overlay) BridgeManager._closeModal();
    });
  },

  _importPasted: function() {
    var text = document.getElementById('bridge-paste-text').value;
    if (!text.trim()) {
      this._closeModal();
      return;
    }

    var lines = text.split('\n');
    var added = 0;
    for (var i = 0; i < lines.length; i++) {
      var line = lines[i].trim();
      if (!line || line.charAt(0) === '#') continue;

      /* Strip leading "Bridge " keyword if present */
      if (/^Bridge\s+/i.test(line)) {
        line = line.replace(/^Bridge\s+/i, '');
      }

      var bridge = null;
      if (line.indexOf('torbridge://') === 0) {
        bridge = this._parseTorbridgeUri(line);
      } else {
        bridge = this._parseBridgeLine(line);
      }

      if (bridge) {
        this._bridges.push(bridge);
        added++;
      }
    }

    this._closeModal();
    this._render();
    if (typeof App !== 'undefined' && App.toast) {
      App.toast('Imported ' + added + ' bridge' + (added !== 1 ? 's' : '') + '.', 'success');
    }
  },

  saveAll: async function() {
    try {
      var bridgeLines = this._bridges.filter(function(b) {
        return b.enabled !== false;
      }).map(function(b) {
        return BridgeManager._formatBridge(b);
      });
      await TorAPI.setBridges(bridgeLines);
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Bridges saved to Tor.', 'success');
      }
    } catch (e) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Failed to save bridges: ' + e.message, 'error');
      }
    }
  },

  _parseBridgeLine: function(line) {
    line = line.trim();
    if (!line || line.charAt(0) === '#') return null;

    var parts = line.split(/\s+/);
    var idx = 0;
    var transport = '';
    var address = '';
    var fingerprint = '';

    /* If the first token looks like an address (starts with a digit or [),
     * it is a vanilla bridge. Otherwise the first token is the transport. */
    if (parts[idx] && /^[\d\[]/.test(parts[idx])) {
      address = parts[idx++];
    } else {
      transport = parts[idx++] || '';
      address = parts[idx++] || '';
    }

    /* Fingerprint: exactly 40 hex characters */
    if (parts[idx] && /^[0-9A-Fa-f]{40}$/.test(parts[idx])) {
      fingerprint = parts[idx++];
    }

    /* Remaining tokens are transport arguments */
    var args = parts.slice(idx).join(' ');

    return {
      transport: transport,
      address: address,
      fingerprint: fingerprint,
      args: args,
      enabled: true
    };
  },

  _parseTorbridgeUri: function(uri) {
    try {
      var withoutScheme = uri.replace('torbridge://', '');
      var qIdx = withoutScheme.indexOf('?');
      var pathPart = qIdx >= 0 ? withoutScheme.substring(0, qIdx) : withoutScheme;
      var queryPart = qIdx >= 0 ? withoutScheme.substring(qIdx + 1) : '';

      var pathSegments = pathPart.split('/');
      var transport = pathSegments[0] || '';
      var address = pathSegments.slice(1).join('/') || '';

      /* If transport looks like an IP address, it is a vanilla bridge */
      if (/^[\d\[]/.test(transport)) {
        address = transport + (address ? '/' + address : '');
        transport = '';
      }

      var params = {};
      if (queryPart) {
        var pairs = queryPart.split('&');
        for (var i = 0; i < pairs.length; i++) {
          var eqIdx = pairs[i].indexOf('=');
          if (eqIdx >= 0) {
            params[decodeURIComponent(pairs[i].substring(0, eqIdx))] =
              decodeURIComponent(pairs[i].substring(eqIdx + 1));
          }
        }
      }

      return {
        transport: transport,
        address: address,
        fingerprint: params.fingerprint || '',
        args: params.args || '',
        enabled: true
      };
    } catch (e) {
      return this._parseBridgeLine(uri.replace('torbridge://', ''));
    }
  },

  _formatBridge: function(b) {
    var line = '';
    if (b.transport) line += b.transport + ' ';
    line += b.address;
    if (b.fingerprint) line += ' ' + b.fingerprint;
    if (b.args) line += ' ' + b.args;
    return line;
  },

  editBridge: function(index) {
    if (index < 0 || index >= this._bridges.length) return;
    var b = this._bridges[index];
    this._showBridgeForm(index, {
      transport: b.transport || '',
      address: b.address || '',
      fingerprint: b.fingerprint || '',
      args: b.args || '',
      enabled: b.enabled !== false
    });
  },

  removeBridge: function(index) {
    if (index < 0 || index >= this._bridges.length) return;
    this._bridges.splice(index, 1);
    this._render();
  },

  toggleBridge: function(index) {
    if (index < 0 || index >= this._bridges.length) return;
    this._bridges[index].enabled = !this._bridges[index].enabled;
    this._render();
  },

  _closeModal: function() {
    var overlay = document.getElementById('bridge-modal-overlay');
    if (overlay && overlay.parentNode) {
      overlay.parentNode.removeChild(overlay);
    }
  },

  _esc: function(str) {
    var d = document.createElement('div');
    d.appendChild(document.createTextNode(str));
    return d.innerHTML;
  }
};
