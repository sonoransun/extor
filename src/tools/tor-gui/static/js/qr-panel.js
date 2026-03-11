/* qr-panel.js - QR code display and scanning */

var QrPanel = {
  _scanning: false,
  _stream: null,
  _scanTimer: null,

  init: function() {
    var page = document.getElementById('page-qr');
    page.innerHTML =
      '<h2>QR Code Sharing</h2>' +
      '<div class="grid grid-2" style="margin-top:16px">' +
        '<div class="card">' +
          '<div class="card-label">Generate QR Code</div>' +
          '<div style="margin-top:12px">' +
            '<label style="font-size:13px;color:var(--text-secondary)">Select what to encode:</label>' +
            '<select class="select" id="qr-source" style="margin-top:4px" ' +
              'onchange="QrPanel.updateGenerate()">' +
              '<option value="bridges">Current Bridges</option>' +
              '<option value="custom">Custom Text</option>' +
            '</select>' +
            '<textarea id="qr-custom-text" class="textarea" ' +
              'style="margin-top:8px;height:100px;display:none" ' +
              'placeholder="Enter bridge line or torbridge:// URI"></textarea>' +
            '<button class="btn btn-primary" style="margin-top:8px" ' +
              'onclick="QrPanel.generate()">Generate QR</button>' +
          '</div>' +
          '<div id="qr-display" style="margin-top:16px;text-align:center"></div>' +
          '<div id="qr-actions" style="margin-top:8px;text-align:center;display:none">' +
            '<button class="btn btn-secondary" onclick="QrPanel.downloadPng()">Download PNG</button> ' +
            '<button class="btn btn-secondary" onclick="QrPanel.copyData()">Copy Data</button>' +
          '</div>' +
        '</div>' +
        '<div class="card">' +
          '<div class="card-label">Scan QR Code</div>' +
          '<div style="margin-top:12px">' +
            '<button class="btn btn-primary" id="qr-scan-btn" ' +
              'onclick="QrPanel.toggleScan()">Start Camera</button>' +
            ' <button class="btn btn-secondary" onclick="QrPanel.uploadImage()">Upload Image</button>' +
          '</div>' +
          '<div style="margin-top:12px;position:relative">' +
            '<video id="qr-video" style="width:100%;border-radius:6px;display:none" ' +
              'playsinline autoplay></video>' +
            '<canvas id="qr-scan-canvas" style="display:none"></canvas>' +
            '<input type="file" id="qr-file-input" accept="image/*" ' +
              'style="display:none" onchange="QrPanel.onFileSelected(event)">' +
          '</div>' +
          '<div style="margin-top:12px">' +
            '<div class="card-label" style="font-size:13px">Or paste bridge line / URI:</div>' +
            '<textarea id="qr-paste" class="textarea" style="margin-top:4px;height:80px" ' +
              'placeholder="Paste bridge line or torbridge:// URI"></textarea>' +
            '<button class="btn btn-secondary" style="margin-top:4px" ' +
              'onclick="QrPanel.importPasted()">Import</button>' +
          '</div>' +
          '<div id="qr-scan-result" style="margin-top:12px;display:none"></div>' +
        '</div>' +
      '</div>';
  },

  updateGenerate: function() {
    var src = document.getElementById('qr-source').value;
    var customEl = document.getElementById('qr-custom-text');
    if (customEl) {
      customEl.style.display = src === 'custom' ? '' : 'none';
    }
  },

  generate: async function() {
    var src = document.getElementById('qr-source').value;
    var data = '';

    if (src === 'bridges') {
      /* Fetch current bridges and format as torbridge:// URIs */
      try {
        var result = await TorAPI.getBridges();
        var bridges = Array.isArray(result) ? result :
          (result && result.bridges ? result.bridges : []);
        if (bridges.length === 0) {
          if (typeof App !== 'undefined' && App.toast) {
            App.toast('No bridges configured to encode.', 'warning');
          }
          return;
        }
        /* Encode each bridge line, separated by newlines */
        var lines = [];
        for (var i = 0; i < bridges.length; i++) {
          var b = bridges[i];
          if (typeof b === 'string') {
            lines.push('torbridge://' + b.replace(/\s+/g, '/'));
          } else if (b && b.address) {
            var line = '';
            if (b.transport) line += b.transport + '/';
            line += b.address;
            if (b.fingerprint) line += '/' + b.fingerprint;
            lines.push('torbridge://' + line);
          }
        }
        data = lines.join('\n');
      } catch (e) {
        if (typeof App !== 'undefined' && App.toast) {
          App.toast('Failed to fetch bridges: ' + e.message, 'error');
        }
        return;
      }
    } else {
      data = (document.getElementById('qr-custom-text').value || '').trim();
      if (!data) {
        if (typeof App !== 'undefined' && App.toast) {
          App.toast('Please enter text to encode.', 'warning');
        }
        return;
      }
    }

    /* Store data for copy/download */
    this._currentData = data;

    /* Request QR PNG from server */
    var imgUrl = TorAPI.getQrUrl(data);
    var display = document.getElementById('qr-display');
    var actions = document.getElementById('qr-actions');

    display.innerHTML =
      '<img id="qr-img" src="' + this._escAttr(imgUrl) + '" alt="QR Code" ' +
        'style="max-width:256px;border-radius:8px;background:white;padding:12px" ' +
        'onerror="QrPanel._onQrError()">' +
      '<div style="margin-top:8px;font-size:12px;color:var(--text-secondary);' +
        'word-break:break-all;max-height:80px;overflow-y:auto">' +
        this._esc(data.length > 200 ? data.substring(0, 200) + '...' : data) +
      '</div>';
    if (actions) actions.style.display = '';
  },

  _onQrError: function() {
    var display = document.getElementById('qr-display');
    if (display) {
      display.innerHTML =
        '<div style="padding:20px;color:var(--accent-red)">' +
          'Failed to generate QR code. The server may not support QR generation.' +
        '</div>';
    }
    var actions = document.getElementById('qr-actions');
    if (actions) actions.style.display = 'none';
  },

  downloadPng: function() {
    var img = document.getElementById('qr-img');
    if (!img || !img.src) return;

    var a = document.createElement('a');
    a.href = img.src;
    a.download = 'tor-bridge-qr.png';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
  },

  copyData: function() {
    if (!this._currentData) return;
    this._copyToClipboard(this._currentData, 'QR data copied to clipboard.');
  },

  toggleScan: function() {
    if (this._scanning) {
      this.stopScan();
    } else {
      this.startScan();
    }
  },

  startScan: async function() {
    var video = document.getElementById('qr-video');
    var scanCanvas = document.getElementById('qr-scan-canvas');
    if (!video || !scanCanvas) return;

    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Camera access not supported in this browser.', 'error');
      }
      return;
    }

    try {
      this._stream = await navigator.mediaDevices.getUserMedia({
        video: { facingMode: 'environment' }
      });
      video.srcObject = this._stream;
      video.style.display = 'block';
      this._scanning = true;

      var btn = document.getElementById('qr-scan-btn');
      if (btn) btn.textContent = 'Stop Camera';

      this._scanLoop(video, scanCanvas);
    } catch (e) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Camera access denied: ' + e.message, 'error');
      }
    }
  },

  _scanLoop: function(video, scanCanvas) {
    if (!this._scanning) return;
    var self = this;

    if (video.readyState === video.HAVE_ENOUGH_DATA) {
      scanCanvas.width = video.videoWidth;
      scanCanvas.height = video.videoHeight;
      var ctx = scanCanvas.getContext('2d');
      ctx.drawImage(video, 0, 0);

      var imageData = ctx.getImageData(0, 0, scanCanvas.width, scanCanvas.height);

      if (typeof jsQR === 'function') {
        var code = jsQR(imageData.data, imageData.width, imageData.height, {
          inversionAttempts: 'dontInvert'
        });
        if (code && code.data) {
          self.stopScan();
          self._parseScanResult(code.data);
          return;
        }
      }
    }

    this._scanTimer = setTimeout(function() {
      self._scanLoop(video, scanCanvas);
    }, 200);
  },

  stopScan: function() {
    this._scanning = false;
    if (this._scanTimer) {
      clearTimeout(this._scanTimer);
      this._scanTimer = null;
    }
    if (this._stream) {
      var tracks = this._stream.getTracks();
      for (var i = 0; i < tracks.length; i++) {
        tracks[i].stop();
      }
      this._stream = null;
    }
    var video = document.getElementById('qr-video');
    if (video) {
      video.style.display = 'none';
      video.srcObject = null;
    }
    var btn = document.getElementById('qr-scan-btn');
    if (btn) btn.textContent = 'Start Camera';
  },

  uploadImage: function() {
    document.getElementById('qr-file-input').click();
  },

  onFileSelected: function(event) {
    var file = event.target.files[0];
    if (!file) return;

    var self = this;
    var reader = new FileReader();
    reader.onload = function(e) {
      var img = new Image();
      img.onload = function() {
        var canvas = document.createElement('canvas');
        canvas.width = img.width;
        canvas.height = img.height;
        var ctx = canvas.getContext('2d');
        ctx.drawImage(img, 0, 0);
        var imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);

        if (typeof jsQR === 'function') {
          var code = jsQR(imageData.data, imageData.width, imageData.height);
          if (code && code.data) {
            self._parseScanResult(code.data);
          } else {
            if (typeof App !== 'undefined' && App.toast) {
              App.toast('No QR code found in image.', 'warning');
            }
          }
        } else {
          if (typeof App !== 'undefined' && App.toast) {
            App.toast('QR scanner library (jsQR) not loaded.', 'error');
          }
        }
      };
      img.src = e.target.result;
    };
    reader.readAsDataURL(file);
    event.target.value = '';
  },

  importPasted: function() {
    var text = (document.getElementById('qr-paste').value || '').trim();
    if (!text) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Nothing to import.', 'warning');
      }
      return;
    }
    this._parseScanResult(text);
  },

  _parseScanResult: function(text) {
    var resultEl = document.getElementById('qr-scan-result');
    if (!resultEl) return;
    resultEl.style.display = '';

    /* Determine if this looks like a bridge */
    var bridges = [];
    var lines = text.split('\n');
    for (var i = 0; i < lines.length; i++) {
      var line = lines[i].trim();
      if (!line) continue;

      if (line.indexOf('torbridge://') === 0) {
        /* Decode torbridge:// URI: replace slashes back to spaces after scheme */
        var decoded = line.replace('torbridge://', '').replace(/\//g, ' ').trim();
        if (decoded) bridges.push(decoded);
      } else if (/^(obfs4|snowflake|meek_lite|meek|webtunnel)\s/.test(line) ||
                 /^\d+\.\d+\.\d+\.\d+:\d+/.test(line) ||
                 /^\[[\da-f:]+\]:\d+/i.test(line)) {
        /* Looks like a raw bridge line */
        if (/^Bridge\s+/i.test(line)) {
          line = line.replace(/^Bridge\s+/i, '');
        }
        bridges.push(line);
      }
    }

    var html = '<div class="card" style="background:var(--bg-tertiary);margin:0">';

    if (bridges.length > 0) {
      html += '<div class="card-label">Detected ' + bridges.length + ' bridge' +
        (bridges.length !== 1 ? 's' : '') + '</div>';
      for (var j = 0; j < bridges.length; j++) {
        html += '<div class="mono" style="font-size:12px;margin-top:8px;' +
          'word-break:break-all;padding:8px;background:var(--bg-primary);' +
          'border-radius:4px">' + this._esc(bridges[j]) + '</div>';
      }
      html += '<div style="margin-top:12px">' +
        '<button class="btn btn-primary btn-sm" onclick="QrPanel._importBridges()">Add to Bridges</button> ' +
        '<button class="btn btn-secondary btn-sm" onclick="QrPanel._copyScanResult()">Copy</button>' +
      '</div>';
      /* Store for import */
      this._scannedBridges = bridges;
    } else {
      html += '<div class="card-label">Scanned Result</div>' +
        '<div class="mono" style="font-size:12px;margin-top:8px;word-break:break-all">' +
          this._esc(text) + '</div>' +
        '<div style="margin-top:12px">' +
          '<button class="btn btn-secondary btn-sm" onclick="QrPanel._copyScanResult()">Copy</button>' +
        '</div>';
      this._scannedText = text;
    }

    html += '</div>';
    resultEl.innerHTML = html;

    if (typeof App !== 'undefined' && App.toast) {
      App.toast('QR code scanned successfully.', 'success');
    }
  },

  _importBridges: async function() {
    if (!this._scannedBridges || this._scannedBridges.length === 0) return;

    try {
      var result = await TorAPI.getBridges();
      var existing = Array.isArray(result) ? result :
        (result && result.bridges ? result.bridges : []);

      for (var i = 0; i < this._scannedBridges.length; i++) {
        existing.push(this._scannedBridges[i]);
      }

      await TorAPI.setBridges(existing);
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Added ' + this._scannedBridges.length + ' bridge(s).', 'success');
      }

      /* Also update BridgeManager if it has been initialized */
      if (typeof BridgeManager !== 'undefined' && BridgeManager.refresh) {
        BridgeManager.refresh();
      }
    } catch (e) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Failed to import bridges: ' + e.message, 'error');
      }
    }
  },

  _copyScanResult: function() {
    var text = '';
    if (this._scannedBridges && this._scannedBridges.length > 0) {
      text = this._scannedBridges.join('\n');
    } else if (this._scannedText) {
      text = this._scannedText;
    }
    if (text) {
      this._copyToClipboard(text, 'Copied to clipboard.');
    }
  },

  _copyToClipboard: function(text, successMsg) {
    if (navigator.clipboard) {
      navigator.clipboard.writeText(text).then(function() {
        if (typeof App !== 'undefined' && App.toast) {
          App.toast(successMsg || 'Copied.', 'success');
        }
      }).catch(function() {
        if (typeof App !== 'undefined' && App.toast) {
          App.toast('Copy failed.', 'error');
        }
      });
    } else {
      var ta = document.createElement('textarea');
      ta.value = text;
      ta.style.position = 'fixed';
      ta.style.opacity = '0';
      document.body.appendChild(ta);
      ta.select();
      try {
        document.execCommand('copy');
        if (typeof App !== 'undefined' && App.toast) {
          App.toast(successMsg || 'Copied.', 'success');
        }
      } catch (e) {
        if (typeof App !== 'undefined' && App.toast) {
          App.toast('Copy failed.', 'error');
        }
      }
      document.body.removeChild(ta);
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
