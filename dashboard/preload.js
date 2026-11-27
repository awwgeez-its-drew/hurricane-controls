'use strict';
const { contextBridge, ipcRenderer } = require('electron');

const on = (ch, cb) => {
  const h = (_e, v) => cb(v);
  ipcRenderer.on(ch, h);
  return () => ipcRenderer.removeListener(ch, h);
};

contextBridge.exposeInMainWorld('hc', {
  state: () => ipcRenderer.invoke('state:get'),
  onState: (cb) => on('state', cb),
  onOta: (cb) => on('ota:progress', cb),
  req: (id, method, path, body, opts) => ipcRenderer.invoke('board:req', { id, method, path, body, ...(opts || {}) }),
  cfgGet: () => ipcRenderer.invoke('cfg:get'),
  cfgSave: (v) => ipcRenderer.invoke('cfg:save', v),
  cfgTest: (v) => ipcRenderer.invoke('cfg:test', v),
  reconnect: (id) => ipcRenderer.invoke('board:reconnect', id),
  csv: (id) => ipcRenderer.invoke('board:csv', id),
  ota: (id) => ipcRenderer.invoke('board:ota', id),
  info: () => ipcRenderer.invoke('app:info'),
});
