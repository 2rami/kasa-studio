const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('studioDesktop', {
  pet: action => ipcRenderer.invoke('studio:pet', action),
  show: () => ipcRenderer.invoke('studio:show'),
});
