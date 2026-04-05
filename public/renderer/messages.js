const MESSAGE_POLL_INTERVAL_MS = 15 * 1000;

const messageState = {
  deviceId: 'default-clock',
  roomName: '',
  unreadCount: 0,
  messages: [],
  activeMessage: null
};

function formatMessageTime(isoString) {
  if (!isoString) return '';
  const date = new Date(isoString);
  if (Number.isNaN(date.getTime())) return '';
  return date.toLocaleTimeString('en-US', {
    hour: 'numeric',
    minute: '2-digit'
  });
}

function renderMessageState() {
  const card = document.getElementById('message-card');
  const empty = document.getElementById('message-empty');
  const text = document.getElementById('message-text');
  const meta = document.getElementById('message-meta');
  const edge = document.getElementById('message-edge-indicator');
  const messageFace = document.querySelector('.message-face');

  if (!card || !empty || !text || !meta || !edge || !messageFace) return;

  const active = messageState.activeMessage;
  edge.classList.toggle('has-unread', messageState.unreadCount > 0);
  edge.classList.toggle('has-important', active?.priority === 'important');

  if (!active) {
    card.hidden = true;
    empty.hidden = false;
    messageFace.classList.remove('has-important-message');
    return;
  }

  const timeLabel = formatMessageTime(active.createdAt);
  const metaParts = [];

  if (active.sender) {
    metaParts.push(`-${active.sender}`);
  }

  if (timeLabel) {
    metaParts.push(timeLabel);
  }

  text.textContent = active.text;
  meta.textContent = metaParts.join(', ');
  card.hidden = false;
  empty.hidden = true;
  messageFace.classList.toggle('has-important-message', active.priority === 'important');
}

async function fetchMessageConfig() {
  try {
    const response = await fetch('/config', { cache: 'no-store' });
    const data = await response.json();
    if (!response.ok || data.error) return;
    if (typeof data.deviceId === 'string' && data.deviceId.trim()) {
      messageState.deviceId = data.deviceId.trim();
    }
    if (typeof data.roomName === 'string') {
      messageState.roomName = data.roomName.trim();
    }
  } catch (error) {
    console.error('Failed to load message config:', error);
  }
}

async function fetchMessages() {
  try {
    const response = await fetch(`/api/messages?deviceId=${encodeURIComponent(messageState.deviceId)}`, {
      cache: 'no-store'
    });
    const data = await response.json();

    if (!response.ok || data.error) {
      console.error('Message fetch failed:', data.error || response.statusText);
      return;
    }

    messageState.messages = Array.isArray(data.messages) ? data.messages : [];
    messageState.unreadCount = Number.isFinite(data.unreadCount) ? data.unreadCount : 0;
    messageState.activeMessage = messageState.messages[0] || null;
    renderMessageState();
  } catch (error) {
    console.error('Failed to fetch messages:', error);
  }
}

async function acknowledgeActiveMessage() {
  if (!messageState.activeMessage) return;

  try {
    const response = await fetch(`/api/messages/${encodeURIComponent(messageState.activeMessage.id)}/ack`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({
        deviceId: messageState.deviceId
      })
    });

    const data = await response.json();
    if (!response.ok || data.error) {
      console.error('Failed to acknowledge message:', data.error || response.statusText);
      return;
    }

    await fetchMessages();
    if (window.appView?.setViewMode) {
      window.appView.setViewMode(window.appView.VIEW_MODES.CLOCK);
    }
  } catch (error) {
    console.error('Failed to acknowledge active message:', error);
  }
}

function setupMessageDismiss() {
  const messageView = document.querySelector('.view-message');
  if (!messageView) return;

  let pointerStart = null;

  messageView.addEventListener('pointerdown', (event) => {
    pointerStart = {
      x: event.clientX,
      y: event.clientY
    };
  });

  messageView.addEventListener('pointerup', (event) => {
    if (!pointerStart) return;

    const deltaX = event.clientX - pointerStart.x;
    const deltaY = event.clientY - pointerStart.y;
    pointerStart = null;

    if (Math.abs(deltaX) <= 12 && Math.abs(deltaY) <= 12) {
      acknowledgeActiveMessage();
    }
  });

  messageView.addEventListener('pointercancel', () => {
    pointerStart = null;
  });
}

async function initializeMessages() {
  await fetchMessageConfig();
  await fetchMessages();
  setupMessageDismiss();
  window.setInterval(fetchMessages, MESSAGE_POLL_INTERVAL_MS);
  window.addEventListener('focus', fetchMessages);
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden) {
      fetchMessages();
    }
  });
}

initializeMessages();
