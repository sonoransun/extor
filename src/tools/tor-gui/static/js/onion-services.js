/* onion-services.js - Onion service management */

var OnionServices = {
  _services: [],

  init: function() {
    var page = document.getElementById('page-onion');
    page.innerHTML =
      '<h2>Onion Services</h2>' +
      '<div class="toolbar" style="margin-top:16px">' +
        '<button class="btn btn-primary" onclick="OnionServices.showCreateDialog()">Create Onion Service</button>' +
        '<button class="btn btn-secondary" onclick="OnionServices.refresh()">Refresh</button>' +
      '</div>' +
      '<div id="onion-list" style="margin-top:16px"></div>' +
      '<div id="onion-empty" class="card" style="text-align:center;padding:40px;' +
        'color:var(--text-secondary);display:none">' +
        'No onion services running. Create one to host a hidden service.' +
      '</div>';
    this.refresh();
  },

  refresh: async function() {
    try {
      var result = await TorAPI.getOnionServices();
      this._services = Array.isArray(result) ? result :
        (result && result.services ? result.services : []);
    } catch (e) {
      this._services = [];
      console.error('Failed to load onion services:', e);
    }
    this._render();
  },

  _render: function() {
    var listEl = document.getElementById('onion-list');
    var emptyEl = document.getElementById('onion-empty');
    if (!listEl) return;

    if (this._services.length === 0) {
      listEl.innerHTML = '';
      if (emptyEl) emptyEl.style.display = '';
      return;
    }

    if (emptyEl) emptyEl.style.display = 'none';

    var html = '';
    for (var i = 0; i < this._services.length; i++) {
      var svc = this._services[i];
      var addr = svc.address || svc.onion_address || svc.service_id || '--';
      if (addr !== '--' && addr.indexOf('.onion') === -1) {
        addr = addr + '.onion';
      }

      var ports = svc.ports || svc.virtual_port || '--';
      if (Array.isArray(ports)) {
        ports = ports.join(', ');
      }

      var status = svc.status || 'active';
      var svcType = svc.type || (svc.ephemeral ? 'ephemeral' : 'persistent');
      var serviceId = svc.service_id || svc.id || addr;

      var statusBadge = status === 'active'
        ? '<span class="badge badge-success">Active</span>'
        : '<span class="badge badge-warning">' + this._esc(status) + '</span>';

      var typeBadge = '<span class="badge badge-info">' +
        this._esc(svcType) + '</span>';

      html += '<div class="card" style="margin-bottom:12px">' +
        '<div style="display:flex;justify-content:space-between;align-items:flex-start">' +
          '<div style="flex:1;min-width:0">' +
            '<div style="display:flex;align-items:center;gap:8px;margin-bottom:8px">' +
              statusBadge + typeBadge +
            '</div>' +
            '<div class="mono" style="font-size:14px;word-break:break-all">' +
              this._esc(addr) +
            '</div>' +
            '<div style="font-size:12px;color:var(--text-secondary);margin-top:4px">' +
              'Ports: ' + this._esc(String(ports)) +
            '</div>' +
          '</div>' +
          '<div style="display:flex;gap:6px;flex-shrink:0;margin-left:12px">' +
            '<button class="btn btn-secondary btn-sm" ' +
              'onclick="OnionServices.copyAddress(\'' + this._escAttr(addr) + '\')">Copy</button>' +
            '<button class="btn btn-secondary btn-sm" ' +
              'onclick="OnionServices.showQr(\'' + this._escAttr(addr) + '\')">QR</button>' +
            '<button class="btn btn-danger btn-sm" ' +
              'onclick="OnionServices.deleteService(\'' + this._escAttr(serviceId) + '\')">Delete</button>' +
          '</div>' +
        '</div>' +
      '</div>';
    }

    listEl.innerHTML = html;
  },

  showCreateDialog: function() {
    var overlay = document.createElement('div');
    overlay.className = 'modal-overlay';
    overlay.id = 'onion-modal-overlay';

    overlay.innerHTML =
      '<div class="modal" onclick="event.stopPropagation()">' +
        '<div class="modal-header">' +
          '<h3>Create Onion Service</h3>' +
          '<button class="modal-close" onclick="OnionServices._closeModal()">&times;</button>' +
        '</div>' +
        '<div class="form-group">' +
          '<label>Virtual Port</label>' +
          '<input class="input" type="number" id="onion-vport" ' +
            'placeholder="80" value="80" min="1" max="65535">' +
          '<div class="hint">The port visitors will connect to via the .onion address</div>' +
        '</div>' +
        '<div class="form-group">' +
          '<label>Target Host</label>' +
          '<input class="input" type="text" id="onion-target-host" ' +
            'placeholder="127.0.0.1" value="127.0.0.1">' +
          '<div class="hint">Local address to forward traffic to</div>' +
        '</div>' +
        '<div class="form-group">' +
          '<label>Target Port</label>' +
          '<input class="input" type="number" id="onion-target-port" ' +
            'placeholder="80" value="80" min="1" max="65535">' +
          '<div class="hint">Local port to forward traffic to (defaults to virtual port)</div>' +
        '</div>' +
        '<div class="form-group">' +
          '<label>Key Type</label>' +
          '<select class="select" id="onion-keytype">' +
            '<option value="NEW:BEST">Generate new key (best available)</option>' +
            '<option value="NEW:ED25519-V3">Generate new ED25519-V3 key</option>' +
          '</select>' +
        '</div>' +
        '<div class="form-group">' +
          '<label style="display:flex;align-items:center;gap:6px;font-weight:normal;cursor:pointer">' +
            '<input type="checkbox" id="onion-detach" checked>' +
            'Detach (survives controller disconnect)' +
          '</label>' +
        '</div>' +
        '<div class="form-group">' +
          '<label style="display:flex;align-items:center;gap:6px;font-weight:normal;cursor:pointer">' +
            '<input type="checkbox" id="onion-discard-pk">' +
            'Discard private key (truly ephemeral)' +
          '</label>' +
          '<div class="hint">If checked, the .onion address cannot be recovered after restart</div>' +
        '</div>' +
        '<div class="modal-footer">' +
          '<button class="btn btn-secondary" onclick="OnionServices._closeModal()">Cancel</button>' +
          '<button class="btn btn-primary" onclick="OnionServices._createFromDialog()">Create</button>' +
        '</div>' +
      '</div>';

    document.body.appendChild(overlay);
    overlay.addEventListener('click', function(e) {
      if (e.target === overlay) OnionServices._closeModal();
    });
  },

  _createFromDialog: async function() {
    var virtualPort = parseInt(document.getElementById('onion-vport').value, 10);
    var targetHost = document.getElementById('onion-target-host').value.trim() || '127.0.0.1';
    var targetPort = parseInt(document.getElementById('onion-target-port').value, 10);
    var keyType = document.getElementById('onion-keytype').value;
    var detach = document.getElementById('onion-detach').checked;
    var discardPk = document.getElementById('onion-discard-pk').checked;

    if (!virtualPort || virtualPort < 1 || virtualPort > 65535) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Please enter a valid virtual port (1-65535).', 'error');
      }
      return;
    }

    if (!targetPort || targetPort < 1 || targetPort > 65535) {
      targetPort = virtualPort;
    }

    var portSpec = virtualPort + ',' + targetHost + ':' + targetPort;

    try {
      var result = await TorAPI.createOnion({
        port: portSpec,
        key_type: keyType,
        detach: detach,
        discard_pk: discardPk
      });

      this._closeModal();

      if (result && (result.address || result.service_id)) {
        var addr = result.address || result.service_id;
        if (addr.indexOf('.onion') === -1) addr += '.onion';

        if (typeof App !== 'undefined' && App.toast) {
          App.toast('Onion service created: ' + addr, 'success');
        }

        /* Show private key warning if returned */
        if (result.private_key && !discardPk) {
          this._showKeyDialog(addr, result.private_key);
        }
      } else {
        if (typeof App !== 'undefined' && App.toast) {
          App.toast('Onion service created.', 'success');
        }
      }

      this.refresh();
    } catch (e) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Failed to create service: ' + e.message, 'error');
      }
    }
  },

  _showKeyDialog: function(addr, privateKey) {
    var overlay = document.createElement('div');
    overlay.className = 'modal-overlay';
    overlay.id = 'onion-key-overlay';

    overlay.innerHTML =
      '<div class="modal" onclick="event.stopPropagation()">' +
        '<div class="modal-header">' +
          '<h3>Save Your Private Key</h3>' +
          '<button class="modal-close" onclick="OnionServices._closeKeyDialog()">&times;</button>' +
        '</div>' +
        '<p style="color:var(--accent-yellow);margin-bottom:12px">' +
          'Save this private key securely. You will need it to recreate this onion address.</p>' +
        '<div style="margin-bottom:12px">' +
          '<strong>Address:</strong> <span class="mono">' + this._esc(addr) + '</span>' +
        '</div>' +
        '<textarea class="textarea" style="height:120px;font-family:monospace;font-size:12px" ' +
          'readonly>' + this._esc(privateKey) + '</textarea>' +
        '<div class="modal-footer">' +
          '<button class="btn btn-secondary" onclick="OnionServices._copyKey()">Copy Key</button>' +
          '<button class="btn btn-primary" onclick="OnionServices._closeKeyDialog()">Done</button>' +
        '</div>' +
      '</div>';

    this._savedKey = privateKey;
    document.body.appendChild(overlay);
    overlay.addEventListener('click', function(e) {
      if (e.target === overlay) OnionServices._closeKeyDialog();
    });
  },

  _copyKey: function() {
    if (!this._savedKey) return;
    if (navigator.clipboard) {
      navigator.clipboard.writeText(this._savedKey).then(function() {
        if (typeof App !== 'undefined' && App.toast) {
          App.toast('Private key copied to clipboard.', 'success');
        }
      });
    }
  },

  _closeKeyDialog: function() {
    var overlay = document.getElementById('onion-key-overlay');
    if (overlay && overlay.parentNode) {
      overlay.parentNode.removeChild(overlay);
    }
    this._savedKey = null;
  },

  deleteService: async function(serviceId) {
    if (!confirm('Delete this onion service? This cannot be undone for ephemeral services.')) {
      return;
    }

    try {
      await TorAPI.deleteOnion(serviceId);
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Onion service deleted.', 'success');
      }
      this.refresh();
    } catch (e) {
      if (typeof App !== 'undefined' && App.toast) {
        App.toast('Failed to delete service: ' + e.message, 'error');
      }
    }
  },

  copyAddress: function(addr) {
    if (navigator.clipboard) {
      navigator.clipboard.writeText(addr).then(function() {
        if (typeof App !== 'undefined' && App.toast) {
          App.toast('Address copied to clipboard.', 'success');
        }
      }).catch(function() {
        if (typeof App !== 'undefined' && App.toast) {
          App.toast('Copy failed.', 'error');
        }
      });
    } else {
      var ta = document.createElement('textarea');
      ta.value = addr;
      ta.style.position = 'fixed';
      ta.style.opacity = '0';
      document.body.appendChild(ta);
      ta.select();
      try {
        document.execCommand('copy');
        if (typeof App !== 'undefined' && App.toast) {
          App.toast('Address copied.', 'success');
        }
      } catch (e) {
        if (typeof App !== 'undefined' && App.toast) {
          App.toast('Copy failed.', 'error');
        }
      }
      document.body.removeChild(ta);
    }
  },

  showQr: function(address) {
    /* Show modal with QR code of the .onion address */
    var url = TorAPI.getQrUrl(address);

    var overlay = document.createElement('div');
    overlay.className = 'modal-overlay';
    overlay.id = 'onion-qr-overlay';

    overlay.innerHTML =
      '<div class="modal" onclick="event.stopPropagation()" style="text-align:center">' +
        '<div class="modal-header">' +
          '<h3>Onion Service QR Code</h3>' +
          '<button class="modal-close" onclick="OnionServices._closeQrDialog()">&times;</button>' +
        '</div>' +
        '<img src="' + this._escAttr(url) + '" alt="QR Code" ' +
          'style="max-width:256px;border-radius:8px;background:white;padding:12px;margin:16px auto" ' +
          'onerror="this.style.display=\'none\'">' +
        '<div class="mono" style="font-size:12px;word-break:break-all;margin-top:8px">' +
          this._esc(address) +
        '</div>' +
        '<div class="modal-footer" style="justify-content:center">' +
          '<button class="btn btn-secondary" ' +
            'onclick="OnionServices.copyAddress(\'' + this._escAttr(address) + '\')">Copy Address</button>' +
          '<button class="btn btn-primary" onclick="OnionServices._closeQrDialog()">Close</button>' +
        '</div>' +
      '</div>';

    document.body.appendChild(overlay);
    overlay.addEventListener('click', function(e) {
      if (e.target === overlay) OnionServices._closeQrDialog();
    });
  },

  _closeQrDialog: function() {
    var overlay = document.getElementById('onion-qr-overlay');
    if (overlay && overlay.parentNode) {
      overlay.parentNode.removeChild(overlay);
    }
  },

  _closeModal: function() {
    var overlay = document.getElementById('onion-modal-overlay');
    if (overlay && overlay.parentNode) {
      overlay.parentNode.removeChild(overlay);
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
