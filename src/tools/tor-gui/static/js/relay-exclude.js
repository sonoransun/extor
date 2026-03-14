/* relay-exclude.js - Relay blacklist / exclusion management */

var RelayExclude = {
  _excludeNodes: '',
  _excludeExitNodes: '',
  _strictNodes: false,
  _activeRelays: [],

  /* All known countries with centroids (for the picker) */
  _countries: [
    {code:'US',name:'United States'},{code:'DE',name:'Germany'},
    {code:'FR',name:'France'},{code:'GB',name:'United Kingdom'},
    {code:'NL',name:'Netherlands'},{code:'CA',name:'Canada'},
    {code:'SE',name:'Sweden'},{code:'CH',name:'Switzerland'},
    {code:'AT',name:'Austria'},{code:'RO',name:'Romania'},
    {code:'FI',name:'Finland'},{code:'NO',name:'Norway'},
    {code:'RU',name:'Russia'},{code:'JP',name:'Japan'},
    {code:'AU',name:'Australia'},{code:'BR',name:'Brazil'},
    {code:'IN',name:'India'},{code:'CN',name:'China'},
    {code:'SG',name:'Singapore'},{code:'KR',name:'South Korea'},
    {code:'IT',name:'Italy'},{code:'ES',name:'Spain'},
    {code:'PL',name:'Poland'},{code:'CZ',name:'Czech Republic'},
    {code:'UA',name:'Ukraine'},{code:'HU',name:'Hungary'},
    {code:'BG',name:'Bulgaria'},{code:'IS',name:'Iceland'},
    {code:'LU',name:'Luxembourg'},{code:'IE',name:'Ireland'},
    {code:'DK',name:'Denmark'},{code:'LT',name:'Lithuania'},
    {code:'LV',name:'Latvia'},{code:'EE',name:'Estonia'},
    {code:'GR',name:'Greece'},{code:'PT',name:'Portugal'},
    {code:'BE',name:'Belgium'},{code:'HR',name:'Croatia'},
    {code:'RS',name:'Serbia'},{code:'SK',name:'Slovakia'},
    {code:'SI',name:'Slovenia'},{code:'MD',name:'Moldova'},
    {code:'ZA',name:'South Africa'},{code:'IL',name:'Israel'},
    {code:'TW',name:'Taiwan'},{code:'HK',name:'Hong Kong'},
    {code:'NZ',name:'New Zealand'},{code:'MX',name:'Mexico'},
    {code:'AR',name:'Argentina'},{code:'CL',name:'Chile'},
    {code:'CO',name:'Colombia'},{code:'TH',name:'Thailand'},
    {code:'TR',name:'Turkey'},{code:'EG',name:'Egypt'},
    {code:'PK',name:'Pakistan'},{code:'BD',name:'Bangladesh'},
    {code:'GE',name:'Georgia'},{code:'CY',name:'Cyprus'},
    {code:'BA',name:'Bosnia'},{code:'AL',name:'Albania'},
    {code:'MK',name:'North Macedonia'},{code:'ME',name:'Montenegro'},
    {code:'KZ',name:'Kazakhstan'},{code:'AM',name:'Armenia'},
    {code:'VN',name:'Vietnam'},{code:'PH',name:'Philippines'},
    {code:'MY',name:'Malaysia'},{code:'ID',name:'Indonesia'}
  ],

  init: function() {
    var page = document.getElementById('page-exclude');
    if (!page) return;

    page.innerHTML =
      '<h2>Relay Exclusion</h2>' +
      '<p style="color:var(--text-secondary);margin-top:4px;font-size:13px">' +
        'Block relays from being used in your Tor circuits by country or ' +
        'individual fingerprint.</p>' +

      '<div class="grid grid-2" style="margin-top:16px">' +

        /* Left column: Current exclusions + controls */
        '<div>' +
          /* Active exclusions card */
          '<div class="card">' +
            '<div style="display:flex;justify-content:space-between;align-items:center">' +
              '<div class="card-label">Excluded Nodes (ExcludeNodes)</div>' +
              '<div style="display:flex;gap:6px">' +
                '<button class="btn btn-secondary btn-sm" ' +
                  'onclick="RelayExclude.showAddManual()">+ Add Manual</button>' +
                '<button class="btn btn-primary btn-sm" ' +
                  'onclick="RelayExclude.apply()">Apply</button>' +
              '</div>' +
            '</div>' +
            '<div id="exclude-nodes-list" style="margin-top:12px"></div>' +
          '</div>' +

          /* ExcludeExitNodes card */
          '<div class="card" style="margin-top:16px">' +
            '<div style="display:flex;justify-content:space-between;align-items:center">' +
              '<div class="card-label">Excluded Exit Nodes (ExcludeExitNodes)</div>' +
              '<div style="display:flex;gap:6px">' +
                '<button class="btn btn-secondary btn-sm" ' +
                  'onclick="RelayExclude.showAddManual(\'exit\')">+ Add</button>' +
              '</div>' +
            '</div>' +
            '<div id="exclude-exit-list" style="margin-top:12px"></div>' +
          '</div>' +

          /* StrictNodes toggle */
          '<div class="card" style="margin-top:16px">' +
            '<div class="card-label">Enforcement</div>' +
            '<label style="display:flex;align-items:center;gap:10px;margin-top:10px;' +
              'cursor:pointer;font-size:13px">' +
              '<input type="checkbox" id="exclude-strict" ' +
                'onchange="RelayExclude._strictChanged()">' +
              '<div>' +
                '<div style="color:var(--text-primary)">Strict Nodes</div>' +
                '<div style="color:var(--text-muted);font-size:12px;margin-top:2px">' +
                  'When enabled, Tor will never use excluded relays even if ' +
                  'it breaks functionality. When disabled, Tor avoids them ' +
                  'but may use them if necessary.</div>' +
              '</div>' +
            '</label>' +
          '</div>' +

          /* Active relays card */
          '<div class="card" style="margin-top:16px">' +
            '<div class="card-label">Relays in Active Circuits</div>' +
            '<div id="exclude-active-relays" style="margin-top:8px;' +
              'max-height:300px;overflow-y:auto"></div>' +
          '</div>' +
        '</div>' +

        /* Right column: Country picker */
        '<div>' +
          '<div class="card">' +
            '<div style="display:flex;justify-content:space-between;align-items:center">' +
              '<div class="card-label">Exclude by Country</div>' +
              '<div style="display:flex;gap:6px">' +
                '<button class="btn btn-secondary btn-sm" ' +
                  'onclick="RelayExclude.selectAllCountries()">Select All</button>' +
                '<button class="btn btn-secondary btn-sm" ' +
                  'onclick="RelayExclude.clearAllCountries()">Clear All</button>' +
              '</div>' +
            '</div>' +
            '<input class="input" type="text" id="country-search" ' +
              'placeholder="Filter countries..." ' +
              'oninput="RelayExclude._filterCountries()" ' +
              'style="margin-top:10px">' +
            '<div style="display:flex;gap:8px;margin-top:8px">' +
              '<button class="btn btn-primary btn-sm" ' +
                'onclick="RelayExclude.addSelectedCountries()">Add to ExcludeNodes</button>' +
              '<button class="btn btn-secondary btn-sm" ' +
                'onclick="RelayExclude.addSelectedCountries(\'exit\')">Add to Exit Only</button>' +
            '</div>' +
            '<div id="country-picker" style="margin-top:10px;max-height:500px;' +
              'overflow-y:auto"></div>' +
          '</div>' +
        '</div>' +

      '</div>';

    this._buildCountryPicker();
    this.refresh();
  },

  refresh: async function() {
    /* Fetch current ExcludeNodes, ExcludeExitNodes, StrictNodes */
    try {
      var r1 = await TorAPI.getConfig('ExcludeNodes');
      this._excludeNodes = (r1 && r1.value !== undefined) ? r1.value :
                           (typeof r1 === 'string' ? r1 : '');
    } catch (e) { this._excludeNodes = ''; }

    try {
      var r2 = await TorAPI.getConfig('ExcludeExitNodes');
      this._excludeExitNodes = (r2 && r2.value !== undefined) ? r2.value :
                                (typeof r2 === 'string' ? r2 : '');
    } catch (e) { this._excludeExitNodes = ''; }

    try {
      var r3 = await TorAPI.getConfig('StrictNodes');
      var sv = (r3 && r3.value !== undefined) ? r3.value :
               (typeof r3 === 'string' ? r3 : '0');
      this._strictNodes = (sv === '1' || sv === 'true');
    } catch (e) { this._strictNodes = false; }

    var strictEl = document.getElementById('exclude-strict');
    if (strictEl) strictEl.checked = this._strictNodes;

    this._renderExcludeList('exclude-nodes-list', this._excludeNodes);
    this._renderExcludeList('exclude-exit-list', this._excludeExitNodes);
    this._syncCountryChecks();
    this._loadActiveRelays();
  },

  /* Parse exclusion string into entries */
  _parseEntries: function(str) {
    if (!str || !str.trim()) return [];
    var parts = str.split(',');
    var entries = [];
    for (var i = 0; i < parts.length; i++) {
      var p = parts[i].trim();
      if (!p) continue;
      var entry = {raw: p};
      if (p.charAt(0) === '{' && p.charAt(p.length - 1) === '}') {
        entry.type = 'country';
        entry.code = p.substring(1, p.length - 1).toUpperCase();
        var name = this._countryName(entry.code);
        entry.label = name ? name + ' (' + entry.code + ')' : entry.code;
      } else if (p.charAt(0) === '$' || /^[0-9a-fA-F]{40}$/.test(p)) {
        entry.type = 'relay';
        entry.fingerprint = p.replace(/^\$/, '');
        entry.label = entry.fingerprint.substring(0, 16) + '...';
      } else if (p.indexOf('/') !== -1 || /^\d+\.\d+\.\d+\.\d+$/.test(p)) {
        entry.type = 'ip';
        entry.label = p;
      } else {
        entry.type = 'nickname';
        entry.label = p;
      }
      entries.push(entry);
    }
    return entries;
  },

  _countryName: function(code) {
    for (var i = 0; i < this._countries.length; i++) {
      if (this._countries[i].code === code) return this._countries[i].name;
    }
    return '';
  },

  _renderExcludeList: function(containerId, value) {
    var el = document.getElementById(containerId);
    if (!el) return;
    var entries = this._parseEntries(value);

    if (entries.length === 0) {
      el.innerHTML = '<div style="color:var(--text-muted);font-size:13px;' +
        'padding:8px 0">No exclusions configured</div>';
      return;
    }

    var html = '';
    for (var i = 0; i < entries.length; i++) {
      var e = entries[i];
      var icon = '';
      var badgeClass = 'badge-info';
      if (e.type === 'country') {
        icon = '<span style="font-size:14px;margin-right:4px">' +
               this._countryFlag(e.code) + '</span>';
        badgeClass = 'badge-warning';
      } else if (e.type === 'relay') {
        badgeClass = 'badge-danger';
      } else if (e.type === 'ip') {
        badgeClass = 'badge-info';
      }

      html += '<div style="display:flex;align-items:center;justify-content:space-between;' +
        'padding:6px 8px;border-radius:4px;margin-bottom:4px;' +
        'background:var(--bg-secondary)">' +
        '<div style="display:flex;align-items:center;gap:6px;min-width:0">' +
          icon +
          '<span class="badge ' + badgeClass + '" style="font-size:10px;flex-shrink:0">' +
            this._esc(e.type) + '</span>' +
          '<span style="font-size:13px;overflow:hidden;text-overflow:ellipsis;' +
            'white-space:nowrap" title="' + this._escAttr(e.raw) + '">' +
            this._esc(e.label) + '</span>' +
        '</div>' +
        '<button class="btn btn-danger btn-sm" style="padding:2px 8px;font-size:11px;' +
          'flex-shrink:0" onclick="RelayExclude.removeEntry(\'' +
          this._escAttr(containerId) + '\',\'' +
          this._escAttr(e.raw) + '\')">Remove</button>' +
      '</div>';
    }
    el.innerHTML = html;
  },

  /* Remove a single entry */
  removeEntry: function(containerId, rawValue) {
    var isExit = (containerId === 'exclude-exit-list');
    var current = isExit ? this._excludeExitNodes : this._excludeNodes;
    var entries = this._parseEntries(current);
    var remaining = [];
    for (var i = 0; i < entries.length; i++) {
      if (entries[i].raw !== rawValue) remaining.push(entries[i].raw);
    }
    var newVal = remaining.join(',');
    if (isExit) {
      this._excludeExitNodes = newVal;
    } else {
      this._excludeNodes = newVal;
    }
    this._renderExcludeList(containerId, newVal);
    this._syncCountryChecks();
  },

  /* Build country picker checkbox list */
  _buildCountryPicker: function() {
    var container = document.getElementById('country-picker');
    if (!container) return;

    /* Sort countries alphabetically */
    var sorted = this._countries.slice().sort(function(a, b) {
      return a.name.localeCompare(b.name);
    });

    var html = '';
    for (var i = 0; i < sorted.length; i++) {
      var c = sorted[i];
      html += '<label class="country-row" data-code="' + c.code + '" ' +
        'data-name="' + this._escAttr(c.name.toLowerCase()) + '" ' +
        'style="display:flex;align-items:center;gap:8px;padding:5px 6px;' +
        'border-radius:4px;cursor:pointer;font-size:13px;' +
        'transition:background 0.15s" ' +
        'onmouseenter="this.style.background=\'var(--bg-tertiary)\'" ' +
        'onmouseleave="this.style.background=\'transparent\'">' +
        '<input type="checkbox" class="country-check" value="' + c.code + '">' +
        '<span style="width:22px;text-align:center">' +
          this._countryFlag(c.code) + '</span>' +
        '<span style="flex:1;color:var(--text-primary)">' +
          this._esc(c.name) + '</span>' +
        '<span style="color:var(--text-muted);font-size:11px;font-family:monospace">' +
          c.code + '</span>' +
      '</label>';
    }
    container.innerHTML = html;
  },

  _filterCountries: function() {
    var query = (document.getElementById('country-search').value || '')
                .toLowerCase().trim();
    var rows = document.querySelectorAll('.country-row');
    for (var i = 0; i < rows.length; i++) {
      var name = rows[i].getAttribute('data-name') || '';
      var code = (rows[i].getAttribute('data-code') || '').toLowerCase();
      var match = !query || name.indexOf(query) !== -1 || code.indexOf(query) !== -1;
      rows[i].style.display = match ? '' : 'none';
    }
  },

  /* Sync country checkboxes with current ExcludeNodes value */
  _syncCountryChecks: function() {
    var entries = this._parseEntries(this._excludeNodes);
    var excludedCountries = {};
    for (var i = 0; i < entries.length; i++) {
      if (entries[i].type === 'country') {
        excludedCountries[entries[i].code] = true;
      }
    }
    var checks = document.querySelectorAll('.country-check');
    for (var j = 0; j < checks.length; j++) {
      checks[j].checked = !!excludedCountries[checks[j].value];
    }
  },

  addSelectedCountries: function(target) {
    var checks = document.querySelectorAll('.country-check:checked');
    if (checks.length === 0) {
      if (typeof App !== 'undefined') App.toast('No countries selected.', 'warning');
      return;
    }

    var isExit = (target === 'exit');
    var current = isExit ? this._excludeExitNodes : this._excludeNodes;
    var entries = this._parseEntries(current);
    var existing = {};
    for (var i = 0; i < entries.length; i++) {
      existing[entries[i].raw] = true;
    }

    var added = 0;
    for (var j = 0; j < checks.length; j++) {
      var code = '{' + checks[j].value + '}';
      if (!existing[code]) {
        entries.push({raw: code});
        added++;
      }
    }

    if (added === 0) {
      if (typeof App !== 'undefined')
        App.toast('Selected countries are already excluded.', 'info');
      return;
    }

    var newVal = entries.map(function(e) { return e.raw; }).join(',');
    if (isExit) {
      this._excludeExitNodes = newVal;
      this._renderExcludeList('exclude-exit-list', newVal);
    } else {
      this._excludeNodes = newVal;
      this._renderExcludeList('exclude-nodes-list', newVal);
    }

    if (typeof App !== 'undefined')
      App.toast('Added ' + added + ' countr' + (added === 1 ? 'y' : 'ies') +
                ' to exclusion list.', 'success');
  },

  selectAllCountries: function() {
    var checks = document.querySelectorAll('.country-check');
    for (var i = 0; i < checks.length; i++) {
      var row = checks[i].closest('.country-row');
      if (row && row.style.display !== 'none') checks[i].checked = true;
    }
  },

  clearAllCountries: function() {
    var checks = document.querySelectorAll('.country-check');
    for (var i = 0; i < checks.length; i++) checks[i].checked = false;
  },

  /* Show manual add dialog */
  showAddManual: function(target) {
    var isExit = (target === 'exit');
    var label = isExit ? 'ExcludeExitNodes' : 'ExcludeNodes';

    var overlay = document.createElement('div');
    overlay.className = 'modal-overlay';
    overlay.onclick = function(e) {
      if (e.target === overlay) document.body.removeChild(overlay);
    };

    overlay.innerHTML =
      '<div class="modal" style="max-width:480px">' +
        '<div style="display:flex;justify-content:space-between;align-items:center">' +
          '<h3>Add to ' + label + '</h3>' +
          '<button class="btn btn-secondary btn-sm modal-close" ' +
            'onclick="this.closest(\'.modal-overlay\').remove()">&times;</button>' +
        '</div>' +
        '<div style="margin-top:16px">' +
          '<label style="font-size:13px;color:var(--text-secondary)">Relay Fingerprint, ' +
            'Nickname, Country Code, or IP Range</label>' +
          '<input class="input" type="text" id="manual-exclude-input" ' +
            'placeholder="e.g. $ABC123..., {ru}, RelayNick, 10.0.0.0/8" ' +
            'style="margin-top:6px;width:100%">' +
          '<div style="color:var(--text-muted);font-size:12px;margin-top:6px">' +
            'Formats: <code style="color:var(--accent-blue)">{CC}</code> for country, ' +
            '<code style="color:var(--accent-blue)">$fingerprint</code> for relay, ' +
            '<code style="color:var(--accent-blue)">IP/mask</code> for range. ' +
            'Separate multiple with commas.' +
          '</div>' +
        '</div>' +
        '<div class="modal-footer" style="margin-top:16px;display:flex;gap:8px;' +
          'justify-content:flex-end">' +
          '<button class="btn btn-secondary" ' +
            'onclick="this.closest(\'.modal-overlay\').remove()">Cancel</button>' +
          '<button class="btn btn-primary" id="manual-exclude-add">Add</button>' +
        '</div>' +
      '</div>';

    document.body.appendChild(overlay);

    var input = document.getElementById('manual-exclude-input');
    input.focus();

    var self = this;
    document.getElementById('manual-exclude-add').onclick = function() {
      var val = input.value.trim();
      if (!val) return;
      self._addEntries(val, isExit);
      overlay.remove();
    };

    input.addEventListener('keydown', function(e) {
      if (e.key === 'Enter') {
        document.getElementById('manual-exclude-add').click();
      }
    });
  },

  /* Add entries (comma-separated string) to the appropriate list */
  _addEntries: function(newEntries, isExit) {
    var current = isExit ? this._excludeExitNodes : this._excludeNodes;
    var entries = this._parseEntries(current);
    var existing = {};
    for (var i = 0; i < entries.length; i++) existing[entries[i].raw] = true;

    var parts = newEntries.split(',');
    var added = 0;
    for (var j = 0; j < parts.length; j++) {
      var p = parts[j].trim();
      if (!p || existing[p]) continue;
      entries.push({raw: p});
      existing[p] = true;
      added++;
    }

    if (added === 0) {
      if (typeof App !== 'undefined')
        App.toast('Entries already in exclusion list.', 'info');
      return;
    }

    var newVal = entries.map(function(e) { return e.raw; }).join(',');
    if (isExit) {
      this._excludeExitNodes = newVal;
      this._renderExcludeList('exclude-exit-list', newVal);
    } else {
      this._excludeNodes = newVal;
      this._renderExcludeList('exclude-nodes-list', newVal);
      this._syncCountryChecks();
    }

    if (typeof App !== 'undefined')
      App.toast('Added ' + added + ' entr' + (added === 1 ? 'y' : 'ies') + '.', 'success');
  },

  /* Apply changes to Tor */
  apply: async function() {
    var errors = [];

    try {
      await TorAPI.setConfig('ExcludeNodes', this._excludeNodes || '');
    } catch (e) {
      errors.push('ExcludeNodes: ' + e.message);
    }

    try {
      await TorAPI.setConfig('ExcludeExitNodes', this._excludeExitNodes || '');
    } catch (e) {
      errors.push('ExcludeExitNodes: ' + e.message);
    }

    try {
      var sv = this._strictNodes ? '1' : '0';
      await TorAPI.setConfig('StrictNodes', sv);
    } catch (e) {
      errors.push('StrictNodes: ' + e.message);
    }

    try {
      await TorAPI.saveConfig();
    } catch (e) {
      errors.push('Save: ' + e.message);
    }

    if (errors.length > 0) {
      if (typeof App !== 'undefined')
        App.toast('Errors: ' + errors.join('; '), 'error');
    } else {
      if (typeof App !== 'undefined')
        App.toast('Exclusion rules applied and saved.', 'success');
    }

    /* Refresh to confirm actual values */
    this.refresh();
  },

  _strictChanged: function() {
    var el = document.getElementById('exclude-strict');
    this._strictNodes = el ? el.checked : false;
  },

  /* Load relays from active circuits for the "exclude from circuit" list */
  _loadActiveRelays: async function() {
    var container = document.getElementById('exclude-active-relays');
    if (!container) return;

    try {
      var data = await TorAPI.getCircuits();
      var circuits = Array.isArray(data) ? data :
                     (data && data.circuits ? data.circuits : []);

      var relays = {};
      for (var i = 0; i < circuits.length; i++) {
        var c = circuits[i];
        if ((c.status || '').toUpperCase() !== 'BUILT') continue;
        var path = c.path || [];
        for (var j = 0; j < path.length; j++) {
          var r = this._parseRelay(path[j]);
          var key = r.fingerprint || r.nickname || r.raw;
          if (!key) continue;
          if (!relays[key]) {
            r._role = j === 0 ? 'Guard' : (j === path.length - 1 ? 'Exit' : 'Middle');
            relays[key] = r;
          }
        }
      }

      var keys = Object.keys(relays);
      if (keys.length === 0) {
        container.innerHTML = '<div style="color:var(--text-muted);font-size:13px;' +
          'padding:4px 0">No active relays</div>';
        return;
      }

      /* Check which are already excluded */
      var excludedSet = {};
      var entries = this._parseEntries(this._excludeNodes);
      for (var ei = 0; ei < entries.length; ei++) {
        if (entries[ei].type === 'relay') {
          excludedSet[entries[ei].fingerprint.toUpperCase()] = true;
        }
      }

      var html = '';
      keys.sort(function(a, b) {
        var ra = relays[a], rb = relays[b];
        var order = {Guard: 0, Middle: 1, Exit: 2};
        return (order[ra._role] || 1) - (order[rb._role] || 1);
      });

      for (var k = 0; k < keys.length; k++) {
        var relay = relays[keys[k]];
        var fp = relay.fingerprint || '';
        var isExcluded = excludedSet[fp.toUpperCase()];
        var roleColor = relay._role === 'Guard' ? '#3fb950' :
                        (relay._role === 'Exit' ? '#f85149' : '#58a6ff');

        html += '<div style="display:flex;align-items:center;gap:6px;padding:5px 4px;' +
          'font-size:12px;border-bottom:1px solid var(--border-color)">' +
          '<span style="width:7px;height:7px;border-radius:50%;background:' +
            roleColor + ';flex-shrink:0"></span>' +
          '<span style="width:36px;color:var(--text-muted);font-size:10px">' +
            this._esc(relay._role) + '</span>' +
          '<span style="flex:1;overflow:hidden;text-overflow:ellipsis;' +
            'white-space:nowrap;color:var(--text-primary)" ' +
            'title="' + this._escAttr(fp) + '">' +
            this._esc(relay.nickname || fp.substring(0, 12)) + '</span>' +
          '<span style="color:var(--text-muted);font-size:10px">' +
            this._esc(relay.country || '??') + '</span>';

        if (isExcluded) {
          html += '<span class="badge badge-danger" style="font-size:9px">Excluded</span>';
        } else {
          html += '<button class="btn btn-danger btn-sm" ' +
            'style="padding:1px 6px;font-size:10px" ' +
            'onclick="RelayExclude.excludeRelay(\'' +
            this._escAttr(fp) + '\')">Exclude</button>';
        }
        html += '</div>';
      }
      container.innerHTML = html;

    } catch (e) {
      container.innerHTML = '<div style="color:var(--text-muted);font-size:13px">' +
        'Failed to load circuits</div>';
    }
  },

  /* Parse relay from circuit path entry */
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
    var info = {fingerprint: '', nickname: '', ip: '', country: '', raw: String(relay)};
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

  /* Exclude a single relay by fingerprint */
  excludeRelay: function(fingerprint) {
    if (!fingerprint) return;
    var entry = '$' + fingerprint.replace(/^\$/, '');
    this._addEntries(entry, false);
    this._loadActiveRelays();
  },

  /* Exclude a relay by country */
  excludeCountry: function(code) {
    if (!code) return;
    this._addEntries('{' + code.toUpperCase() + '}', false);
    this._syncCountryChecks();
  },

  /* Public API for other modules to call */
  excludeRelayFromCircuit: function(fingerprint, nickname) {
    if (!fingerprint) return;
    this.excludeRelay(fingerprint);
    if (typeof App !== 'undefined')
      App.toast('Added ' + (nickname || fingerprint.substring(0, 8)) +
                ' to exclusion list. Click Apply on the Relay Exclusion page to save.',
                'info');
  },

  /* Emoji flag from country code */
  _countryFlag: function(code) {
    if (!code || code.length < 2) return '';
    try {
      var c1 = code.charCodeAt(0) - 65 + 0x1F1E6;
      var c2 = code.charCodeAt(1) - 65 + 0x1F1E6;
      return String.fromCodePoint(c1) + String.fromCodePoint(c2);
    } catch (e) {
      return code;
    }
  },

  _esc: function(str) {
    return String(str).replace(/&/g,'&amp;').replace(/</g,'&lt;')
      .replace(/>/g,'&gt;').replace(/"/g,'&quot;');
  },
  _escAttr: function(str) {
    return String(str).replace(/&/g,'&amp;').replace(/"/g,'&quot;')
      .replace(/'/g,'&#39;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
  }
};
