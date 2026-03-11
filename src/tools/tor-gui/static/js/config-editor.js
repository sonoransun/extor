/* config-editor.js - Tor configuration editor */

var ConfigEditor = {
  _rawConfig: '',
  _formFields: [
    { key: 'SocksPort', label: 'SOCKS Port', type: 'text', hint: 'e.g. 9050 or 127.0.0.1:9050' },
    { key: 'ControlPort', label: 'Control Port', type: 'text', hint: 'e.g. 9051' },
    { key: 'UseBridges', label: 'Use Bridges', type: 'checkbox' },
    { key: 'EntryNodes', label: 'Entry Nodes', type: 'text', hint: 'Country codes or fingerprints, e.g. {us},{de}' },
    { key: 'ExitNodes', label: 'Exit Nodes', type: 'text', hint: 'Country codes or fingerprints, e.g. {us},{de}' },
    { key: 'ExcludeNodes', label: 'Exclude Nodes', type: 'text', hint: 'Nodes to avoid, e.g. {cn},{ru}' },
    { key: 'StrictNodes', label: 'Strict Nodes', type: 'checkbox' },
    { key: 'ConnectionPadding', label: 'Connection Padding', type: 'select',
      options: [
        { value: 'auto', label: 'Auto' },
        { value: '0', label: 'Disabled' },
        { value: '1', label: 'Enabled' }
      ]
    },
    { key: 'AvoidDiskWrites', label: 'Avoid Disk Writes', type: 'checkbox' },
    { key: 'SafeLogging', label: 'Safe Logging', type: 'select',
      options: [
        { value: '1', label: 'Enabled (scrub sensitive strings)' },
        { value: '0', label: 'Disabled (log everything)' },
        { value: 'relay', label: 'Relay (scrub only relay addresses)' }
      ]
    }
  ],
  _currentValues: {},

  init: function() {
    var page = document.getElementById('page-config');
    page.innerHTML =
      '<h2>Configuration</h2>' +
      '<div class="toolbar" style="margin-top:16px">' +
        '<button class="btn btn-primary" onclick="ConfigEditor.save()">Save Config</button>' +
        '<button class="btn btn-secondary" onclick="ConfigEditor.reload()">Reload</button>' +
        '<button class="btn btn-secondary" onclick="ConfigEditor.refresh()">Refresh</button>' +
      '</div>' +
      '<div class="grid grid-2" style="margin-top:16px">' +
        '<div class="card">' +
          '<div class="card-label">Common Settings</div>' +
          '<div id="config-form" style="margin-top:12px"></div>' +
        '</div>' +
        '<div class="card">' +
          '<div class="card-label">Raw torrc</div>' +
          '<textarea id="config-raw" class="textarea" ' +
            'style="margin-top:12px;height:500px;font-family:monospace;font-size:13px;resize:vertical" ' +
            'spellcheck="false"></textarea>' +
          '<div style="margin-top:8px;display:flex;gap:8px">' +
            '<button class="btn btn-secondary btn-sm" onclick="ConfigEditor.applyRaw()">Apply Raw</button>' +
            '<button class="btn btn-secondary btn-sm" onclick="ConfigEditor.refreshRaw()">Refresh Raw</button>' +
          '</div>' +
        '</div>' +
      '</div>';
    this._buildForm();
    this.refresh();
  },

  _buildForm: function() {
    var container = document.getElementById('config-form');
    if (!container) return;

    var html = '';
    for (var i = 0; i < this._formFields.length; i++) {
      var f = this._formFields[i];
      html += '<div class="form-group">';
      html += '<label for="cfg-' + f.key + '">' + this._esc(f.label) + '</label>';

      if (f.type === 'checkbox') {
        html += '<div style="margin-top:4px">' +
          '<label style="display:flex;align-items:center;gap:8px;font-weight:normal;cursor:pointer">' +
            '<input type="checkbox" id="cfg-' + f.key + '" data-config-key="' + f.key + '">' +
            '<span style="font-size:13px;color:var(--text-secondary)">Enable ' + this._esc(f.label) + '</span>' +
          '</label>' +
        '</div>';
      } else if (f.type === 'select') {
        html += '<select class="select" id="cfg-' + f.key + '" data-config-key="' + f.key + '" ' +
          'style="margin-top:4px">';
        for (var j = 0; j < f.options.length; j++) {
          html += '<option value="' + this._escAttr(f.options[j].value) + '">' +
            this._esc(f.options[j].label) + '</option>';
        }
        html += '</select>';
      } else {
        html += '<input class="input" type="text" id="cfg-' + f.key + '" ' +
          'data-config-key="' + f.key + '" ' +
          'placeholder="' + this._escAttr(f.hint || '') + '" style="margin-top:4px">';
      }

      if (f.hint && f.type !== 'text') {
        html += '<div class="hint">' + this._esc(f.hint) + '</div>';
      }
      html += '</div>';
    }
    container.innerHTML = html;
  },

  refresh: async function() {
    this._currentValues = {};

    /* Fetch values for each form field individually */
    var promises = [];
    for (var i = 0; i < this._formFields.length; i++) {
      promises.push(this._fetchConfigValue(this._formFields[i].key));
    }

    try {
      await Promise.all(promises);
    } catch (e) {
      /* Individual failures are handled inside _fetchConfigValue */
    }

    /* Update form fields with fetched values */
    for (var k = 0; k < this._formFields.length; k++) {
      var field = this._formFields[k];
      var el = document.getElementById('cfg-' + field.key);
      if (!el) continue;

      var val = this._currentValues[field.key];
      if (val === undefined) val = '';

      if (field.type === 'checkbox') {
        el.checked = (val === '1' || val === 'true' || val === true);
      } else {
        el.value = String(val);
      }
    }

    /* Fetch raw torrc */
    this.refreshRaw();
  },

  _fetchConfigValue: async function(key) {
    try {
      var result = await TorAPI.getConfig(key);
      if (result && result.value !== undefined) {
        this._currentValues[key] = result.value;
      } else if (typeof result === 'string') {
        this._currentValues[key] = result;
      } else if (result && result[key] !== undefined) {
        this._currentValues[key] = result[key];
      }
    } catch (e) {
      /* Config key might not be set; that is fine */
    }
  },

  refreshRaw: async function() {
    try {
      var result = await TorAPI.getAllConfig();
      var rawEl = document.getElementById('config-raw');
      if (!rawEl) return;

      if (result && typeof result === 'string') {
        rawEl.value = result;
      } else if (result && result.config && typeof result.config === 'string') {
        rawEl.value = result.config;
      } else if (result && typeof result === 'object') {
        /* Format as key-value pairs */
        var lines = [];
        var cfg = result.config || result;
        var keys = Object.keys(cfg);
        for (var i = 0; i < keys.length; i++) {
          var v = cfg[keys[i]];
          if (v !== '' && v !== null && v !== undefined) {
            lines.push(keys[i] + ' ' + v);
          }
        }
        rawEl.value = lines.join('\n');
      }
    } catch (e) {
      /* Ignore errors fetching raw config */
    }
  },

  save: async function() {
    var changes = [];
    for (var i = 0; i < this._formFields.length; i++) {
      var field = this._formFields[i];
      var el = document.getElementById('cfg-' + field.key);
      if (!el) continue;

      var newVal;
      if (field.type === 'checkbox') {
        newVal = el.checked ? '1' : '0';
      } else {
        newVal = el.value.trim();
      }

      var oldVal = String(this._currentValues[field.key] || '');
      if (newVal !== oldVal) {
        changes.push({ key: field.key, value: newVal });
      }
    }

    if (changes.length === 0) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('No changes to save.', 'info');
      }
      return;
    }

    var errors = [];
    for (var j = 0; j < changes.length; j++) {
      try {
        await TorAPI.setConfig(changes[j].key, changes[j].value);
        this._currentValues[changes[j].key] = changes[j].value;
      } catch (e) {
        errors.push(changes[j].key + ': ' + e.message);
      }
    }

    /* Persist to disk */
    try {
      await TorAPI.saveConfig();
    } catch (e) {
      errors.push('SAVECONF: ' + e.message);
    }

    if (errors.length > 0) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Some settings failed: ' + errors.join('; '), 'error');
      }
    } else {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Configuration saved (' + changes.length + ' setting' +
          (changes.length !== 1 ? 's' : '') + ').', 'success');
      }
    }
  },

  applyRaw: async function() {
    var rawEl = document.getElementById('config-raw');
    if (!rawEl || !rawEl.value.trim()) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('No configuration to apply.', 'warning');
      }
      return;
    }

    try {
      await TorAPI.loadConfig(rawEl.value);
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Raw configuration applied.', 'success');
      }
      this.refresh();
    } catch (e) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Failed to apply: ' + e.message, 'error');
      }
    }
  },

  reload: async function() {
    try {
      await TorAPI.reloadConfig();
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Configuration reloaded (SIGHUP sent).', 'success');
      }
      this.refresh();
    } catch (e) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Reload failed: ' + e.message, 'error');
      }
    }
  },

  _esc: function(str) {
    var d = document.createElement('div');
    d.appendChild(document.createTextNode(str));
    return d.innerHTML;
  },

  _escAttr: function(str) {
    return String(str).replace(/&/g, '&amp;').replace(/"/g, '&quot;')
      .replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }
};
