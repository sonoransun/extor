/* transport-setup.js - Pluggable transport setup wizard */

var TransportSetup = {
  _transports: [],
  _presets: {
    obfs4: {
      name: 'obfs4',
      description: 'Looks like nothing protocol',
      binary: 'obfs4proxy',
      paths: ['/usr/bin/obfs4proxy', '/usr/local/bin/obfs4proxy',
              '/snap/bin/obfs4proxy', '/usr/lib/tor/obfs4proxy']
    },
    snowflake: {
      name: 'snowflake',
      description: 'Uses WebRTC through volunteer proxies',
      binary: 'snowflake-client',
      paths: ['/usr/bin/snowflake-client', '/usr/local/bin/snowflake-client',
              '/snap/bin/snowflake-client']
    },
    meek_lite: {
      name: 'meek_lite',
      description: 'Domain fronting via CDN',
      binary: 'meek-client',
      paths: ['/usr/bin/meek-client', '/usr/local/bin/meek-client']
    },
    webtunnel: {
      name: 'webtunnel',
      description: 'Tunnels through HTTPS websites',
      binary: 'webtunnel-client',
      paths: ['/usr/bin/webtunnel-client', '/usr/local/bin/webtunnel-client']
    }
  },

  init: function() {
    var page = document.getElementById('page-transports');
    page.innerHTML =
      '<h2>Pluggable Transports</h2>' +
      '<p style="color:var(--text-secondary);margin-top:8px">' +
        'Configure pluggable transports to bypass censorship.</p>' +
      '<div class="grid grid-2" style="margin-top:16px" id="transport-cards"></div>' +
      '<div class="card" style="margin-top:16px">' +
        '<div class="card-label">Configured Transports</div>' +
        '<div id="transport-list" style="margin-top:12px"></div>' +
      '</div>';
    this.refresh();
  },

  refresh: async function() {
    try {
      var data = await TorAPI.getTransports();
      var raw = Array.isArray(data) ? data : (data && data.transports ? data.transports : []);
      this._transports = raw;
    } catch (e) {
      this._transports = [];
    }
    this._render();
  },

  _render: function() {
    this._renderCards();
    this._renderConfigured();
  },

  _renderCards: function() {
    var container = document.getElementById('transport-cards');
    if (!container) return;

    var html = '';
    var names = Object.keys(this._presets);
    for (var i = 0; i < names.length; i++) {
      var key = names[i];
      var preset = this._presets[key];

      var isConfigured = this._isTransportConfigured(key);
      var statusBadge = isConfigured
        ? '<span class="badge badge-success">Configured</span>'
        : '<span class="badge badge-warning">Not Configured</span>';

      html += '<div class="card">' +
        '<div style="display:flex;justify-content:space-between;align-items:center">' +
          '<strong style="font-size:16px">' + this._esc(preset.name) + '</strong>' +
          statusBadge +
        '</div>' +
        '<p style="color:var(--text-secondary);font-size:13px;margin-top:8px">' +
          this._esc(preset.description) + '</p>' +
        '<p style="font-size:12px;margin-top:4px;color:var(--text-muted)">' +
          'Binary: <code>' + this._esc(preset.binary) + '</code></p>' +
        '<button class="btn btn-primary btn-sm" style="margin-top:12px" ' +
          'onclick="TransportSetup.configure(\'' + key + '\')">' +
          (isConfigured ? 'Reconfigure' : 'Configure') +
        '</button>' +
      '</div>';
    }
    container.innerHTML = html;
  },

  _isTransportConfigured: function(name) {
    for (var j = 0; j < this._transports.length; j++) {
      var t = this._transports[j];
      if (typeof t === 'string' && t.indexOf(name) !== -1) return true;
      if (t && typeof t === 'object' && (t.name === name || t.type === name)) return true;
    }
    return false;
  },

  _renderConfigured: function() {
    var container = document.getElementById('transport-list');
    if (!container) return;

    if (this._transports.length === 0) {
      container.innerHTML =
        '<div class="empty-state">' +
          '<p>No transports configured yet.</p>' +
        '</div>';
      return;
    }

    var html = '<table class="table"><thead><tr>' +
      '<th>Transport</th><th>Binary Path</th><th>Actions</th>' +
      '</tr></thead><tbody>';

    for (var i = 0; i < this._transports.length; i++) {
      var t = this._transports[i];
      var name = '';
      var path = '';
      if (typeof t === 'string') {
        var match = t.match(/^(?:ClientTransportPlugin\s+)?(\S+)\s+exec\s+(.+)$/);
        if (match) {
          name = match[1];
          path = match[2];
        } else {
          name = t;
          path = '--';
        }
      } else {
        name = t.name || t.type || '--';
        path = t.path || t.exec || t.binary || '--';
      }

      html += '<tr>' +
        '<td><span class="badge badge-info">' + this._esc(name) + '</span></td>' +
        '<td class="mono" style="font-size:12px">' + this._esc(path) + '</td>' +
        '<td>' +
          '<button class="btn btn-danger btn-sm" ' +
            'onclick="TransportSetup.removeTransport(' + i + ')">Remove</button>' +
        '</td>' +
      '</tr>';
    }

    html += '</tbody></table>';
    container.innerHTML = html;
  },

  configure: function(type) {
    var preset = this._presets[type];
    if (!preset) return;

    var overlay = document.createElement('div');
    overlay.className = 'modal-overlay';
    overlay.id = 'transport-modal-overlay';

    var pathHints = '';
    for (var i = 0; i < preset.paths.length; i++) {
      if (i > 0) pathHints += ', ';
      pathHints += '<code>' + this._esc(preset.paths[i]) + '</code>';
    }

    overlay.innerHTML =
      '<div class="modal" onclick="event.stopPropagation()">' +
        '<div class="modal-header">' +
          '<h3>Configure ' + this._esc(preset.name) + '</h3>' +
          '<button class="modal-close" ' +
            'onclick="TransportSetup._closeModal()">&times;</button>' +
        '</div>' +
        '<div id="transport-wizard-content">' +
          '<div class="form-group">' +
            '<label>Step 1: Binary Path</label>' +
            '<p style="font-size:13px;color:var(--text-secondary);margin-bottom:8px">' +
              'Specify the path to <code>' + this._esc(preset.binary) + '</code>. ' +
              'Common locations are checked automatically.</p>' +
            '<input class="input" id="transport-binary-path" ' +
              'placeholder="' + this._esc(preset.paths[0] || '/usr/bin/' + preset.binary) + '" ' +
              'value="' + this._esc(preset.paths[0] || '') + '">' +
            '<div class="hint">Common paths: ' + pathHints + '</div>' +
          '</div>' +
          '<div class="form-group">' +
            '<label>Step 2: Transport Options (optional)</label>' +
            '<textarea class="textarea" id="transport-options" ' +
              'style="height:80px" placeholder="Additional command-line options...">' +
            '</textarea>' +
          '</div>' +
          '<div id="transport-test-result" style="display:none;margin-bottom:16px"></div>' +
        '</div>' +
        '<div class="modal-footer">' +
          '<button class="btn btn-secondary" ' +
            'onclick="TransportSetup._closeModal()">Cancel</button>' +
          '<button class="btn btn-secondary" ' +
            'onclick="TransportSetup._testTransport(\'' + type + '\')">' +
            'Test Connection</button>' +
          '<button class="btn btn-primary" ' +
            'onclick="TransportSetup._applyTransport(\'' + type + '\')">' +
            'Apply</button>' +
        '</div>' +
      '</div>';

    document.body.appendChild(overlay);
    overlay.addEventListener('click', function(e) {
      if (e.target === overlay) TransportSetup._closeModal();
    });
  },

  _testTransport: async function(type) {
    var resultDiv = document.getElementById('transport-test-result');
    if (!resultDiv) return;

    resultDiv.style.display = '';
    resultDiv.innerHTML =
      '<div class="card" style="margin:0">' +
        '<span class="spinner"></span> Testing transport configuration...' +
      '</div>';

    var binaryPath = document.getElementById('transport-binary-path').value.trim();
    if (!binaryPath) {
      resultDiv.innerHTML =
        '<div class="card" style="margin:0;border-left:4px solid var(--accent-yellow)">' +
          'Please specify a binary path first.' +
        '</div>';
      return;
    }

    try {
      var configLine = type + ' exec ' + binaryPath;
      await TorAPI.setConfig('ClientTransportPlugin', configLine);

      resultDiv.innerHTML =
        '<div class="card" style="margin:0;border-left:4px solid var(--accent-green)">' +
          'Transport accepted by Tor. The binary path appears valid.' +
        '</div>';
    } catch (e) {
      resultDiv.innerHTML =
        '<div class="card" style="margin:0;border-left:4px solid var(--accent-red)">' +
          'Test failed: ' + this._esc(e.message) +
          '<br><span style="font-size:12px;color:var(--text-secondary)">' +
          'Make sure the binary is installed and the path is correct.</span>' +
        '</div>';
    }
  },

  _applyTransport: async function(type) {
    var binaryPath = document.getElementById('transport-binary-path').value.trim();
    if (!binaryPath) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Binary path is required.', 'error');
      }
      return;
    }

    var configLine = type + ' exec ' + binaryPath;
    var options = document.getElementById('transport-options').value.trim();
    if (options) {
      configLine += ' ' + options;
    }

    try {
      await TorAPI.setConfig('ClientTransportPlugin', configLine);
      await TorAPI.saveConfig();

      /* Update local transport list */
      var existing = false;
      for (var i = 0; i < this._transports.length; i++) {
        var t = this._transports[i];
        var tName = typeof t === 'string' ? t : (t.name || t.type || '');
        if ((typeof t === 'string' && t.indexOf(type) !== -1) ||
            (typeof t === 'object' && (t.name === type || t.type === type))) {
          this._transports[i] = { name: type, path: binaryPath };
          existing = true;
          break;
        }
      }
      if (!existing) {
        this._transports.push({ name: type, path: binaryPath });
      }

      this._closeModal();
      this._render();

      if (typeof App !== 'undefined' && App.toast) {
        App.toast(type + ' transport configured successfully.', 'success');
      }
    } catch (e) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Failed to configure transport: ' + e.message, 'error');
      }
    }
  },

  removeTransport: async function(index) {
    if (index < 0 || index >= this._transports.length) return;
    if (!confirm('Remove this transport configuration?')) return;

    this._transports.splice(index, 1);

    try {
      await TorAPI.setTransports(this._transports);
      this._render();
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Transport removed.', 'success');
      }
    } catch (e) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Failed to remove transport: ' + e.message, 'error');
      }
    }
  },

  _closeModal: function() {
    var overlay = document.getElementById('transport-modal-overlay');
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
