// ws.js — thin WebSocket wrapper. Binary mode, callback-style.

export function connect(url, { onOpen, onClose, onError, onMessage }) {
  const ws = new WebSocket(url, 'lhc-v0');
  ws.binaryType = 'arraybuffer';
  ws.addEventListener('open',    () => onOpen && onOpen());
  ws.addEventListener('close',   () => onClose && onClose());
  ws.addEventListener('error',   (e) => onError && onError(e));
  ws.addEventListener('message', (ev) => {
    if (ev.data instanceof ArrayBuffer) {
      onMessage && onMessage(ev.data);
    } else {
      // ignore text frames (we only speak binary)
    }
  });
  return ws;
}
