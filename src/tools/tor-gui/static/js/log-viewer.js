/* log-viewer.js - Real-time log stream viewer */

var LogViewer = {
  _logs: [],
  _maxLogs: 1000,
  _autoScroll: true,
  _filter: '',
  _level: 'all',
  _paused: false,

  init: function() {
    var page = document.getElementById('page-logs');
    page.innerHTML =
      '<h2>Log Viewer</h2>' +
      '<div class="toolbar" style="margin-top:16px">' +
        '<input class="input" id="log-filter" style="width:300px" ' +
          'placeholder="Filter logs..." oninput="LogViewer.setFilter(this.value)">' +
        '<select class="select" id="log-level" style="width:150px" ' +
          'onchange="LogViewer.setLevel(this.value)">' +
          '<option value="all">All Levels</option>' +
          '<option value="err">Error</option>' +
          '<option value="warn">Warning</option>' +
          '<option value="notice">Notice</option>' +
          '<option value="info">Info</option>' +
          '<option value="debug">Debug</option>' +
        '</select>' +
        '<label style="display:flex;align-items:center;gap:4px;font-size:13px">' +
          '<input type="checkbox" id="log-autoscroll" checked ' +
            'onchange="LogViewer.setAutoScroll(this.checked)"> Auto-scroll' +
        '</label>' +
        '<button class="btn btn-secondary" id="log-pause-btn" ' +
          'onclick="LogViewer.togglePause()">Pause</button>' +
        '<button class="btn btn-secondary" onclick="LogViewer.clear()">Clear</button>' +
        '<button class="btn btn-secondary" onclick="LogViewer.download()">Download</button>' +
        '<span id="log-count" style="font-size:12px;color:var(--text-muted)"></span>' +
      '</div>' +
      '<div class="card" style="margin-top:16px;padding:0">' +
        '<div id="log-container" style="height:600px;overflow-y:auto;' +
          'font-family:monospace;font-size:12px;padding:12px"></div>' +
      '</div>';

    /* Subscribe to all WS events for logging */
    TorWS.on('*', function(msg) { LogViewer._onEvent(msg); });

    /* Fetch initial log history */
    this._fetchInitialLogs();
  },

  _fetchInitialLogs: async function() {
    try {
      var result = await TorAPI.getLogs(null, 200);
      var logs = Array.isArray(result) ? result :
        (result && result.logs ? result.logs : []);
      for (var i = 0; i < logs.length; i++) {
        this._addLogEntry(logs[i], false);
      }
      this._render();
    } catch (e) {
      /* No initial logs available; that is fine */
    }
  },

  _onEvent: function(msg) {
    if (this._paused) return;
    if (!msg) return;

    var entry;
    if (msg.type === 'log') {
      entry = {
        timestamp: msg.timestamp || new Date().toISOString(),
        level: (msg.level || msg.severity || 'info').toLowerCase(),
        message: msg.message || msg.text || '',
        source: 'tor'
      };
    } else {
      /* Format non-log events as informational entries */
      var text = msg.type || 'event';
      if (msg.message) text += ': ' + msg.message;
      else if (msg.data) text += ': ' + (typeof msg.data === 'string' ? msg.data : JSON.stringify(msg.data));
      entry = {
        timestamp: msg.timestamp || new Date().toISOString(),
        level: 'info',
        message: text,
        source: 'ws'
      };
    }

    this._addLogEntry(entry, true);
  },

  _addLogEntry: function(entry, renderImmediately) {
    /* Normalize entry from various formats */
    if (typeof entry === 'string') {
      var match = entry.match(/^(\S+ \S+ [\d:.]+)\s+\[(\w+)\]\s+(.*)$/);
      if (match) {
        entry = {
          timestamp: match[1],
          level: match[2].toLowerCase(),
          message: match[3]
        };
      } else {
        entry = {
          timestamp: new Date().toISOString(),
          level: 'info',
          message: entry
        };
      }
    }

    if (!entry.timestamp) entry.timestamp = new Date().toISOString();
    if (!entry.level) entry.level = 'info';
    if (!entry.message) entry.message = '';
    entry.level = entry.level.toLowerCase();

    this._logs.push(entry);

    /* Enforce max log limit */
    while (this._logs.length > this._maxLogs) {
      this._logs.shift();
    }

    if (renderImmediately) {
      this._appendSingle(entry);
    }
  },

  _render: function() {
    var container = document.getElementById('log-container');
    if (!container) return;

    var filtered = this._getFilteredLogs();
    var html = '';
    for (var i = 0; i < filtered.length; i++) {
      html += this._formatLogLine(filtered[i]);
    }
    container.innerHTML = html;
    this._updateCount(filtered.length);

    if (this._autoScroll) {
      container.scrollTop = container.scrollHeight;
    }
  },

  _appendSingle: function(entry) {
    if (!this._matchesFilter(entry)) return;

    var container = document.getElementById('log-container');
    if (!container) return;

    /* Append directly for performance */
    var div = document.createElement('div');
    div.innerHTML = this._formatLogLine(entry);
    if (div.firstChild) {
      container.appendChild(div.firstChild);
    }

    /* Update count */
    this._updateCount(null);

    if (this._autoScroll) {
      container.scrollTop = container.scrollHeight;
    }
  },

  _formatLogLine: function(entry) {
    var level = entry.level || 'info';
    var color = 'var(--text-primary)';

    if (level === 'err' || level === 'error') {
      color = 'var(--accent-red)';
    } else if (level === 'warn' || level === 'warning') {
      color = 'var(--accent-yellow)';
    } else if (level === 'notice') {
      color = 'var(--accent-green)';
    } else if (level === 'debug') {
      color = 'var(--text-muted)';
    }

    var ts = entry.timestamp || '';
    /* Truncate to HH:MM:SS if it is a full ISO timestamp */
    if (ts.length > 19 && ts.indexOf('T') !== -1) {
      ts = ts.substring(11, 19);
    } else if (ts.length > 19) {
      ts = ts.substring(0, 19);
    }

    var levelDisplay = level.toUpperCase();
    if (levelDisplay.length > 6) levelDisplay = levelDisplay.substring(0, 6);

    return '<div style="padding:2px 0;border-bottom:1px solid rgba(48,54,61,0.2);' +
      'white-space:pre-wrap;word-break:break-word">' +
      '<span style="color:var(--text-muted)">' + this._esc(ts) + '</span> ' +
      '<span style="color:' + color + ';font-weight:600;display:inline-block;' +
        'min-width:56px">[' + this._esc(levelDisplay) + ']</span> ' +
      '<span>' + this._esc(entry.message || '') + '</span>' +
    '</div>';
  },

  _getFilteredLogs: function() {
    var result = [];
    for (var i = 0; i < this._logs.length; i++) {
      if (this._matchesFilter(this._logs[i])) {
        result.push(this._logs[i]);
      }
    }
    return result;
  },

  _matchesFilter: function(entry) {
    /* Level filter */
    if (this._level !== 'all') {
      var entryLevel = (entry.level || '').toLowerCase();
      var filterLevel = this._level.toLowerCase();

      /* Normalize synonyms */
      var normalizedEntry = entryLevel;
      if (normalizedEntry === 'error') normalizedEntry = 'err';
      if (normalizedEntry === 'warning') normalizedEntry = 'warn';

      if (normalizedEntry !== filterLevel) return false;
    }

    /* Text filter */
    if (this._filter) {
      var searchText = (
        (entry.message || '') + ' ' +
        (entry.timestamp || '') + ' ' +
        (entry.level || '')
      ).toLowerCase();
      if (searchText.indexOf(this._filter) === -1) return false;
    }

    return true;
  },

  _updateCount: function(filteredCount) {
    var countEl = document.getElementById('log-count');
    if (!countEl) return;
    if (filteredCount === null || filteredCount === undefined) {
      filteredCount = this._getFilteredLogs().length;
    }
    countEl.textContent = filteredCount + ' / ' + this._logs.length + ' entries';
  },

  setFilter: function(f) {
    this._filter = f.toLowerCase();
    this._render();
  },

  setLevel: function(l) {
    this._level = l;
    this._render();
  },

  setAutoScroll: function(v) {
    this._autoScroll = v;
    if (v) {
      var container = document.getElementById('log-container');
      if (container) container.scrollTop = container.scrollHeight;
    }
  },

  togglePause: function() {
    this._paused = !this._paused;
    var btn = document.getElementById('log-pause-btn');
    if (btn) {
      btn.textContent = this._paused ? 'Resume' : 'Pause';
    }
  },

  clear: function() {
    this._logs = [];
    var container = document.getElementById('log-container');
    if (container) container.innerHTML = '';
    this._updateCount(0);
  },

  download: function() {
    var lines = [];
    for (var i = 0; i < this._logs.length; i++) {
      var e = this._logs[i];
      lines.push(
        (e.timestamp || '') + ' [' +
        (e.level || 'info').toUpperCase() + '] ' +
        (e.message || '')
      );
    }
    var text = lines.join('\n');
    var blob = new Blob([text], { type: 'text/plain' });
    var url = URL.createObjectURL(blob);
    var a = document.createElement('a');
    a.href = url;
    a.download = 'tor-logs-' + new Date().toISOString().slice(0, 10) + '.txt';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
    if (typeof App !== 'undefined' && App.toast) {
      App.toast('Logs downloaded.', 'success');
    }
  },

  _esc: function(str) {
    var d = document.createElement('div');
    d.appendChild(document.createTextNode(str));
    return d.innerHTML;
  }
};
