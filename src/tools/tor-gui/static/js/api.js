/* api.js - REST API client wrapper for tor-gui backend */

var TorAPI = {
  _token: null,
  _baseUrl: '',

  setToken: function(token) {
    this._token = token;
  },

  setBaseUrl: function(url) {
    this._baseUrl = url.replace(/\/$/, '');
  },

  _fetch: async function(method, path, body) {
    var opts = {
      method: method,
      headers: {
        'Content-Type': 'application/json'
      }
    };
    if (this._token) {
      opts.headers['Authorization'] = 'Bearer ' + this._token;
    }
    if (body !== undefined && body !== null) {
      opts.body = JSON.stringify(body);
    }

    var res;
    try {
      res = await fetch(this._baseUrl + path, opts);
    } catch (e) {
      throw new Error('Network error: ' + e.message);
    }

    if (!res.ok) {
      var err;
      try {
        err = await res.json();
      } catch (_e) {
        err = { error: res.statusText };
      }
      throw new Error(err.error || err.message || res.statusText);
    }

    var ct = res.headers.get('content-type');
    if (ct && ct.indexOf('application/json') !== -1) {
      return res.json();
    }
    if (ct && ct.indexOf('image/') !== -1) {
      return res.blob();
    }
    return res.text();
  },

  get: function(path) {
    return this._fetch('GET', path);
  },

  put: function(path, body) {
    return this._fetch('PUT', path, body);
  },

  post: function(path, body) {
    return this._fetch('POST', path, body);
  },

  del: function(path) {
    return this._fetch('DELETE', path);
  },

  /* Status & monitoring */
  getStatus: function() {
    return this.get('/api/status');
  },

  getBandwidth: function() {
    return this.get('/api/bandwidth');
  },

  getCircuits: function() {
    return this.get('/api/circuits');
  },

  getStreams: function() {
    return this.get('/api/streams');
  },

  getGuards: function() {
    return this.get('/api/guards');
  },

  getLogs: function(level, limit) {
    var params = [];
    if (level) params.push('level=' + encodeURIComponent(level));
    if (limit) params.push('limit=' + encodeURIComponent(limit));
    var qs = params.length ? '?' + params.join('&') : '';
    return this.get('/api/logs' + qs);
  },

  /* Configuration */
  getConfig: function(key) {
    return this.get('/api/config/' + encodeURIComponent(key));
  },

  setConfig: function(key, value) {
    return this.put('/api/config/' + encodeURIComponent(key), { value: value });
  },

  getAllConfig: function() {
    return this.get('/api/config');
  },

  loadConfig: function(config) {
    return this.post('/api/config/load', { config: config });
  },

  saveConfig: function() {
    return this.post('/api/config/save');
  },

  reloadConfig: function() {
    return this.post('/api/config/reload');
  },

  /* Bridges */
  getBridges: function() {
    return this.get('/api/bridges');
  },

  setBridges: function(bridges) {
    return this.put('/api/bridges', { bridges: bridges });
  },

  /* Transports */
  getTransports: function() {
    return this.get('/api/transports');
  },

  setTransports: function(transports) {
    return this.put('/api/transports', { transports: transports });
  },

  /* Onion services */
  getOnionServices: function() {
    return this.get('/api/onion');
  },

  createOnion: function(params) {
    return this.post('/api/onion', params);
  },

  deleteOnion: function(id) {
    return this.del('/api/onion/' + encodeURIComponent(id));
  },

  /* Signals */
  sendSignal: function(name) {
    return this.post('/api/signal/' + encodeURIComponent(name));
  },

  /* QR */
  getQrUrl: function(data) {
    return this._baseUrl + '/api/qr?data=' + encodeURIComponent(data);
  },

  /* GeoIP */
  getGeoip: function(ip) {
    return this.get('/api/geoip/' + encodeURIComponent(ip));
  }
};
