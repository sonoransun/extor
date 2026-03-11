/* dashboard.js - Dashboard page with status, bandwidth chart, and quick actions */

var Dashboard = {
  _bwCanvas: null,
  _bwCtx: null,
  _bwData: { read: [], written: [] },
  _maxBwPoints: 300,
  _statusPollTimer: null,
  _animFrame: null,

  init: function() {
    var page = document.getElementById('page-dashboard');
    page.innerHTML =
      '<h2>Dashboard</h2>' +
      '<div class="grid grid-3" style="margin-top:16px">' +
        '<div class="card" id="dash-status-card">' +
          '<div class="card-label">Status</div>' +
          '<div class="card-value" id="dash-status">--</div>' +
        '</div>' +
        '<div class="card" id="dash-circuits-card">' +
          '<div class="card-label">Active Circuits</div>' +
          '<div class="card-value" id="dash-circuits">--</div>' +
        '</div>' +
        '<div class="card" id="dash-uptime-card">' +
          '<div class="card-label">Uptime</div>' +
          '<div class="card-value" id="dash-uptime">--</div>' +
        '</div>' +
      '</div>' +
      '<div class="card" style="margin-top:16px">' +
        '<div class="card-label">Bootstrap Progress</div>' +
        '<div class="progress" style="margin-top:8px">' +
          '<div class="progress-bar" id="dash-bootstrap-bar" style="width:0%"></div>' +
        '</div>' +
        '<div id="dash-bootstrap-text" style="margin-top:4px;font-size:13px;color:var(--text-secondary)">Waiting...</div>' +
      '</div>' +
      '<div class="card" style="margin-top:16px">' +
        '<div style="display:flex;justify-content:space-between;align-items:center">' +
          '<div class="card-label">Bandwidth</div>' +
          '<div id="dash-bw-rate" style="font-size:13px;color:var(--text-secondary)"></div>' +
        '</div>' +
        '<div class="chart-container" style="margin-top:12px">' +
          '<canvas id="dash-bw-canvas"></canvas>' +
        '</div>' +
        '<div style="display:flex;gap:16px;margin-top:8px;font-size:12px;color:var(--text-secondary)">' +
          '<span style="display:flex;align-items:center;gap:4px">' +
            '<span style="width:12px;height:3px;background:#3fb950;display:inline-block;border-radius:2px"></span> Download' +
          '</span>' +
          '<span style="display:flex;align-items:center;gap:4px">' +
            '<span style="width:12px;height:3px;background:#a855f7;display:inline-block;border-radius:2px"></span> Upload' +
          '</span>' +
        '</div>' +
      '</div>' +
      '<div class="card" style="margin-top:16px">' +
        '<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:12px">' +
          '<div class="card-label">Quick Actions</div>' +
        '</div>' +
        '<div style="display:flex;gap:8px;flex-wrap:wrap">' +
          '<button class="btn btn-primary" onclick="Dashboard.newIdentity()">New Identity</button>' +
          '<button class="btn btn-secondary" onclick="Dashboard.reloadConfig()">Reload Config</button>' +
          '<button class="btn btn-secondary" onclick="Dashboard.clearDns()">Clear DNS Cache</button>' +
        '</div>' +
      '</div>';

    this._bwCanvas = document.getElementById('dash-bw-canvas');
    this._bwCtx = this._bwCanvas.getContext('2d');
    this._resizeCanvas();

    var self = this;
    window.addEventListener('resize', function() { self._resizeCanvas(); });

    TorWS.on('bandwidth', function(d) { self._onBandwidth(d); });
    TorWS.on('bootstrap', function(d) { self._onBootstrap(d); });
    TorWS.on('circuit', function() { self._updateCircuitCount(); });

    this.refresh();
    this._statusPollTimer = setInterval(function() { self.refresh(); }, 5000);
  },

  refresh: async function() {
    try {
      var status = await TorAPI.getStatus();
      var statusEl = document.getElementById('dash-status');
      if (status.bootstrap_progress >= 100) {
        statusEl.textContent = 'Connected';
        statusEl.style.color = 'var(--accent-green)';
      } else {
        statusEl.textContent = 'Bootstrapping...';
        statusEl.style.color = 'var(--accent-yellow)';
      }
      document.getElementById('dash-bootstrap-bar').style.width =
        status.bootstrap_progress + '%';
      document.getElementById('dash-bootstrap-text').textContent =
        status.bootstrap_phase || ('Progress: ' + status.bootstrap_progress + '%');
      if (status.version) {
        document.getElementById('tor-version').textContent = 'v' + status.version;
      }
      if (status.uptime !== undefined) {
        document.getElementById('dash-uptime').textContent =
          this._formatUptime(parseInt(status.uptime, 10));
      }
    } catch (e) {
      console.error('Status fetch error:', e);
      document.getElementById('dash-status').textContent = 'Offline';
      document.getElementById('dash-status').style.color = 'var(--accent-red)';
    }

    this._updateCircuitCount();
  },

  _updateCircuitCount: async function() {
    try {
      var circuits = await TorAPI.getCircuits();
      document.getElementById('dash-circuits').textContent =
        Array.isArray(circuits) ? circuits.length.toString() : '0';
    } catch (e) {
      /* ignore */
    }
  },

  _onBandwidth: function(data) {
    var read = data.read || data.bytes_read || 0;
    var written = data.written || data.bytes_written || 0;

    this._bwData.read.push(read);
    this._bwData.written.push(written);

    if (this._bwData.read.length > this._maxBwPoints) {
      this._bwData.read.shift();
    }
    if (this._bwData.written.length > this._maxBwPoints) {
      this._bwData.written.shift();
    }

    var rateEl = document.getElementById('dash-bw-rate');
    rateEl.textContent =
      this._formatBytes(read) + '/s down, ' +
      this._formatBytes(written) + '/s up';

    this._drawBandwidth();
  },

  _onBootstrap: function(data) {
    var progress = data.progress || 0;
    var summary = data.summary || data.phase || '';
    document.getElementById('dash-bootstrap-bar').style.width = progress + '%';
    document.getElementById('dash-bootstrap-text').textContent =
      summary || ('Progress: ' + progress + '%');
  },

  _drawBandwidth: function() {
    var canvas = this._bwCanvas;
    var ctx = this._bwCtx;
    var w = canvas.width;
    var h = canvas.height;
    var readArr = this._bwData.read;
    var writtenArr = this._bwData.written;

    ctx.clearRect(0, 0, w, h);

    if (readArr.length < 2) return;

    /* Find max value for Y axis scaling */
    var maxVal = 1;
    var i;
    for (i = 0; i < readArr.length; i++) {
      if (readArr[i] > maxVal) maxVal = readArr[i];
    }
    for (i = 0; i < writtenArr.length; i++) {
      if (writtenArr[i] > maxVal) maxVal = writtenArr[i];
    }
    maxVal = maxVal * 1.15; /* 15% headroom */

    var padLeft = 60;
    var padRight = 10;
    var padTop = 10;
    var padBottom = 20;
    var plotW = w - padLeft - padRight;
    var plotH = h - padTop - padBottom;

    /* Draw grid lines */
    ctx.strokeStyle = 'rgba(48, 54, 61, 0.6)';
    ctx.lineWidth = 1;
    ctx.setLineDash([4, 4]);
    var gridLines = 4;
    for (i = 0; i <= gridLines; i++) {
      var gy = padTop + (plotH / gridLines) * i;
      ctx.beginPath();
      ctx.moveTo(padLeft, gy);
      ctx.lineTo(w - padRight, gy);
      ctx.stroke();

      /* Y axis label */
      var labelVal = maxVal - (maxVal / gridLines) * i;
      ctx.fillStyle = '#8b949e';
      ctx.font = '11px -apple-system, sans-serif';
      ctx.textAlign = 'right';
      ctx.textBaseline = 'middle';
      ctx.fillText(this._formatBytes(labelVal), padLeft - 6, gy);
    }
    ctx.setLineDash([]);

    /* X axis time labels */
    ctx.fillStyle = '#484f58';
    ctx.font = '10px -apple-system, sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    var totalSec = this._maxBwPoints;
    var labelSteps = [0, 0.25, 0.5, 0.75, 1.0];
    for (i = 0; i < labelSteps.length; i++) {
      var lx = padLeft + plotW * labelSteps[i];
      var secAgo = Math.round(totalSec * (1 - labelSteps[i]));
      var label = secAgo === 0 ? 'now' : '-' + this._formatTime(secAgo);
      ctx.fillText(label, lx, h - padBottom + 4);
    }

    /* Helper to draw a data line with area fill */
    var self = this;
    function drawLine(arr, strokeColor, fillColor) {
      if (arr.length < 2) return;
      var pts = arr.length;
      var xStep = plotW / (self._maxBwPoints - 1);
      var xOffset = (self._maxBwPoints - pts) * xStep;

      ctx.beginPath();
      for (var j = 0; j < pts; j++) {
        var x = padLeft + xOffset + j * xStep;
        var y = padTop + plotH - (arr[j] / maxVal) * plotH;
        if (j === 0) {
          ctx.moveTo(x, y);
        } else {
          ctx.lineTo(x, y);
        }
      }

      /* Stroke the line */
      ctx.strokeStyle = strokeColor;
      ctx.lineWidth = 2;
      ctx.stroke();

      /* Fill area beneath */
      var lastX = padLeft + xOffset + (pts - 1) * xStep;
      var firstX = padLeft + xOffset;
      ctx.lineTo(lastX, padTop + plotH);
      ctx.lineTo(firstX, padTop + plotH);
      ctx.closePath();
      ctx.fillStyle = fillColor;
      ctx.fill();
    }

    /* Draw written (purple) first so read (green) is on top */
    drawLine(writtenArr, '#a855f7', 'rgba(168, 85, 247, 0.12)');
    drawLine(readArr, '#3fb950', 'rgba(63, 185, 80, 0.12)');
  },

  _resizeCanvas: function() {
    if (!this._bwCanvas) return;
    var container = this._bwCanvas.parentElement;
    var dpr = window.devicePixelRatio || 1;
    var rect = container.getBoundingClientRect();
    this._bwCanvas.width = rect.width * dpr;
    this._bwCanvas.height = rect.height * dpr;
    this._bwCtx.scale(dpr, dpr);
    /* Reset canvas CSS size */
    this._bwCanvas.style.width = rect.width + 'px';
    this._bwCanvas.style.height = rect.height + 'px';
    /* Store logical dimensions for drawing */
    this._bwCanvas.width = rect.width * dpr;
    this._bwCanvas.height = rect.height * dpr;
    this._bwCtx.setTransform(dpr, 0, 0, dpr, 0, 0);
    this._drawBandwidth();
  },

  _formatUptime: function(seconds) {
    if (isNaN(seconds) || seconds < 0) return '--';
    var days = Math.floor(seconds / 86400);
    var hours = Math.floor((seconds % 86400) / 3600);
    var mins = Math.floor((seconds % 3600) / 60);
    var parts = [];
    if (days > 0) parts.push(days + 'd');
    if (hours > 0 || days > 0) parts.push(hours + 'h');
    parts.push(mins + 'm');
    return parts.join(' ');
  },

  _formatBytes: function(bytes) {
    if (bytes === 0) return '0 B';
    var units = ['B', 'KB', 'MB', 'GB', 'TB'];
    var i = 0;
    var val = bytes;
    while (val >= 1024 && i < units.length - 1) {
      val /= 1024;
      i++;
    }
    return (i === 0 ? val : val.toFixed(1)) + ' ' + units[i];
  },

  _formatTime: function(seconds) {
    if (seconds < 60) return seconds + 's';
    var m = Math.floor(seconds / 60);
    var s = seconds % 60;
    if (s === 0) return m + 'm';
    return m + 'm' + s + 's';
  },

  newIdentity: async function() {
    try {
      await TorAPI.sendSignal('NEWNYM');
      App.toast('New identity requested', 'success');
    } catch (e) {
      App.toast('Failed: ' + e.message, 'error');
    }
  },

  reloadConfig: async function() {
    try {
      await TorAPI.reloadConfig();
      App.toast('Configuration reloaded', 'success');
    } catch (e) {
      App.toast('Reload failed: ' + e.message, 'error');
    }
  },

  clearDns: async function() {
    try {
      await TorAPI.sendSignal('CLEARDNSCACHE');
      App.toast('DNS cache cleared', 'success');
    } catch (e) {
      App.toast('Failed: ' + e.message, 'error');
    }
  }
};
