/* ws.js - WebSocket event handler for real-time updates */

var TorWS = {
  _ws: null,
  _listeners: {},
  _reconnectDelay: 1000,
  _maxReconnectDelay: 30000,
  _reconnectTimer: null,
  _intentionalClose: false,

  connect: function() {
    if (this._ws && (this._ws.readyState === WebSocket.CONNECTING ||
                     this._ws.readyState === WebSocket.OPEN)) {
      return;
    }

    this._intentionalClose = false;
    var proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    var url = proto + '//' + location.host + '/ws';

    try {
      this._ws = new WebSocket(url);
    } catch (e) {
      console.error('[WS] Failed to create WebSocket:', e);
      this._scheduleReconnect();
      return;
    }

    var self = this;

    this._ws.onopen = function() {
      console.log('[WS] Connected');
      self._reconnectDelay = 1000;
      self._emit('_connected', {});
    };

    this._ws.onclose = function(ev) {
      console.log('[WS] Disconnected (code=' + ev.code + ')');
      self._emit('_disconnected', {});
      if (!self._intentionalClose) {
        self._scheduleReconnect();
      }
    };

    this._ws.onerror = function(e) {
      console.error('[WS] Error:', e);
    };

    this._ws.onmessage = function(e) {
      try {
        var msg = JSON.parse(e.data);
        if (msg.type) {
          self._emit(msg.type, msg);
        }
      } catch (err) {
        console.error('[WS] Parse error:', err);
      }
    };
  },

  disconnect: function() {
    this._intentionalClose = true;
    if (this._reconnectTimer) {
      clearTimeout(this._reconnectTimer);
      this._reconnectTimer = null;
    }
    if (this._ws) {
      this._ws.close();
      this._ws = null;
    }
  },

  _scheduleReconnect: function() {
    var self = this;
    console.log('[WS] Reconnecting in ' + this._reconnectDelay + 'ms');
    this._reconnectTimer = setTimeout(function() {
      self._reconnectTimer = null;
      self.connect();
    }, this._reconnectDelay);
    this._reconnectDelay = Math.min(
      this._reconnectDelay * 2,
      this._maxReconnectDelay
    );
  },

  on: function(type, fn) {
    if (!this._listeners[type]) {
      this._listeners[type] = [];
    }
    this._listeners[type].push(fn);
  },

  off: function(type, fn) {
    if (!this._listeners[type]) return;
    this._listeners[type] = this._listeners[type].filter(function(f) {
      return f !== fn;
    });
  },

  _emit: function(type, data) {
    var fns = this._listeners[type] || [];
    fns.forEach(function(fn) {
      try { fn(data); } catch (e) {
        console.error('[WS] Handler error for "' + type + '":', e);
      }
    });
    /* Also emit to wildcard listeners */
    var all = this._listeners['*'] || [];
    all.forEach(function(fn) {
      try { fn(data); } catch (e) {
        console.error('[WS] Wildcard handler error:', e);
      }
    });
  },

  send: function(msg) {
    if (this._ws && this._ws.readyState === WebSocket.OPEN) {
      this._ws.send(typeof msg === 'string' ? msg : JSON.stringify(msg));
    }
  },

  isConnected: function() {
    return this._ws && this._ws.readyState === WebSocket.OPEN;
  }
};
