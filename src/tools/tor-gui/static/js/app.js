/* app.js - Main application entry point and router for tor-gui */

var App = {
  currentPage: 'dashboard',

  init: function() {
    var self = this;

    /* Hash-based routing */
    window.addEventListener('hashchange', function() { self.route(); });

    /* Initialize all pages */
    Dashboard.init();
    BridgeManager.init();
    TransportSetup.init();
    ConfigEditor.init();
    NetworkGlobe.init();
    CircuitView.init();
    QrPanel.init();
    LogViewer.init();
    OnionServices.init();

    /* Connect WebSocket */
    TorWS.connect();
    TorWS.on('_connected', function() {
      self.setConnectionStatus('connected');
    });
    TorWS.on('_disconnected', function() {
      self.setConnectionStatus('disconnected');
    });

    /* Auto-extract token from URL query string */
    var params = new URLSearchParams(location.search);
    var token = params.get('token');
    if (token) {
      TorAPI.setToken(token);
      /* Clean URL */
      history.replaceState(null, '', location.pathname + location.hash);
    }

    /* Initial route */
    this.route();
  },

  route: function() {
    var hash = location.hash.slice(1) || 'dashboard';
    /* Strip any query params from hash */
    var qIdx = hash.indexOf('?');
    if (qIdx !== -1) hash = hash.substring(0, qIdx);
    this.showPage(hash);
  },

  showPage: function(name) {
    /* Deactivate all pages and links */
    var pages = document.querySelectorAll('.page');
    for (var i = 0; i < pages.length; i++) {
      pages[i].classList.remove('active');
    }
    var links = document.querySelectorAll('.nav-link');
    for (var j = 0; j < links.length; j++) {
      links[j].classList.remove('active');
    }

    /* Activate the target page and link */
    var page = document.getElementById('page-' + name);
    var link = document.querySelector('[data-page="' + name + '"]');
    if (page) page.classList.add('active');
    if (link) link.classList.add('active');
    this.currentPage = name;

    /* Notify page of activation for lazy-loading (e.g. 3D globe) */
    if (name === 'network' && typeof NetworkGlobe !== 'undefined' &&
        NetworkGlobe.onShow) {
      NetworkGlobe.onShow();
    }
  },

  setConnectionStatus: function(status) {
    var el = document.getElementById('connection-status');
    if (!el) return;
    el.className = 'status-indicator ' + status;
    var textEl = el.querySelector('.status-text');
    if (textEl) {
      if (status === 'connected') {
        textEl.textContent = 'Connected';
      } else if (status === 'connecting') {
        textEl.textContent = 'Connecting...';
      } else {
        textEl.textContent = 'Disconnected';
      }
    }
  },

  toast: function(message, type) {
    type = type || 'info';
    var t = document.createElement('div');
    t.className = 'toast toast-' + type;
    t.textContent = message;
    document.body.appendChild(t);

    /* Stack multiple toasts */
    var existing = document.querySelectorAll('.toast');
    var offset = 24;
    for (var i = 0; i < existing.length - 1; i++) {
      offset += existing[i].offsetHeight + 8;
    }
    t.style.bottom = offset + 'px';

    /* Auto-dismiss */
    setTimeout(function() {
      t.style.opacity = '0';
      t.style.transition = 'opacity 0.3s, transform 0.3s';
      t.style.transform = 'translateX(100px)';
      setTimeout(function() {
        if (t.parentNode) t.parentNode.removeChild(t);
      }, 300);
    }, 3000);
  }
};

/* Initialize when DOM is ready */
document.addEventListener('DOMContentLoaded', function() {
  App.init();
});
